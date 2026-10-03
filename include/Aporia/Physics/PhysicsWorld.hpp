#pragma once

#include "Aporia/Core/Export.hpp"
#include "Aporia/Math/Quaternion.hpp"
#include "Aporia/Math/Ray.hpp"
#include "Aporia/Math/Vector3.hpp"
#include "Aporia/Terrain/Terrain.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace Aporia {

struct BodyHandle {
    std::uint64_t value = 0;

    [[nodiscard]] constexpr bool valid() const noexcept {
        return value != 0;
    }

    explicit constexpr operator bool() const noexcept {
        return valid();
    }

    friend constexpr bool operator==(BodyHandle, BodyHandle) noexcept = default;
};

struct RaycastHit {
    BodyHandle body{};
    Vec3 point{};
    Vec3 normal{0.0, 0.0, 1.0};
    double distance = 0.0;
    double fraction = 0.0;
    bool hasHit = false;

    explicit constexpr operator bool() const noexcept {
        return hasHit;
    }
};

struct BodySettings {
    Vec3 position{};
    Vec3 linearVelocity{};
    Quat orientation{};
    Vec3 angularVelocity{};
    double mass = 1.0;
    double restitution = 0.2;
    double friction = 0.5;
    bool isStatic = false;
};

struct SphereBodyConfig : BodySettings {
    double radius = 0.5;
};

struct BoxBodyConfig : BodySettings {
    Vec3 halfExtents{0.5, 0.5, 0.5};
};

struct CapsuleBodyConfig : BodySettings {
    double radius = 0.5;
    double halfHeight = 0.5;
};

struct ContactInfo {
    BodyHandle first;
    BodyHandle second;
    Vec3 normal;
    double penetration = 0.0;
    double impulse = 0.0;
};

// Steering model for the body-turn predictor. The host only supplies a driver
// request in [-1, 1]; PhysicsWorld smooths the wheel, then derives the yaw rate
// from the bicycle model, capped by grip, gravity on the body's tilt and the
// body's own inertia/shape. No engine "vehicle" entity exists: any Body X can
// opt into steering.
struct SteeringConfig {
    double maxSteerAngle = 0.5;     // rad, full wheel lock
    double steerRate = 1.8;         // rad/s the wheel travels toward lock
    double steerReturn = 3.5;       // rad/s the wheel recenters hands-off
    double wheelbase = 0.0;         // m; <= 0 derives it from the body shape
    double gripLimit = 8.5;         // m/s^2 lateral grip on flat ground (~0.9 g)
    double slopeInfluence = 0.6;    // how strongly a tilt steers the heading
    double yawResponse = 5.0;       // 1/s, how fast the body reaches target yaw
};

class APORIA_API PhysicsWorld {
public:
    explicit PhysicsWorld(const Vec3& gravity = Vec3{0.0, 0.0, -9.81});
    ~PhysicsWorld();

    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;
    PhysicsWorld(PhysicsWorld&&) noexcept;
    PhysicsWorld& operator=(PhysicsWorld&&) noexcept;

    BodyHandle createSphere(const SphereBodyConfig& config);
    BodyHandle createBox(const BoxBodyConfig& config);
    BodyHandle createCapsule(const CapsuleBodyConfig& config);

    // Static heightfield collider from generated terrain. Placement comes from
    // the descriptor transform (rotation maps the field's local Z-up into the
    // world; e.g. Y-up 2D worlds use a -90° X rotation). Dynamic bodies
    // collide with the surface; returns an invalid handle on bad input.
    BodyHandle createHeightfield(const TerrainDesc& description, const TerrainHeightfield& field);

    // Surface elevation under a world point (descriptor transform applied).
    [[nodiscard]] double heightfieldHeightAt(const TerrainHeightfield& field, const Vec3& worldPoint) const noexcept;

    // Same, but uses the heightfield body's stored (possibly rotated/placed)
    // descriptor, so the result is correct for non-identity transforms.
    [[nodiscard]] double heightfieldHeightAt(BodyHandle body, const Vec3& worldPoint) const noexcept;

    void setGravity(const Vec3& gravity) noexcept;
    [[nodiscard]] Vec3 gravity() const noexcept;

    void applyForce(BodyHandle body, const Vec3& force) noexcept;
    void applyTorque(BodyHandle body, const Vec3& torque) noexcept;

    // Steering predictor. `steerInput` is the driver request in [-1, 1]; calling
    // it enables steering on the body. The wheel angle is integrated smoothly on
    // each substep and a physically limited yaw rate is applied about the body's
    // up axis, accounting for speed (no turn at rest), mass/inertia, the tilt
    // against the terrain and the body's wheelbase/track shape.
    void setBodySteering(BodyHandle body, double steerInput, const SteeringConfig& config = {}) noexcept;
    void disableBodySteering(BodyHandle body) noexcept;
    void setBodySteeringConfig(BodyHandle body, const SteeringConfig& config) noexcept;
    [[nodiscard]] double bodySteeringAngle(BodyHandle body) const noexcept;
    [[nodiscard]] double bodyYawRate(BodyHandle body) const noexcept;

    void setBodyPosition(BodyHandle body, const Vec3& position) noexcept;
    void setBodyVelocity(BodyHandle body, const Vec3& velocity) noexcept;
    void setBodyOrientation(BodyHandle body, const Quat& orientation) noexcept;
    void setBodyAngularVelocity(BodyHandle body, const Vec3& angularVelocity) noexcept;
    void setBodyFriction(BodyHandle body, double friction) noexcept;
    void setBodyRestitution(BodyHandle body, double restitution) noexcept;

    [[nodiscard]] Vec3 bodyPosition(BodyHandle body) const noexcept;
    [[nodiscard]] Vec3 bodyVelocity(BodyHandle body) const noexcept;
    [[nodiscard]] Quat bodyOrientation(BodyHandle body) const noexcept;
    [[nodiscard]] Vec3 bodyAngularVelocity(BodyHandle body) const noexcept;
    [[nodiscard]] double bodyMass(BodyHandle body) const noexcept;
    [[nodiscard]] std::size_t bodyCount() const noexcept;

    void step(double deltaSeconds) noexcept;

    // Iterative constraint/contact solver iterations (default 8; 1..64).
    void setSolverIterations(std::uint32_t iterations) noexcept;
    [[nodiscard]] std::uint32_t solverIterations() const noexcept;

    // Raycast query: returns the closest hit along the ray up to maxDistance.
    [[nodiscard]] RaycastHit raycast(const Ray& ray, double maxDistance = 1000.0) const noexcept;

    // Multi-hit raycast: all hits sorted from closest to furthest.
    [[nodiscard]] std::vector<RaycastHit> raycastAll(const Ray& ray, double maxDistance = 1000.0) const;

    // Simulation frequency for step()-less usage and Game integration.
    // Default 300 Hz; any positive finite value is accepted.
    void setTimestepHz(double hz) noexcept;
    [[nodiscard]] double timestepHz() const noexcept;

    [[nodiscard]] std::size_t lastStepCollisionCount() const noexcept;
    [[nodiscard]] ContactInfo lastContact() const noexcept;
    [[nodiscard]] std::vector<ContactInfo> lastContacts() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
