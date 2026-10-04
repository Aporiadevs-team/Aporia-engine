#include "Aporia/Physics/PhysicsWorld.hpp"
#include "../internal/PhysicsInternal.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Aporia {

struct PhysicsWorld::Impl {
    std::vector<apr::Body> bodies;
    Vec3 gravity;
    double timestepHz = 300.0;
    std::size_t lastStepCollisionCount = 0;
    ContactInfo lastContact;
    std::uint64_t nextId = 1;

    std::uint32_t solverIterations = 8;
    std::vector<ContactInfo> lastContactsList;
    std::vector<apr::ActiveContact> activeContacts;
    std::vector<apr::Contact> scratchContacts;
    double accumulator = 0.0;
    std::uint32_t maxSubsteps = 8;
    std::uint32_t lastSubstepCount = 0;

    // O(1) id -> index lookup (bodies only ever grow).
    std::unordered_map<std::uint64_t, std::size_t> indexById;

    // Uniform spatial-hash broad phase, rebuilt every substep.
    std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> grid;
    std::vector<std::uint32_t> pairStamp;
    std::uint32_t pairStampCounter = 0;

    static std::uint64_t packCell(std::int32_t x, std::int32_t y, std::int32_t z) noexcept {
        const std::uint64_t bx = static_cast<std::uint32_t>(x) & 0x1FFFFFu;
        const std::uint64_t by = static_cast<std::uint32_t>(y) & 0x1FFFFFu;
        const std::uint64_t bz = static_cast<std::uint32_t>(z) & 0x1FFFFFu;
        return bx | (by << 21) | (bz << 42);
    }

    void gridRange(const apr::Body& body, double cellSize,
                   std::int32_t& x0, std::int32_t& y0, std::int32_t& z0,
                   std::int32_t& x1, std::int32_t& y1, std::int32_t& z1) const noexcept {
        const double r = std::max(body.boundingRadius, 0.05);
        x0 = static_cast<std::int32_t>(std::floor((body.settings.position.x - r) / cellSize));
        y0 = static_cast<std::int32_t>(std::floor((body.settings.position.y - r) / cellSize));
        z0 = static_cast<std::int32_t>(std::floor((body.settings.position.z - r) / cellSize));
        x1 = static_cast<std::int32_t>(std::floor((body.settings.position.x + r) / cellSize));
        y1 = static_cast<std::int32_t>(std::floor((body.settings.position.y + r) / cellSize));
        z1 = static_cast<std::int32_t>(std::floor((body.settings.position.z + r) / cellSize));
    }

    apr::Body* find(std::uint64_t id) noexcept {
        const auto it = indexById.find(id);
        return it == indexById.end() ? nullptr : &bodies[it->second];
    }

    const apr::Body* find(std::uint64_t id) const noexcept {
        const auto it = indexById.find(id);
        return it == indexById.end() ? nullptr : &bodies[it->second];
    }

    static double inverseMass(const apr::Body& body) noexcept {
        if (body.settings.isStatic || body.settings.mass <= 0.0) {
            return 0.0;
        }
        return 1.0 / body.settings.mass;
    }

    static Vec3 applyInverseInertia(const apr::Body& body, const Vec3& value) noexcept {
        if (body.settings.isStatic) {
            return {};
        }

        const Vec3 local = body.settings.orientation.inverseRotate(value);
        const Vec3 scaled{
            body.inverseInertia.x * local.x,
            body.inverseInertia.y * local.y,
            body.inverseInertia.z * local.z,
        };
        return body.settings.orientation.rotate(scaled);
    }

