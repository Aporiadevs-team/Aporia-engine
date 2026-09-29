#include "Aporia/Physics/PhysicsWorld.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace Aporia {
namespace apr {

enum class ColliderType {
    Sphere,
    Box,
    Capsule,
    Heightfield,
};

struct Body {
    std::uint64_t id;
    BodySettings settings;
    ColliderType colliderType;
    double radius;
    Vec3 halfExtents;
    double halfHeight;
    Vec3 velocity;
    Vec3 angularVelocity;
    Vec3 force;
    Vec3 torque;
    Vec3 inverseInertia;
    // Heightfield payload (valid when colliderType == Heightfield).
    TerrainDesc terrainDescription;
    TerrainHeightfield terrainField;
};

struct Contact {
    Vec3 normal;
    Vec3 point;
    double penetration;

    Contact(const Vec3& normalValue, double penetrationValue) noexcept
        : normal(normalValue), point{}, penetration(penetrationValue) {
    }

    Contact(const Vec3& normalValue, const Vec3& pointValue, double penetrationValue) noexcept
        : normal(normalValue), point(pointValue), penetration(penetrationValue) {
    }
};

struct Bounds {
    Vec3 minimum;
    Vec3 maximum;
};

bool finite(const Vec3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool finite(double value) noexcept {
    return std::isfinite(value);
}

bool finite(const Quat& value) noexcept {
    return value.finite();
}

Vec3 absolute(const Vec3& value) noexcept {
    return {std::abs(value.x), std::abs(value.y), std::abs(value.z)};
}

Vec3 clamp(const Vec3& value, const Vec3& minimum, const Vec3& maximum) noexcept {
    return {
        std::clamp(value.x, minimum.x, maximum.x),
        std::clamp(value.y, minimum.y, maximum.y),
        std::clamp(value.z, minimum.z, maximum.z),
    };
}

struct Segment {
    Vec3 start;
    Vec3 end;
};

struct ClosestSegmentPoints {
    Vec3 first;
    Vec3 second;
};

Bounds localBounds(const Body& body) noexcept {
    return {-body.halfExtents, body.halfExtents};
}

Vec3 localToWorld(const Body& body, const Vec3& point) noexcept {
    return body.settings.position + body.settings.orientation.rotate(point);
}

Vec3 worldToLocal(const Body& body, const Vec3& point) noexcept {
    return body.settings.orientation.inverseRotate(point - body.settings.position);
}

std::array<Vec3, 3> boxAxes(const Body& body) noexcept {
    return {
        body.settings.orientation.rotate({1.0, 0.0, 0.0}),
        body.settings.orientation.rotate({0.0, 1.0, 0.0}),
        body.settings.orientation.rotate({0.0, 0.0, 1.0}),
    };
}

Segment capsuleSegment(const Body& body) noexcept {
    return {
        localToWorld(body, {0.0, 0.0, -body.halfHeight}),
        localToWorld(body, {0.0, 0.0, body.halfHeight}),
    };
}

Vec3 closestPointOnBox(const Vec3& point, const Bounds& box) noexcept {
    return clamp(point, box.minimum, box.maximum);
}

Vec3 closestPointOnSegment(const Vec3& point, const Segment& segment) noexcept {
    const Vec3 direction = segment.end - segment.start;
    const double lengthSquared = direction.lengthSquared();
    if (lengthSquared <= std::numeric_limits<double>::epsilon()) {
        return segment.start;
    }

    const double parameter = std::clamp(dot(point - segment.start, direction) / lengthSquared, 0.0, 1.0);
    return segment.start + direction * parameter;
}

ClosestSegmentPoints closestPointsOnSegments(const Segment& first, const Segment& second) noexcept {
    const Vec3 firstDirection = first.end - first.start;
    const Vec3 secondDirection = second.end - second.start;
    const Vec3 offset = first.start - second.start;
    const double firstLengthSquared = firstDirection.lengthSquared();
    const double secondLengthSquared = secondDirection.lengthSquared();
    const double firstProjection = dot(firstDirection, secondDirection);
    const double firstOffsetProjection = dot(firstDirection, offset);
    const double secondOffsetProjection = dot(secondDirection, offset);

    double firstParameter = 0.0;
    double secondParameter = 0.0;
    const double denominator = firstLengthSquared * secondLengthSquared - firstProjection * firstProjection;
    if (firstLengthSquared <= std::numeric_limits<double>::epsilon()) {
        secondParameter = secondLengthSquared > std::numeric_limits<double>::epsilon()
            ? std::clamp(secondOffsetProjection / secondLengthSquared, 0.0, 1.0)
            : 0.0;
    } else if (secondLengthSquared <= std::numeric_limits<double>::epsilon()) {
        firstParameter = std::clamp(-firstOffsetProjection / firstLengthSquared, 0.0, 1.0);
    } else if (denominator > std::numeric_limits<double>::epsilon()) {
        firstParameter = std::clamp(
            (firstProjection * secondOffsetProjection - secondLengthSquared * firstOffsetProjection) / denominator,
            0.0,
            1.0);
        secondParameter = std::clamp(
            (firstProjection * firstParameter + secondOffsetProjection) / secondLengthSquared,
            0.0,
            1.0);
        firstParameter = std::clamp(
            (firstProjection * secondParameter - firstOffsetProjection) / firstLengthSquared,
            0.0,
            1.0);
    } else {
        firstParameter = std::clamp(-firstOffsetProjection / firstLengthSquared, 0.0, 1.0);
        secondParameter = std::clamp(
            (firstProjection * firstParameter + secondOffsetProjection) / secondLengthSquared,
            0.0,
            1.0);
        firstParameter = std::clamp(
            (firstProjection * secondParameter - firstOffsetProjection) / firstLengthSquared,
            0.0,
            1.0);
    }

    return {
        first.start + firstDirection * firstParameter,
        second.start + secondDirection * secondParameter,
    };
}

std::optional<BodySettings> normalizeSettings(const BodySettings& settings) noexcept {
    if (!finite(settings.position) || !finite(settings.linearVelocity) ||
        !finite(settings.orientation) || !finite(settings.angularVelocity)) {
        return std::nullopt;
    }

    const double orientationLengthSquared = settings.orientation.lengthSquared();
    if (!std::isfinite(orientationLengthSquared) || orientationLengthSquared <= std::numeric_limits<double>::epsilon()) {
        return std::nullopt;
    }

    BodySettings result = settings;
    result.orientation = result.orientation.normalized();
    if (!finite(result.mass) || result.mass <= 0.0) {
        result.mass = 1.0;
    }

    if (!finite(result.restitution)) {
        result.restitution = 0.0;
    }

    if (!finite(result.friction)) {
        result.friction = 0.0;
    }

    result.restitution = std::clamp(result.restitution, 0.0, 1.0);
    result.friction = std::max(0.0, result.friction);
    return result;
}

Vec3 inverseInertiaFor(const Body& body) noexcept {
    if (body.settings.isStatic) {
        return {};
    }

    const double mass = body.settings.mass;
    if (body.colliderType == apr::ColliderType::Box) {
        const double x = mass * (body.halfExtents.y * body.halfExtents.y + body.halfExtents.z * body.halfExtents.z) / 3.0;
        const double y = mass * (body.halfExtents.x * body.halfExtents.x + body.halfExtents.z * body.halfExtents.z) / 3.0;
        const double z = mass * (body.halfExtents.x * body.halfExtents.x + body.halfExtents.y * body.halfExtents.y) / 3.0;
        return {x > 0.0 ? 1.0 / x : 0.0, y > 0.0 ? 1.0 / y : 0.0, z > 0.0 ? 1.0 / z : 0.0};
    }

    const double radius = body.colliderType == apr::ColliderType::Sphere
        ? body.radius
        : body.radius * body.radius + body.halfHeight * body.halfHeight / 3.0;
    const double inertia = body.colliderType == apr::ColliderType::Sphere
        ? 0.4 * mass * radius * radius
        : mass * radius;
    const double inverse = inertia > 0.0 ? 1.0 / inertia : 0.0;
    return {inverse, inverse, inverse};
}

std::optional<Contact> sphereSphere(const Body& first, const Body& second) noexcept {
    const Vec3 separation = second.settings.position - first.settings.position;
    const double radiusSum = first.radius + second.radius;
    const double distanceSquared = separation.lengthSquared();

    if (distanceSquared >= radiusSum * radiusSum) {
        return std::nullopt;
    }

    const double distance = std::sqrt(distanceSquared);
    const Vec3 normal = distance > std::numeric_limits<double>::epsilon()
        ? separation / distance
        : Vec3{1.0, 0.0, 0.0};
    const Vec3 point = first.settings.position + normal * first.radius;
    return Contact{normal, point, radiusSum - distance};
}

std::optional<Contact> sphereBox(const Body& sphere, const Body& box) noexcept {
    const Bounds boxBounds = localBounds(box);
    const Vec3 localSphere = worldToLocal(box, sphere.settings.position);
    const Vec3 localClosest = closestPointOnBox(localSphere, boxBounds);
    const Vec3 localSeparation = localSphere - localClosest;
    const double distanceSquared = localSeparation.lengthSquared();

    if (distanceSquared > std::numeric_limits<double>::epsilon()) {
        const double distance = std::sqrt(distanceSquared);
        if (distance >= sphere.radius) {
            return std::nullopt;
        }

        const Vec3 normal = box.settings.orientation.rotate(localSeparation / distance);
        const Vec3 point = localToWorld(box, localClosest);
        return Contact{normal, point, sphere.radius - distance};
    }

    const double positiveX = boxBounds.maximum.x - localSphere.x;
    const double negativeX = localSphere.x - boxBounds.minimum.x;
    const double positiveY = boxBounds.maximum.y - localSphere.y;
    const double negativeY = localSphere.y - boxBounds.minimum.y;
    const double positiveZ = boxBounds.maximum.z - localSphere.z;
    const double negativeZ = localSphere.z - boxBounds.minimum.z;

    double distanceToFace = positiveX;
    Vec3 localNormal{1.0, 0.0, 0.0};
    if (negativeX < distanceToFace) {
        distanceToFace = negativeX;
        localNormal = {-1.0, 0.0, 0.0};
    }
    if (positiveY < distanceToFace) {
        distanceToFace = positiveY;
        localNormal = {0.0, 1.0, 0.0};
    }
    if (negativeY < distanceToFace) {
        distanceToFace = negativeY;
        localNormal = {0.0, -1.0, 0.0};
    }
    if (positiveZ < distanceToFace) {
        distanceToFace = positiveZ;
        localNormal = {0.0, 0.0, 1.0};
    }
    if (negativeZ < distanceToFace) {
        distanceToFace = negativeZ;
        localNormal = {0.0, 0.0, -1.0};
    }

    Vec3 localPoint = localSphere;
    if (localNormal.x > 0.5) {
        localPoint.x = boxBounds.maximum.x;
    } else if (localNormal.x < -0.5) {
        localPoint.x = boxBounds.minimum.x;
    } else if (localNormal.y > 0.5) {
        localPoint.y = boxBounds.maximum.y;
    } else if (localNormal.y < -0.5) {
        localPoint.y = boxBounds.minimum.y;
    } else if (localNormal.z > 0.5) {
        localPoint.z = boxBounds.maximum.z;
    } else {
        localPoint.z = boxBounds.minimum.z;
    }

    return Contact{
        box.settings.orientation.rotate(localNormal),
        localToWorld(box, localPoint),
        sphere.radius + distanceToFace,
    };
}

std::optional<Contact> sphereCapsule(const Body& sphere, const Body& capsule) noexcept {
    const Segment segment = capsuleSegment(capsule);
    const Vec3 closest = closestPointOnSegment(sphere.settings.position, segment);
    const Vec3 separation = sphere.settings.position - closest;
    const double radiusSum = sphere.radius + capsule.radius;
    const double distanceSquared = separation.lengthSquared();

    if (distanceSquared >= radiusSum * radiusSum) {
        return std::nullopt;
    }

    const double distance = std::sqrt(distanceSquared);
    const Vec3 normal = distance > std::numeric_limits<double>::epsilon()
        ? separation / distance
        : Vec3{1.0, 0.0, 0.0};
    return Contact{normal, closest, radiusSum - distance};
}

double projectedRadius(const Body& body, const std::array<Vec3, 3>& axes, const Vec3& axis) noexcept {
    return body.halfExtents.x * std::abs(dot(axes[0], axis)) +
           body.halfExtents.y * std::abs(dot(axes[1], axis)) +
           body.halfExtents.z * std::abs(dot(axes[2], axis));
}

std::optional<Contact> boxBox(const Body& first, const Body& second) noexcept {
    const std::array<Vec3, 3> firstAxes = boxAxes(first);
    const std::array<Vec3, 3> secondAxes = boxAxes(second);
    const Vec3 centerSeparation = second.settings.position - first.settings.position;
    const Vec3 contactPoint = (first.settings.position + second.settings.position) * 0.5;
    std::optional<Contact> bestContact;
    double smallestOverlap = std::numeric_limits<double>::infinity();

    const auto testAxis = [&](const Vec3& candidate) noexcept {
        const double lengthSquared = candidate.lengthSquared();
        if (lengthSquared <= std::numeric_limits<double>::epsilon()) {
            return;
        }

        const Vec3 axis = candidate / std::sqrt(lengthSquared);
        const double projectedDistance = dot(centerSeparation, axis);
        const double overlap = projectedRadius(first, firstAxes, axis) +
                               projectedRadius(second, secondAxes, axis) - std::abs(projectedDistance);
        if (overlap <= 0.0 || overlap >= smallestOverlap) {
            return;
        }

        const double sign = projectedDistance >= 0.0 ? 1.0 : -1.0;
        smallestOverlap = overlap;
        bestContact = Contact{axis * sign, contactPoint, overlap};
    };

    for (const Vec3& axis : firstAxes) {
        testAxis(axis);
    }
    for (const Vec3& axis : secondAxes) {
        testAxis(axis);
    }
    for (const Vec3& firstAxis : firstAxes) {
        for (const Vec3& secondAxis : secondAxes) {
            testAxis(cross(firstAxis, secondAxis));
        }
    }

    return bestContact;
}

std::optional<Contact> capsuleCapsule(const Body& first, const Body& second) noexcept {
    const ClosestSegmentPoints closest = closestPointsOnSegments(capsuleSegment(first), capsuleSegment(second));
    const Vec3 separation = closest.second - closest.first;
    const double radiusSum = first.radius + second.radius;
    const double distanceSquared = separation.lengthSquared();
    if (distanceSquared >= radiusSum * radiusSum) {
        return std::nullopt;
    }

    const double distance = std::sqrt(distanceSquared);
    const Vec3 normal = distance > std::numeric_limits<double>::epsilon()
        ? separation / distance
        : Vec3{1.0, 0.0, 0.0};
    return Contact{normal, (closest.first + closest.second) * 0.5, radiusSum - distance};
}

std::optional<Contact> boxCapsule(const Body& box, const Body& capsule) noexcept {
    const Bounds boxBounds = localBounds(box);
    const Segment worldSegment = capsuleSegment(capsule);
    const Vec3 start = worldToLocal(box, worldSegment.start);
    const Vec3 end = worldToLocal(box, worldSegment.end);
    const Vec3 direction = end - start;

    const auto distanceSquaredAt = [&](double parameter) noexcept {
        const Vec3 point = start + direction * parameter;
        return (point - closestPointOnBox(point, boxBounds)).lengthSquared();
    };

    double lower = 0.0;
    double upper = 1.0;
    for (std::size_t iteration = 0; iteration < 32; ++iteration) {
        const double firstParameter = lower + (upper - lower) / 3.0;
        const double secondParameter = upper - (upper - lower) / 3.0;
        if (distanceSquaredAt(firstParameter) < distanceSquaredAt(secondParameter)) {
            upper = secondParameter;
        } else {
            lower = firstParameter;
        }
    }

    const double parameter = (lower + upper) * 0.5;
    const Vec3 localPoint = start + direction * parameter;
    const Vec3 localClosest = closestPointOnBox(localPoint, boxBounds);
    const Vec3 localSeparation = localPoint - localClosest;
    const double distanceSquared = localSeparation.lengthSquared();
    if (distanceSquared >= capsule.radius * capsule.radius) {
        return std::nullopt;
    }

    const double distance = std::sqrt(distanceSquared);
    if (distance > std::numeric_limits<double>::epsilon()) {
        return Contact{
            box.settings.orientation.rotate(localSeparation / distance),
            localToWorld(box, localClosest),
            capsule.radius - distance,
        };
    }

    const double positiveX = boxBounds.maximum.x - localPoint.x;
    const double negativeX = localPoint.x - boxBounds.minimum.x;
    const double positiveY = boxBounds.maximum.y - localPoint.y;
    const double negativeY = localPoint.y - boxBounds.minimum.y;
    const double positiveZ = boxBounds.maximum.z - localPoint.z;
    const double negativeZ = localPoint.z - boxBounds.minimum.z;

    double distanceToFace = positiveX;
    Vec3 localNormal{1.0, 0.0, 0.0};
    if (negativeX < distanceToFace) {
        distanceToFace = negativeX;
        localNormal = {-1.0, 0.0, 0.0};
    }
    if (positiveY < distanceToFace) {
        distanceToFace = positiveY;
        localNormal = {0.0, 1.0, 0.0};
    }
    if (negativeY < distanceToFace) {
        distanceToFace = negativeY;
        localNormal = {0.0, -1.0, 0.0};
    }
    if (positiveZ < distanceToFace) {
        distanceToFace = positiveZ;
        localNormal = {0.0, 0.0, 1.0};
    }
    if (negativeZ < distanceToFace) {
        distanceToFace = negativeZ;
        localNormal = {0.0, 0.0, -1.0};
    }

    Vec3 localContact = localPoint;
    if (localNormal.x > 0.5) {
        localContact.x = boxBounds.maximum.x;
    } else if (localNormal.x < -0.5) {
        localContact.x = boxBounds.minimum.x;
    } else if (localNormal.y > 0.5) {
        localContact.y = boxBounds.maximum.y;
    } else if (localNormal.y < -0.5) {
        localContact.y = boxBounds.minimum.y;
    } else if (localNormal.z > 0.5) {
        localContact.z = boxBounds.maximum.z;
    } else {
        localContact.z = boxBounds.minimum.z;
    }

    return Contact{
        box.settings.orientation.rotate(localNormal),
        localToWorld(box, localContact),
        capsule.radius + distanceToFace,
    };
}

// --- Heightfield contacts -------------------------------------------------

// Terrain surface point and world-space normal under a planar sample.
struct HeightSample {
    Vec3 point;
    Vec3 normal;
    double elevation;
};

HeightSample sampleHeightfield(const Body& field, const Vec3& planarWorldPoint) noexcept {
    const Quat orientation = field.terrainDescription.orientation.normalized();
    const Vec3 local = orientation.inverseRotate(planarWorldPoint - field.terrainDescription.position);

    // Elevation from bilinear heights; normal from central-difference gradient.
    const TerrainHeightfield& hf = field.terrainField;
    const std::uint32_t resolution = hf.resolution;
    const double cell = static_cast<double>(hf.worldSize) / static_cast<double>(resolution - 1);
    const double gx = (local.x + hf.worldSize * 0.5) / cell;
    const double gy = (local.y + hf.worldSize * 0.5) / cell;
    const std::int64_t ix = static_cast<std::int64_t>(std::floor(gx));
    const std::int64_t iy = static_cast<std::int64_t>(std::floor(gy));
    const double tx = gx - std::floor(gx);
    const double ty = gy - std::floor(gy);

    const auto at = [&](std::int64_t x, std::int64_t y) noexcept {
        const std::int64_t cx = std::clamp<std::int64_t>(x, 0, static_cast<std::int64_t>(resolution) - 1);
        const std::int64_t cy = std::clamp<std::int64_t>(y, 0, static_cast<std::int64_t>(resolution) - 1);
        return static_cast<double>(hf.heights[static_cast<std::size_t>(cy) * resolution + cx]);
    };

    const double h00 = at(ix, iy);
    const double h10 = at(ix + 1, iy);
    const double h01 = at(ix, iy + 1);
    const double h11 = at(ix + 1, iy + 1);
    const double a = h00 + (h10 - h00) * tx;
    const double b = h01 + (h11 - h01) * tx;
    const double elevation = a + (b - a) * ty;

    const double gradientX = (h10 - h00) * (1.0 - ty) + (h11 - h01) * ty;
    const double gradientY = (h01 - h00) * (1.0 - tx) + (h11 - h10) * tx;
    const Vec3 localNormal = Vec3{-gradientX / cell, -gradientY / cell, 1.0}.normalized();

    const Vec3 localPoint{local.x, local.y, elevation};
    return {
        orientation.rotate(localPoint) + field.terrainDescription.position,
        orientation.rotate(localNormal),
        elevation,
    };
}

std::optional<Contact> sphereHeightfield(const Body& sphere, const Body& field) noexcept {
    const HeightSample sample = sampleHeightfield(field, sphere.settings.position);
    const Vec3 separation = sphere.settings.position - sample.point;
    const double distance = separation.length();

    if (distance >= sphere.radius) {
        return std::nullopt;
    }

    if (distance > std::numeric_limits<double>::epsilon()) {
        // Center above the surface: standard deepest-point contact.
        const Vec3 normal = separation / distance;
        return Contact{normal, sample.point, sphere.radius - distance};
    }

    // Center at/below the surface: push up along the terrain normal.
    return Contact{sample.normal, sample.point, sphere.radius};
}

std::optional<Contact> boxHeightfield(const Body& box, const Body& field) noexcept {
    // Sample a 3x3 grid on the box's downward face (in world terms) plus the
    // center; any sample point pushed under the surface produces a contact.
    // Face sampling handles slopes and edges far better than corner tests.
    std::optional<Contact> best;
    double deepest = 0.0;

    const std::array<Vec3, 3> axes = boxAxes(box);
    // Face sampling needs a "down" face: pick the local axis most aligned
    // with world -Z (terrain height axis is world Z regardless of placement).
    std::size_t downAxis = 0;
    double bestAlignment = -2.0;
    for (std::size_t index = 0; index < 3; ++index) {
        const double alignment = -axes[index].z;
        if (alignment > bestAlignment) {
            bestAlignment = alignment;
            downAxis = index;
        }
    }
    const std::size_t tangentA = (downAxis + 1) % 3;
    const std::size_t tangentB = (downAxis + 2) % 3;
    const Vec3 axisVector = axes[downAxis];
    const double axisExtent = downAxis == 0 ? box.halfExtents.x : downAxis == 1 ? box.halfExtents.y : box.halfExtents.z;
    const double tangentAExtent = tangentA == 0 ? box.halfExtents.x : tangentA == 1 ? box.halfExtents.y : box.halfExtents.z;
    const double tangentBExtent = tangentB == 0 ? box.halfExtents.x : tangentB == 1 ? box.halfExtents.y : box.halfExtents.z;

    const auto consider = [&](const Vec3& worldPoint) noexcept {
        const HeightSample sample = sampleHeightfield(field, worldPoint);
        const Vec3 local = worldToLocal(box, sample.point);
        const Bounds boxBounds = localBounds(box);
        if (local.x < boxBounds.minimum.x || local.x > boxBounds.maximum.x ||
            local.y < boxBounds.minimum.y || local.y > boxBounds.maximum.y ||
            local.z < boxBounds.minimum.z || local.z > boxBounds.maximum.z) {
            return;
        }

        const double penetration =
            std::min({local.x - boxBounds.minimum.x, boxBounds.maximum.x - local.x,
                      local.y - boxBounds.minimum.y, boxBounds.maximum.y - local.y,
                      local.z - boxBounds.minimum.z, boxBounds.maximum.z - local.z});
        if (penetration > deepest) {
            deepest = penetration;
            best = Contact{sample.normal, sample.point, penetration};
        }
    };

    // Bottom-face 3x3 grid in the two tangent axes.
    const Vec3 faceOffsetA = axes[tangentA] * tangentAExtent;
    const Vec3 faceOffsetB = axes[tangentB] * tangentBExtent;
    // World-space offset from box center to the bottom face along the down axis.
    const double faceSign = dot(axisVector, Vec3{0.0, 0.0, -1.0}) >= 0.0 ? 1.0 : -1.0;
    const Vec3 faceOffset = axisVector * (axisExtent * faceSign);

    for (int a = -1; a <= 1; ++a) {
        for (int b = -1; b <= 1; ++b) {
            const Vec3 samplePoint = box.settings.position + faceOffset +
                faceOffsetA * static_cast<double>(a) + faceOffsetB * static_cast<double>(b);
            consider(samplePoint);
        }
    }
    consider(box.settings.position);
    return best;
}

std::optional<Contact> capsuleHeightfield(const Body& capsule, const Body& field) noexcept {
    // Sample along the capsule segment; deepest sphere-style contact wins.
    const Segment segment = capsuleSegment(capsule);
    std::optional<Contact> best;
    double deepest = 0.0;

    constexpr std::size_t sampleCount = 8;
    for (std::size_t index = 0; index < sampleCount; ++index) {
        const double parameter = static_cast<double>(index) / static_cast<double>(sampleCount - 1);
        const Vec3 center = segment.start + (segment.end - segment.start) * parameter;
        const HeightSample sample = sampleHeightfield(field, center);
        const Vec3 separation = center - sample.point;
        const double distance = separation.length();
        if (distance >= capsule.radius) {
            continue;
        }
        const double depth = distance > std::numeric_limits<double>::epsilon()
            ? capsule.radius - distance
            : capsule.radius;
        const Vec3 normal = distance > std::numeric_limits<double>::epsilon()
            ? separation / distance
            : sample.normal;
        if (depth > deepest) {
            deepest = depth;
            best = Contact{normal, sample.point, depth};
        }
    }
    return best;
}

}

struct PhysicsWorld::Impl {
    std::vector<apr::Body> bodies;
    Vec3 gravity;
    double timestepHz = 300.0;
    std::size_t lastStepCollisionCount = 0;
    ContactInfo lastContact;
    std::uint64_t nextId = 1;

