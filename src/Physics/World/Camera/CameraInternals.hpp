#pragma once

#include "Aporia/Physics/Camera.hpp"

namespace Aporia::physics_internal {

// One impulse-driven state per effect. Everything is deterministic (no RNG),
// so the same velocity sequence always produces the same offsets.

struct BobbingState {
    double phase = 0.0;    // radians, 2*pi per step
    double activity = 0.0; // 0..1 envelope, fades out while standing
    double dip = 0.0;      // touchdown spring displacement (m, positive = down)
    double dipVelocity = 0.0;
    double lastVerticalSpeed = 0.0;
    bool wasGrounded = false;
};

struct BobbingResult {
    double vertical = 0.0;
    double lateral = 0.0;
    double roll = 0.0;
    double pitch = 0.0;
    double cadence = 0.0;
};

struct SeatState {
    Vec3 offset{};
    Vec3 velocity{};
};

struct ShakeState {
    double timeSeconds = 0.0;
};

struct ShakeResult {
    double vertical = 0.0;
    double roll = 0.0;
    double pitch = 0.0;
};

[[nodiscard]] double stepLengthM(const CameraEffectsConfig& config, double massKg) noexcept;

[[nodiscard]] BobbingResult updateBobbing(const CameraEffectsConfig& config,
                                          double massKg,
                                          BobbingState& state,
                                          double deltaSeconds,
                                          double horizontalSpeed,
                                          double verticalSpeed,
                                          bool onGround) noexcept;

[[nodiscard]] Vec3 updateSeatPush(const CameraEffectsConfig& config,
                                  SeatState& state,
                                  const Vec3& acceleration,
                                  double deltaSeconds) noexcept;

[[nodiscard]] ShakeResult updateSpeedShake(const CameraEffectsConfig& config,
                                           ShakeState& state,
                                           double horizontalSpeed,
                                           double deltaSeconds) noexcept;

} // namespace Aporia::physics_internal