    // Smooth body-turn predictor: a rate-limited wheel plus a bicycle-model yaw
    // rate, limited by grip and modulated by slope, mass/inertia and shape.
    void applySteering(apr::Body& body, double deltaSeconds) noexcept {
        if (!body.steeringEnabled || body.settings.isStatic) {
            return;
        }
        const SteeringConfig& config = body.steering;

        // 1. Body frame: up = body vertical, forward = local +Y, right = local +X.
        const Quat& orientation = body.settings.orientation;
        const Vec3 up = orientation.rotate({0.0, 0.0, 1.0}).normalized();
        const Vec3 forward = orientation.rotate({0.0, 1.0, 0.0}).normalized();
        const Vec3 right = cross(forward, up).normalized();

        const double forwardSpeed = dot(body.velocity, forward);
        const double speed = std::abs(forwardSpeed);

        // 2. Smooth the wheel, with speed-sensitive lock: a heavy car turns its
        //    wheels less as speed rises, so fast corners are gentle (understeer)
        //    rather than a twitch. Recentering is a touch faster than turning in.
        const double lockScale = 1.0 / (1.0 + speed * 0.04);
        const double wanted = std::clamp(body.steerInput, -1.0, 1.0) *
            config.maxSteerAngle * lockScale;
        const bool recentering = std::abs(body.steerInput) < 1.0e-3;
        const double travel = (recentering ? config.steerReturn : config.steerRate) * deltaSeconds;
        body.steerAngle += std::clamp(wanted - body.steerAngle, -travel, travel);

        // 3. Shape: a longer wheelbase and a wider track are more stable.
        const double wheelbase = config.wheelbase > 0.0
            ? config.wheelbase
            : std::max(1.0, 2.0 * body.halfExtents.y);
        const double track = std::max(0.5, 2.0 * body.halfExtents.x);
        const double shapeStability = std::clamp(wheelbase / track, 0.5, 3.0);

        // 4. Bicycle model: yaw rate grows with speed, so a still body cannot
        //    spin in place and low-speed turns are gentle, not twitchy.
        double targetYaw = speed > 1.0e-4
            ? (forwardSpeed / wheelbase) * std::tan(body.steerAngle)
            : 0.0;

        // 5. Slope: gravity on the body's lateral axis steers it downhill,
        //    faded in with speed like a real lateral force.
        const double lateralGravity = dot(gravity, right);
        const double moving = speed / (speed + 2.0);
        targetYaw += config.slopeInfluence * lateralGravity * moving / std::max(speed, 1.5);

        // 6. Grip: cap lateral acceleration (v * yaw); a leaning body loses
        //    normal load, so the budget shrinks as it tilts.
        const double upright = std::clamp(dot(up, Vec3{0.0, 0.0, 1.0}), 0.0, 1.0);
        const double grip = config.gripLimit * (0.3 + 0.7 * upright);
        const double maxYaw = grip / std::max(speed, 0.5);
        targetYaw = std::clamp(targetYaw, -maxYaw, maxYaw);

        // 7. Mass/shape: heavier, higher-inertia bodies build yaw more slowly.
        const double yawInertia = body.inverseInertia.z > 0.0 ? 1.0 / body.inverseInertia.z : 1.0;
        constexpr double referenceYawInertia = 800.0;
        const double inertiaScale = std::clamp(referenceYawInertia / std::max(yawInertia, 1.0e-3), 0.2, 2.0);
        const double response = std::clamp(
            config.yawResponse * inertiaScale * shapeStability / 1.5, 0.5, 20.0);

        // 8. Blend only the yaw component toward the target, leaving terrain
        //    roll/pitch intact. Wheels off the ground have no authority.
        const double blend = body.onGround ? std::clamp(response * deltaSeconds, 0.0, 1.0) : 0.0;
        Vec3 angular = body.angularVelocity;
        const double currentYaw = dot(angular, up);
        const double newYaw = currentYaw + (targetYaw - currentYaw) * blend;
        angular += up * (newYaw - currentYaw);
        body.angularVelocity = angular;
        body.lastYawRate = newYaw;

        // 9. Lateral tire grip: instead of forcing the heading while the
        //    velocity keeps going straight (which is a drift), the sideways
        //    velocity is bled off at the friction budget. On asphalt this makes
        //    the chassis track its nose; once cornering exceeds the grip budget
        //    the tires break loose and the car really does slide.
        if (body.onGround) {
            Vec3 lateralAxis{right.x, right.y, 0.0};
            const double lateralLength = lateralAxis.length();
            if (lateralLength > 1.0e-4) {
                lateralAxis = lateralAxis / lateralLength;
                const double lateralSpeed = dot(body.velocity, lateralAxis);
                double gripAccel = grip;
                if (std::abs(lateralSpeed) > 8.0) {
                    gripAccel *= 0.55; // sliding: kinetic friction is lower than static
                }
                const double maxStep = gripAccel * deltaSeconds;
                body.velocity -= lateralAxis * std::clamp(lateralSpeed, -maxStep, maxStep);
            }
        }
    }

    // One fixed substep: integrate forces, integrate motion, then resolve.
    void integrateSubstep(double deltaSeconds) noexcept {
        // Ground contact from the previous step gates the steering predictor.
        for (apr::Body& body : bodies) {
            body.onGround = false;
        }
        for (const apr::ActiveContact& contact : activeContacts) {
            apr::Body& first = bodies[contact.firstIndex];
            apr::Body& second = bodies[contact.secondIndex];
            if (first.colliderType != apr::ColliderType::Heightfield) {
                first.onGround = true;
            }
            if (second.colliderType != apr::ColliderType::Heightfield) {
                second.onGround = true;
            }
        }

        for (apr::Body& body : bodies) {
            const double bodyInverseMass = inverseMass(body);
            if (bodyInverseMass <= 0.0) {
                continue;
            }

            body.velocity += (gravity + body.force * bodyInverseMass) * deltaSeconds;
            body.settings.position += body.velocity * deltaSeconds;

            const Vec3 localTorque = body.settings.orientation.inverseRotate(body.torque);
            const Vec3 localAngularAcceleration{
                body.inverseInertia.x * localTorque.x,
                body.inverseInertia.y * localTorque.y,
                body.inverseInertia.z * localTorque.z,
            };
            body.angularVelocity += body.settings.orientation.rotate(localAngularAcceleration) * deltaSeconds;
            const double angularSpeed = body.angularVelocity.length();
            if (angularSpeed > std::numeric_limits<double>::epsilon()) {
                const Quat rotation = Quat::fromAxisAngle(body.angularVelocity, angularSpeed * deltaSeconds);
                body.settings.orientation = (rotation * body.settings.orientation).normalized();
            }
            body.force = {};
            body.torque = {};
        }

        for (apr::Body& body : bodies) {
            applySteering(body, deltaSeconds);
        }

        resolveCollisions();
    }

