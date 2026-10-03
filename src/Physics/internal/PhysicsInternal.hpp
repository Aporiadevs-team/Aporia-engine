#pragma once

// Shared internal physics types and algorithms. These live in namespace
// Aporia::apr and are split across PhysicsHelpers, PhysicsContacts and
// PhysicsRaycast; PhysicsWorld only orchestrates them.

#include "Aporia/Math/Quaternion.hpp"
#include "Aporia/Math/Ray.hpp"
#include "Aporia/Math/Vector3.hpp"
#include "Aporia/Physics/PhysicsWorld.hpp"
#include "Aporia/Terrain/Terrain.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <optional>
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
    std::uint64_t id = 0;
    BodySettings settings;
    ColliderType colliderType = ColliderType::Sphere;
    double radius = 0.0;
    Vec3 halfExtents{};
    double halfHeight = 0.0;
    // Bounding sphere around the collider center, used for broad-phase culling.
    double boundingRadius = 0.0;
    Vec3 velocity{};
    Vec3 angularVelocity{};
    Vec3 force{};
    Vec3 torque{};
    Vec3 inverseInertia{};
    // Body-turn predictor state (valid when steeringEnabled).
    bool steeringEnabled = false;
    bool onGround = false;
    double steerInput = 0.0;
    double steerAngle = 0.0;
    double lastYawRate = 0.0;
    SteeringConfig steering{};
    // Heightfield payload (valid when colliderType == Heightfield).
    TerrainDesc terrainDescription;
    TerrainHeightfield terrainField;
};

struct Contact {
    Vec3 normal{};
    Vec3 point{};
    double penetration = 0.0;

    Contact() noexcept = default;
    Contact(const Vec3& normalValue, double penetrationValue) noexcept
        : normal(normalValue), point{}, penetration(penetrationValue) {
    }
    Contact(const Vec3& normalValue, const Vec3& pointValue, double penetrationValue) noexcept
        : normal(normalValue), point(pointValue), penetration(penetrationValue) {
    }
};

// One resolved contact with accumulated impulses, reused across solver
// iterations so no per-iteration allocation is needed.
struct ActiveContact {
    std::size_t firstIndex = 0;
    std::size_t secondIndex = 0;
    Contact contact;
    double normalTarget = 0.0; // desired separating velocity (restitution bias)
    double normalImpulse = 0.0;
    double frictionImpulse = 0.0;
};

struct Bounds {
    Vec3 minimum;
    Vec3 maximum;
};

struct Segment {
    Vec3 start;
    Vec3 end;
};

struct ClosestSegmentPoints {
    Vec3 first;
    Vec3 second;
};

// Terrain surface point and world-space normal under a planar sample.
struct HeightSample {
    Vec3 point;
    Vec3 normal;
    double elevation;
};

struct RaycastInternalHit {
    double distance = std::numeric_limits<double>::infinity();
    Vec3 point{};
    Vec3 normal{0.0, 0.0, 1.0};
    bool hasHit = false;
};

// Solver tuning. Contacts shallower than slop are left alone, restitution is
// ignored for slow approaches, and positional correction runs once per step.
inline constexpr double kLinearSlop = 0.01;
inline constexpr double kPositionCorrection = 0.8;
inline constexpr double kMaxCorrection = 0.2;
inline constexpr double kRestitutionThreshold = 1.0;

// --- PhysicsHelpers ---------------------------------------------------------

bool finite(const Vec3& value) noexcept;
bool finite(double value) noexcept;
bool finite(const Quat& value) noexcept;
Vec3 absolute(const Vec3& value) noexcept;
Vec3 clamp(const Vec3& value, const Vec3& minimum, const Vec3& maximum) noexcept;

Bounds localBounds(const Body& body) noexcept;
Vec3 localToWorld(const Body& body, const Vec3& point) noexcept;
Vec3 worldToLocal(const Body& body, const Vec3& point) noexcept;
std::array<Vec3, 3> boxAxes(const Body& body) noexcept;
Segment capsuleSegment(const Body& body) noexcept;
Vec3 closestPointOnBox(const Vec3& point, const Bounds& box) noexcept;
Vec3 closestPointOnSegment(const Vec3& point, const Segment& segment) noexcept;
ClosestSegmentPoints closestPointsOnSegments(const Segment& first, const Segment& second) noexcept;
std::optional<BodySettings> normalizeSettings(const BodySettings& settings) noexcept;
Vec3 inverseInertiaFor(const Body& body) noexcept;
double projectedRadius(const Body& body, const std::array<Vec3, 3>& axes, const Vec3& axis) noexcept;
HeightSample sampleHeightfield(const Body& field, const Vec3& planarWorldPoint) noexcept;

// --- PhysicsContacts --------------------------------------------------------

std::optional<Contact> sphereSphere(const Body& first, const Body& second) noexcept;
std::optional<Contact> sphereBox(const Body& sphere, const Body& box) noexcept;
std::optional<Contact> sphereCapsule(const Body& sphere, const Body& capsule) noexcept;
std::optional<Contact> boxBox(const Body& first, const Body& second) noexcept;
std::optional<Contact> capsuleCapsule(const Body& first, const Body& second) noexcept;
std::optional<Contact> boxCapsule(const Body& box, const Body& capsule) noexcept;
std::optional<Contact> sphereHeightfield(const Body& sphere, const Body& field) noexcept;
std::optional<Contact> boxHeightfield(const Body& box, const Body& field) noexcept;
std::optional<Contact> capsuleHeightfield(const Body& capsule, const Body& field) noexcept;
void boxBoxManifold(const Body& first, const Body& second, std::vector<Contact>& contacts) noexcept;
void boxHeightfieldManifold(const Body& box, const Body& field, std::vector<Contact>& contacts) noexcept;

// --- PhysicsRaycast ---------------------------------------------------------

RaycastInternalHit raycastSphere(const Ray& ray, const Body& sphere) noexcept;
RaycastInternalHit raycastBox(const Ray& ray, const Body& box) noexcept;
RaycastInternalHit raycastCapsule(const Ray& ray, const Body& capsule) noexcept;
RaycastInternalHit raycastHeightfield(const Ray& ray, const Body& field, double maxDistance) noexcept;

} // namespace apr
} // namespace Aporia