    apr::Body* find(std::uint64_t id) noexcept {
        for (apr::Body& body : bodies) {
            if (body.id == id) {
                return &body;
            }
        }
        return nullptr;
    }

    const apr::Body* find(std::uint64_t id) const noexcept {
        for (const apr::Body& body : bodies) {
            if (body.id == id) {
                return &body;
            }
        }
        return nullptr;
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
        body.velocity = settings->linearVelocity;
        body.angularVelocity = settings->angularVelocity;
        body.force = {};
        body.torque = {};
        body.inverseInertia = apr::inverseInertiaFor(body);
        bodies.push_back(std::move(body));
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

    void resolveContact(apr::Body& first, apr::Body& second, const apr::Contact& contact) noexcept {
        const double firstInverseMass = inverseMass(first);
        const double secondInverseMass = inverseMass(second);
        const double inverseMassSum = firstInverseMass + secondInverseMass;
        if (inverseMassSum <= 0.0) {
            return;
        }

        const Vec3 correction = contact.normal * (contact.penetration / inverseMassSum);
        first.settings.position -= correction * firstInverseMass;
        second.settings.position += correction * secondInverseMass;

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
        const Vec3 relativeVelocityBeforeImpulse = secondContactVelocity - firstContactVelocity;
        const double normalVelocity = dot(relativeVelocityBeforeImpulse, contact.normal);
        double normalImpulse = 0.0;

        if (normalVelocity < 0.0) {
            const double restitution = std::clamp(
                std::max(first.settings.restitution, second.settings.restitution),
                0.0,
                1.0);
            const double normalEffectiveMass = effectiveMass(contact.normal);
            if (normalEffectiveMass > 0.0) {
                normalImpulse = -(1.0 + restitution) * normalVelocity / normalEffectiveMass;
                applyImpulse(contact.normal * normalImpulse);
            }
        }

        const Vec3 tangentVelocity = relativeVelocityBeforeImpulse - contact.normal * normalVelocity;
        const double tangentLength = tangentVelocity.length();
        if (tangentLength > std::numeric_limits<double>::epsilon() && normalImpulse > 0.0) {
            const Vec3 tangent = tangentVelocity / tangentLength;
            const double frictionCoefficient = std::sqrt(
                std::max(0.0, first.settings.friction) * std::max(0.0, second.settings.friction));
            const double maximumFriction = normalImpulse * frictionCoefficient;
            const double tangentEffectiveMass = effectiveMass(tangent);
            if (tangentEffectiveMass > 0.0) {
                double frictionImpulse = -dot(relativeVelocityBeforeImpulse, tangent) / tangentEffectiveMass;
                frictionImpulse = std::clamp(frictionImpulse, -maximumFriction, maximumFriction);
                applyImpulse(tangent * frictionImpulse);
            }
        }

        ++lastStepCollisionCount;
        lastContact = ContactInfo{
            BodyHandle{first.id},
            BodyHandle{second.id},
            contact.normal,
            contact.penetration,
            normalImpulse,
        };
    }

    void resolveCollisions() noexcept {
        for (std::size_t firstIndex = 0; firstIndex < bodies.size(); ++firstIndex) {
            apr::Body& first = bodies[firstIndex];
            for (std::size_t secondIndex = firstIndex + 1; secondIndex < bodies.size(); ++secondIndex) {
                apr::Body& second = bodies[secondIndex];
                if (inverseMass(first) <= 0.0 && inverseMass(second) <= 0.0) {
                    continue;
                }

                const std::optional<apr::Contact> contact = detectContact(first, second);
                if (contact.has_value()) {
                    resolveContact(first, second, *contact);
                }
            }
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

    for (apr::Body& body : impl_->bodies) {
        const double inverseMass = Impl::inverseMass(body);
        if (inverseMass <= 0.0) {
            continue;
        }

        body.velocity += (impl_->gravity + body.force * inverseMass) * deltaSeconds;
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

    impl_->resolveCollisions();
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

ContactInfo PhysicsWorld::lastContact() const noexcept {
    return impl_->lastContact;
}

}