    BodyHandle add(
        apr::ColliderType colliderType,
        const BodySettings& input,
        double radius,
        const Vec3& halfExtents,
        double halfHeight) {
        const std::optional<BodySettings> settings = apr::normalizeSettings(input);
        if (!settings.has_value()) {
            return {};
        }

        if (!apr::finite(radius) || radius < 0.0) {
            return {};
        }
        if (!apr::finite(halfExtents) || !apr::finite(halfHeight) || halfHeight < 0.0) {
            return {};
        }

        apr::Body body;
        body.id = nextId++;
        body.settings = *settings;
        body.colliderType = colliderType;
        body.radius = radius <= 0.0 ? 0.001 : radius;
        body.halfExtents = {
            std::max(0.001, std::abs(halfExtents.x)),
            std::max(0.001, std::abs(halfExtents.y)),
            std::max(0.001, std::abs(halfExtents.z)),
        };
        body.halfHeight = halfHeight;
        switch (colliderType) {
        case apr::ColliderType::Sphere:
            body.boundingRadius = body.radius;
            break;
        case apr::ColliderType::Box:
            body.boundingRadius = body.halfExtents.length();
            break;
        case apr::ColliderType::Capsule:
            body.boundingRadius = body.halfHeight + body.radius;
            break;
        default:
            body.boundingRadius = 0.0;
            break;
        }
        body.velocity = settings->linearVelocity;
        body.angularVelocity = settings->angularVelocity;
        body.force = {};
        body.torque = {};
        body.inverseInertia = apr::inverseInertiaFor(body);
        bodies.push_back(std::move(body));
        indexById[bodies.back().id] = bodies.size() - 1;
        return BodyHandle{bodies.back().id};
    }

    BodyHandle addHeightfield(const TerrainDesc& description, const TerrainHeightfield& field) {
        if (field.heights.empty() || field.resolution < 2 || field.worldSize <= 0.0f) {
            return {};
        }
        if (!apr::finite(description.position) || !apr::finite(description.orientation)) {
            return {};
        }

        apr::Body body;
        body.id = nextId++;
        body.settings = BodySettings{};
        body.settings.position = description.position;
        body.settings.orientation = description.orientation.normalized();
        body.settings.isStatic = true;
        body.colliderType = apr::ColliderType::Heightfield;
        body.radius = 0.001;
        body.halfExtents = {1.0, 1.0, 1.0};
        body.halfHeight = 0.0;
        body.terrainDescription = description;
        body.terrainField = field;
        body.inverseInertia = {};
        bodies.push_back(std::move(body));
        indexById[bodies.back().id] = bodies.size() - 1;
        return BodyHandle{bodies.back().id};
    }

    std::optional<apr::Contact> detectContact(const apr::Body& first, const apr::Body& second) const noexcept {
        // Heightfield contacts: heightfield functions return the normal that
        // pushes the dynamic body away from the surface (roughly up). The
        // resolveContact convention is "normal points from first to second",
        // so the normal is flipped when the heightfield is the second operand.
        if (first.colliderType == apr::ColliderType::Heightfield ||
            second.colliderType == apr::ColliderType::Heightfield) {
            const apr::Body& dynamicBody = first.colliderType == apr::ColliderType::Heightfield ? second : first;
            const apr::Body& field = first.colliderType == apr::ColliderType::Heightfield ? first : second;
            if (dynamicBody.colliderType == apr::ColliderType::Heightfield) {
                return std::nullopt;
            }

            std::optional<apr::Contact> contact;
            switch (dynamicBody.colliderType) {
            case apr::ColliderType::Sphere:
                contact = apr::sphereHeightfield(dynamicBody, field);
                break;
            case apr::ColliderType::Box:
                contact = apr::boxHeightfield(dynamicBody, field);
                break;
            case apr::ColliderType::Capsule:
                contact = apr::capsuleHeightfield(dynamicBody, field);
                break;
            default:
                return std::nullopt;
            }
            if (contact.has_value() && second.colliderType == apr::ColliderType::Heightfield) {
                contact->normal = -contact->normal;
            }
            return contact;
        }

        if (first.colliderType == apr::ColliderType::Sphere) {
            if (second.colliderType == apr::ColliderType::Sphere) {
                return apr::sphereSphere(first, second);
            }
            if (second.colliderType == apr::ColliderType::Box) {
                std::optional<apr::Contact> contact = apr::sphereBox(first, second);
                if (contact.has_value()) {
                    contact->normal = -contact->normal;
                }
                return contact;
            }
            std::optional<apr::Contact> contact = apr::sphereCapsule(first, second);
            if (contact.has_value()) {
                contact->normal = -contact->normal;
            }
            return contact;
        }

        if (first.colliderType == apr::ColliderType::Box) {
            if (second.colliderType == apr::ColliderType::Box) {
                return apr::boxBox(first, second);
            }
            if (second.colliderType == apr::ColliderType::Sphere) {
                return apr::sphereBox(second, first);
            }
            return apr::boxCapsule(first, second);
        }

        if (second.colliderType == apr::ColliderType::Sphere) {
            return apr::sphereCapsule(second, first);
        }
        if (second.colliderType == apr::ColliderType::Box) {
            std::optional<apr::Contact> contact = apr::boxCapsule(second, first);
            if (contact.has_value()) {
                contact->normal = -contact->normal;
            }
            return contact;
        }
        return apr::capsuleCapsule(first, second);
    }

    // Velocity-only contact solve for one iteration. `normalTarget` is the
    // desired separating velocity (restitution bias); accumulated impulses are
    // passed in/out so clamping is stable across iterations.
    void solveVelocity(apr::Body& first, apr::Body& second, const apr::Contact& contact,
                       double normalTarget, double& normalImpulse, double& frictionImpulse) noexcept {
        const double firstInverseMass = inverseMass(first);
        const double secondInverseMass = inverseMass(second);
        const double inverseMassSum = firstInverseMass + secondInverseMass;
        if (inverseMassSum <= 0.0) {
            return;
        }

        const Vec3 firstLever = contact.point - first.settings.position;
        const Vec3 secondLever = contact.point - second.settings.position;
        const auto effectiveMass = [&](const Vec3& direction) noexcept {
            const Vec3 firstRotation = cross(firstLever, direction);
            const Vec3 secondRotation = cross(secondLever, direction);
            const Vec3 firstResponse = cross(applyInverseInertia(first, firstRotation), firstLever);
            const Vec3 secondResponse = cross(applyInverseInertia(second, secondRotation), secondLever);
            return inverseMassSum + dot(direction, firstResponse) + dot(direction, secondResponse);
        };
        const auto applyImpulse = [&](const Vec3& impulse) noexcept {
            first.velocity -= impulse * firstInverseMass;
            second.velocity += impulse * secondInverseMass;
            first.angularVelocity -= applyInverseInertia(first, cross(firstLever, impulse));
            second.angularVelocity += applyInverseInertia(second, cross(secondLever, impulse));
        };

        const Vec3 firstContactVelocity = first.velocity + cross(first.angularVelocity, firstLever);
        const Vec3 secondContactVelocity = second.velocity + cross(second.angularVelocity, secondLever);
        const Vec3 relativeVelocity = secondContactVelocity - firstContactVelocity;
        const double normalVelocity = dot(relativeVelocity, contact.normal);
        const double normalEffectiveMass = effectiveMass(contact.normal);
        if (normalEffectiveMass <= 0.0) {
            return;
        }

        double normalDelta = (normalTarget - normalVelocity) / normalEffectiveMass;
        const double newNormal = std::max(normalImpulse + normalDelta, 0.0);
        normalDelta = newNormal - normalImpulse;
        normalImpulse = newNormal;
        if (normalDelta != 0.0) {
            applyImpulse(contact.normal * normalDelta);
        }

        const Vec3 tangentVelocity = relativeVelocity - contact.normal * normalVelocity;
        const double tangentLength = tangentVelocity.length();
        if (tangentLength > std::numeric_limits<double>::epsilon() && normalImpulse > 0.0) {
            const Vec3 tangent = tangentVelocity / tangentLength;
            const double frictionCoefficient = std::sqrt(
                std::max(0.0, first.settings.friction) * std::max(0.0, second.settings.friction));
            const double maximumFriction = normalImpulse * frictionCoefficient;
            const double tangentEffectiveMass = effectiveMass(tangent);
            if (tangentEffectiveMass > 0.0) {
                double tangentDelta = -dot(relativeVelocity, tangent) / tangentEffectiveMass;
                const double newFriction = std::clamp(
                    frictionImpulse + tangentDelta, -maximumFriction, maximumFriction);
                tangentDelta = newFriction - frictionImpulse;
                frictionImpulse = newFriction;
                applyImpulse(tangent * tangentDelta);
            }
        }
    }

    // Positional correction runs once per step (not per iteration) with slop so
    // resting bodies do not get pushed apart every iteration.
    void correctPositions() noexcept {
        for (const apr::ActiveContact& item : activeContacts) {
            apr::Body& first = bodies[item.firstIndex];
            apr::Body& second = bodies[item.secondIndex];
            const double firstInverseMass = inverseMass(first);
            const double secondInverseMass = inverseMass(second);
            const double inverseMassSum = firstInverseMass + secondInverseMass;
            if (inverseMassSum <= 0.0) {
                continue;
            }
            const double separation = item.contact.penetration - apr::kLinearSlop;
            if (separation <= 0.0) {
                continue;
            }
            const double magnitude = std::min(separation * apr::kPositionCorrection, apr::kMaxCorrection);
            const Vec3 correction = item.contact.normal * (magnitude / inverseMassSum);
            first.settings.position -= correction * firstInverseMass;
            second.settings.position += correction * secondInverseMass;
        }
    }

    void detectContacts(const apr::Body& first, const apr::Body& second,
                        std::vector<apr::Contact>& out) const noexcept {
        out.clear();
        if (first.colliderType == apr::ColliderType::Heightfield ||
            second.colliderType == apr::ColliderType::Heightfield) {
            const apr::Body& dynamicBody = first.colliderType == apr::ColliderType::Heightfield ? second : first;
            const apr::Body& field = first.colliderType == apr::ColliderType::Heightfield ? first : second;
            if (dynamicBody.colliderType == apr::ColliderType::Heightfield) {
                return;
            }

            if (dynamicBody.colliderType == apr::ColliderType::Box) {
                apr::boxHeightfieldManifold(dynamicBody, field, out);
            } else {
                std::optional<apr::Contact> single = detectContact(first, second);
                if (single.has_value()) {
                    out.push_back(*single);
                }
            }

            if (second.colliderType == apr::ColliderType::Heightfield) {
                for (apr::Contact& c : out) {
                    c.normal = -c.normal;
                }
            }
            return;
        }

        if (first.colliderType == apr::ColliderType::Box && second.colliderType == apr::ColliderType::Box) {
            apr::boxBoxManifold(first, second, out);
            return;
        }

        std::optional<apr::Contact> single = detectContact(first, second);
        if (single.has_value()) {
            out.push_back(*single);
        }
    }

    void resolveCollisions() noexcept {
        lastContactsList.clear();
        activeContacts.clear();

        const auto considerPair = [&](std::size_t firstIndex, std::size_t secondIndex) {
            apr::Body& first = bodies[firstIndex];
            apr::Body& second = bodies[secondIndex];
            if (inverseMass(first) <= 0.0 && inverseMass(second) <= 0.0) {
                return;
            }
            const Vec3 delta = second.settings.position - first.settings.position;
            const double reach = first.boundingRadius + second.boundingRadius;
            const bool hasHeightfield = first.colliderType == apr::ColliderType::Heightfield ||
                                        second.colliderType == apr::ColliderType::Heightfield;
            if (!hasHeightfield && delta.lengthSquared() > reach * reach) {
                return;
            }
            detectContacts(first, second, scratchContacts);
            for (const apr::Contact& c : scratchContacts) {
                apr::ActiveContact active;
                active.firstIndex = firstIndex;
                active.secondIndex = secondIndex;
                active.contact = c;
                activeContacts.push_back(active);
            }
        };

        // Uniform spatial-hash broad phase. Only nearby bodies reach the narrow
        // phase, so a pile of thousands costs far less than the old O(N^2) pair
        // loop. Heightfields are handled separately (one surface, many bodies).
        const double cellSize = 2.0;
        grid.clear();
        for (std::size_t index = 0; index < bodies.size(); ++index) {
            const apr::Body& body = bodies[index];
            if (body.colliderType == apr::ColliderType::Heightfield) {
                continue;
            }
            std::int32_t x0, y0, z0, x1, y1, z1;
            gridRange(body, cellSize, x0, y0, z0, x1, y1, z1);
            for (std::int32_t x = x0; x <= x1; ++x) {
                for (std::int32_t y = y0; y <= y1; ++y) {
                    for (std::int32_t z = z0; z <= z1; ++z) {
                        grid[packCell(x, y, z)].push_back(static_cast<std::uint32_t>(index));
                    }
                }
            }
        }

        pairStamp.assign(bodies.size(), 0);
        for (std::size_t firstIndex = 0; firstIndex < bodies.size(); ++firstIndex) {
            const apr::Body& first = bodies[firstIndex];
            if (first.colliderType == apr::ColliderType::Heightfield) {
                continue;
            }
            ++pairStampCounter;
            std::int32_t x0, y0, z0, x1, y1, z1;
            gridRange(first, cellSize, x0, y0, z0, x1, y1, z1);
            for (std::int32_t x = x0; x <= x1; ++x) {
                for (std::int32_t y = y0; y <= y1; ++y) {
                    for (std::int32_t z = z0; z <= z1; ++z) {
                        const auto cellIt = grid.find(packCell(x, y, z));
                        if (cellIt == grid.end()) {
                            continue;
                        }
                        for (std::uint32_t other : cellIt->second) {
                            if (other <= firstIndex || pairStamp[other] == pairStampCounter) {
                                continue;
                            }
                            pairStamp[other] = pairStampCounter;
                            considerPair(firstIndex, other);
                        }
                    }
                }
            }
        }

        for (std::size_t fieldIndex = 0; fieldIndex < bodies.size(); ++fieldIndex) {
            if (bodies[fieldIndex].colliderType != apr::ColliderType::Heightfield) {
                continue;
            }
            for (std::size_t bodyIndex = 0; bodyIndex < bodies.size(); ++bodyIndex) {
                if (bodies[bodyIndex].colliderType == apr::ColliderType::Heightfield ||
                    inverseMass(bodies[bodyIndex]) <= 0.0) {
                    continue;
                }
                considerPair(std::min(fieldIndex, bodyIndex), std::max(fieldIndex, bodyIndex));
            }
        }

        const std::uint32_t iterations = std::clamp(solverIterations, 1u, 64u);

        // Restitution bias is computed once from the pre-solve approach speed;
        // applying it every iteration would double-count.
        for (apr::ActiveContact& item : activeContacts) {
            apr::Body& first = bodies[item.firstIndex];
            apr::Body& second = bodies[item.secondIndex];
            const Vec3 firstLever = item.contact.point - first.settings.position;
            const Vec3 secondLever = item.contact.point - second.settings.position;
            const Vec3 firstContactVelocity = first.velocity + cross(first.angularVelocity, firstLever);
            const Vec3 secondContactVelocity = second.velocity + cross(second.angularVelocity, secondLever);
            const double approach = dot(secondContactVelocity - firstContactVelocity, item.contact.normal);
            double restitution = std::clamp(
                std::max(first.settings.restitution, second.settings.restitution), 0.0, 1.0);
            if (-approach < apr::kRestitutionThreshold) {
                restitution = 0.0; // resting contact, no bounce
            }
            item.normalTarget = approach < 0.0 ? -restitution * approach : 0.0;
        }

        for (std::uint32_t iter = 0; iter < iterations; ++iter) {
            for (apr::ActiveContact& item : activeContacts) {
                solveVelocity(bodies[item.firstIndex], bodies[item.secondIndex], item.contact,
                              item.normalTarget, item.normalImpulse, item.frictionImpulse);
            }
        }

        correctPositions();

        std::size_t bestIndex = 0;
        double bestImpulse = -1.0;
        for (std::size_t index = 0; index < activeContacts.size(); ++index) {
            const apr::ActiveContact& item = activeContacts[index];
            lastContactsList.push_back(ContactInfo{
                BodyHandle{bodies[item.firstIndex].id},
                BodyHandle{bodies[item.secondIndex].id},
                item.contact.normal,
                item.contact.penetration,
                item.normalImpulse,
            });
            if (item.normalImpulse > bestImpulse) {
                bestImpulse = item.normalImpulse;
                bestIndex = index;
            }
        }
        lastStepCollisionCount = activeContacts.size();
        if (!activeContacts.empty()) {
            const apr::ActiveContact& item = activeContacts[bestIndex];
            lastContact = ContactInfo{
                BodyHandle{bodies[item.firstIndex].id},
                BodyHandle{bodies[item.secondIndex].id},
                item.contact.normal,
                item.contact.penetration,
                item.normalImpulse,
            };
        }
    }
};

PhysicsWorld::PhysicsWorld(const Vec3& gravity)
    : impl_(std::make_unique<Impl>()) {
    setGravity(gravity);
}

PhysicsWorld::~PhysicsWorld() = default;
PhysicsWorld::PhysicsWorld(PhysicsWorld&&) noexcept = default;
PhysicsWorld& PhysicsWorld::operator=(PhysicsWorld&&) noexcept = default;

BodyHandle PhysicsWorld::createSphere(const SphereBodyConfig& config) {
    return impl_->add(
        apr::ColliderType::Sphere,
        config,
        config.radius,
        Vec3{},
        0.0);
}

BodyHandle PhysicsWorld::createBox(const BoxBodyConfig& config) {
    return impl_->add(
        apr::ColliderType::Box,
        config,
        0.0,
        config.halfExtents,
        0.0);
}

BodyHandle PhysicsWorld::createCapsule(const CapsuleBodyConfig& config) {
    return impl_->add(
        apr::ColliderType::Capsule,
        config,
        config.radius,
        Vec3{},
        config.halfHeight);
}

BodyHandle PhysicsWorld::createHeightfield(const TerrainDesc& description, const TerrainHeightfield& field) {
    return impl_->addHeightfield(description, field);
}

double PhysicsWorld::heightfieldHeightAt(const TerrainHeightfield& field, const Vec3& worldPoint) const noexcept {
    return terrainHeightAt(TerrainDesc{}, field, worldPoint);
}

double PhysicsWorld::heightfieldHeightAt(BodyHandle handle, const Vec3& worldPoint) const noexcept {
    const apr::Body* body = impl_->find(handle.value);
    if (body == nullptr || body->colliderType != apr::ColliderType::Heightfield) {
        return 0.0;
    }
    return terrainHeightAt(body->terrainDescription, body->terrainField, worldPoint);
}

void PhysicsWorld::setGravity(const Vec3& gravity) noexcept {
    if (apr::finite(gravity)) {
        impl_->gravity = gravity;
    }
}

Vec3 PhysicsWorld::gravity() const noexcept {
    return impl_->gravity;
}

void PhysicsWorld::applyForce(BodyHandle handle, const Vec3& force) noexcept {
    apr::Body* body = impl_->find(handle.value);
    if (body == nullptr || body->settings.isStatic || !apr::finite(force)) {
        return;
    }
    body->force += force;
}

void PhysicsWorld::applyTorque(BodyHandle handle, const Vec3& torque) noexcept {
    apr::Body* body = impl_->find(handle.value);
    if (body == nullptr || body->settings.isStatic || !apr::finite(torque)) {
        return;
    }
    body->torque += torque;
}

void PhysicsWorld::setBodySteering(BodyHandle handle, double steerInput, const SteeringConfig& config) noexcept {
    apr::Body* body = impl_->find(handle.value);
    if (body == nullptr || body->settings.isStatic || !apr::finite(steerInput)) {
        return;
    }
    body->steering = config;
    body->steerInput = std::clamp(steerInput, -1.0, 1.0);
    body->steeringEnabled = true;
}

void PhysicsWorld::disableBodySteering(BodyHandle handle) noexcept {
    apr::Body* body = impl_->find(handle.value);
    if (body != nullptr) {
        body->steeringEnabled = false;
        body->steerInput = 0.0;
        body->steerAngle = 0.0;
    }
}

void PhysicsWorld::setBodySteeringConfig(BodyHandle handle, const SteeringConfig& config) noexcept {
    apr::Body* body = impl_->find(handle.value);
    if (body != nullptr) {
        body->steering = config;
    }
}

double PhysicsWorld::bodySteeringAngle(BodyHandle handle) const noexcept {
    const apr::Body* body = impl_->find(handle.value);
    return body == nullptr ? 0.0 : body->steerAngle;
}

double PhysicsWorld::bodyYawRate(BodyHandle handle) const noexcept {
    const apr::Body* body = impl_->find(handle.value);
    return body == nullptr ? 0.0 : body->lastYawRate;
}

void PhysicsWorld::setBodyPosition(BodyHandle handle, const Vec3& position) noexcept {
    apr::Body* body = impl_->find(handle.value);
    if (body != nullptr && apr::finite(position)) {
        body->settings.position = position;
    }
}

void PhysicsWorld::setBodyVelocity(BodyHandle handle, const Vec3& velocity) noexcept {
    apr::Body* body = impl_->find(handle.value);
    if (body != nullptr && !body->settings.isStatic && apr::finite(velocity)) {
        body->velocity = velocity;
    }
}

void PhysicsWorld::setBodyOrientation(BodyHandle handle, const Quat& orientation) noexcept {
    apr::Body* body = impl_->find(handle.value);
    if (body == nullptr || !apr::finite(orientation) || orientation.lengthSquared() <= std::numeric_limits<double>::epsilon()) {
        return;
    }
    body->settings.orientation = orientation.normalized();
}

void PhysicsWorld::setBodyAngularVelocity(BodyHandle handle, const Vec3& angularVelocity) noexcept {
    apr::Body* body = impl_->find(handle.value);
    if (body != nullptr && !body->settings.isStatic && apr::finite(angularVelocity)) {
        body->angularVelocity = angularVelocity;
    }
}

void PhysicsWorld::setBodyFriction(BodyHandle handle, double friction) noexcept {
    apr::Body* body = impl_->find(handle.value);
    if (body != nullptr && apr::finite(friction)) {
        body->settings.friction = std::max(0.0, friction);
    }
}

void PhysicsWorld::setBodyRestitution(BodyHandle handle, double restitution) noexcept {
    apr::Body* body = impl_->find(handle.value);
    if (body != nullptr && apr::finite(restitution)) {
        body->settings.restitution = std::clamp(restitution, 0.0, 1.0);
    }
}

Vec3 PhysicsWorld::bodyPosition(BodyHandle handle) const noexcept {
    const apr::Body* body = impl_->find(handle.value);
    return body == nullptr ? Vec3{} : body->settings.position;
}

Vec3 PhysicsWorld::bodyVelocity(BodyHandle handle) const noexcept {
    const apr::Body* body = impl_->find(handle.value);
    return body == nullptr ? Vec3{} : body->velocity;
}

Quat PhysicsWorld::bodyOrientation(BodyHandle handle) const noexcept {
    const apr::Body* body = impl_->find(handle.value);
    return body == nullptr ? Quat::identity() : body->settings.orientation;
}

Vec3 PhysicsWorld::bodyAngularVelocity(BodyHandle handle) const noexcept {
    const apr::Body* body = impl_->find(handle.value);
    return body == nullptr ? Vec3{} : body->angularVelocity;
}

double PhysicsWorld::bodyMass(BodyHandle handle) const noexcept {
    const apr::Body* body = impl_->find(handle.value);
    if (body == nullptr || body->settings.isStatic) {
        return 0.0;
    }
    return body->settings.mass;
}

std::size_t PhysicsWorld::bodyCount() const noexcept {
    return impl_->bodies.size();
}

void PhysicsWorld::step(double deltaSeconds) noexcept {
    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0) {
        return;
    }

    impl_->lastStepCollisionCount = 0;
    impl_->lastContact = {};

    // Fixed-timestep accumulator: timestepHz is the real simulation rate, and
    // a variable frame delta is consumed in whole substeps (clamped so a long
    // stall cannot spiral).
    const double fixed = 1.0 / impl_->timestepHz;
    impl_->accumulator += deltaSeconds;
    std::uint32_t steps = 0;
    while (impl_->accumulator >= fixed && steps < impl_->maxSubsteps) {
        impl_->integrateSubstep(fixed);
        impl_->accumulator -= fixed;
        ++steps;
    }
    if (steps == impl_->maxSubsteps && impl_->accumulator >= fixed) {
        impl_->accumulator = 0.0; // drop the backlog instead of spiralling
    }
    impl_->lastSubstepCount = steps;
}

std::size_t PhysicsWorld::lastStepCollisionCount() const noexcept {
    return impl_->lastStepCollisionCount;
}

void PhysicsWorld::setTimestepHz(double hz) noexcept {
    if (std::isfinite(hz) && hz > 0.0) {
        impl_->timestepHz = hz;
    }
}

double PhysicsWorld::timestepHz() const noexcept {
    return impl_->timestepHz;
}

void PhysicsWorld::setSolverIterations(std::uint32_t iterations) noexcept {
    impl_->solverIterations = std::clamp(iterations, 1u, 64u);
}

std::uint32_t PhysicsWorld::solverIterations() const noexcept {
    return impl_->solverIterations;
}

RaycastHit PhysicsWorld::raycast(const Ray& ray, double maxDistance) const noexcept {
    RaycastHit closestHit;
    closestHit.distance = maxDistance;

    if (!ray.valid() || maxDistance <= 0.0) {
        return closestHit;
    }

    const Ray normalizedRay{ray.origin, ray.direction.normalized()};

    for (const apr::Body& body : impl_->bodies) {
        apr::RaycastInternalHit hit;
        switch (body.colliderType) {
        case apr::ColliderType::Sphere:
            hit = apr::raycastSphere(normalizedRay, body);
            break;
        case apr::ColliderType::Box:
            hit = apr::raycastBox(normalizedRay, body);
            break;
        case apr::ColliderType::Capsule:
            hit = apr::raycastCapsule(normalizedRay, body);
            break;
        case apr::ColliderType::Heightfield:
            hit = apr::raycastHeightfield(normalizedRay, body, maxDistance);
            break;
        }

        if (hit.hasHit && hit.distance >= 0.0 && hit.distance <= closestHit.distance) {
            closestHit.body = BodyHandle{body.id};
            closestHit.point = hit.point;
            closestHit.normal = hit.normal;
            closestHit.distance = hit.distance;
            closestHit.fraction = hit.distance / maxDistance;
            closestHit.hasHit = true;
        }
    }

    return closestHit;
}

std::vector<RaycastHit> PhysicsWorld::raycastAll(const Ray& ray, double maxDistance) const {
    std::vector<RaycastHit> hits;
    if (!ray.valid() || maxDistance <= 0.0) {
        return hits;
    }

    const Ray normalizedRay{ray.origin, ray.direction.normalized()};

    for (const apr::Body& body : impl_->bodies) {
        apr::RaycastInternalHit hit;
        switch (body.colliderType) {
        case apr::ColliderType::Sphere:
            hit = apr::raycastSphere(normalizedRay, body);
            break;
        case apr::ColliderType::Box:
            hit = apr::raycastBox(normalizedRay, body);
            break;
        case apr::ColliderType::Capsule:
            hit = apr::raycastCapsule(normalizedRay, body);
            break;
        case apr::ColliderType::Heightfield:
            hit = apr::raycastHeightfield(normalizedRay, body, maxDistance);
            break;
        }

        if (hit.hasHit && hit.distance >= 0.0 && hit.distance <= maxDistance) {
            RaycastHit outHit;
            outHit.body = BodyHandle{body.id};
            outHit.point = hit.point;
            outHit.normal = hit.normal;
            outHit.distance = hit.distance;
            outHit.fraction = hit.distance / maxDistance;
            outHit.hasHit = true;
            hits.push_back(outHit);
        }
    }

    std::sort(hits.begin(), hits.end(), [](const RaycastHit& a, const RaycastHit& b) {
        return a.distance < b.distance;
    });

    return hits;
}

ContactInfo PhysicsWorld::lastContact() const noexcept {
    return impl_->lastContact;
}

std::vector<ContactInfo> PhysicsWorld::lastContacts() const {
    return impl_->lastContactsList;
}

}
