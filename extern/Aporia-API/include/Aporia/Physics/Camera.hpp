#pragma once

#include "Aporia/Core/Export.hpp"
#include "Aporia/Math/Vector3.hpp"

#include <memory>

namespace Aporia {

// View effects derived from body motion. The engine is never told what the body
// is: the host submits mass and velocity, the model derives step length,
// cadence and sway on its own. Step length scales with body size (mass^(1/3)),
// so one set of defaults fits anything from a human to an elephant.
//
// Presentation-only: update() runs per rendered frame (not on the fixed physics
// step) and fills a CameraOffset. No render types are involved, so this module
// stays headless-testable and the physics solver does not depend on it.
struct CameraEffectsConfig {
    // Locomotion sway.
    double referenceMassKg = 75.0;
    double referenceStepM = 0.72;       // step length at referenceMassKg
    double verticalPerStep = 0.028;     // vertical sway per step length (m/m)
    double lateralPerStep = 0.011;      // side sway per step length
    double rollPerStep = 0.020;         // rad per step length
    double pitchPerStep = 0.008;        // rad per step length
    double walkSpeedRefMps = 1.4;       // sway reaches full amplitude here
    double landingPerStep = 0.05;       // touchdown spring impulse scale

    // Inertial seat push: the eye lags behind acceleration.
    double seatPushPerAccel = 0.003;    // m per m/s^2
    double seatMaxM = 0.12;
    double seatSpringHz = 4.0;

    // High-speed buffeting: amplitude grows with (speed - start)^2.
    double speedShakeStartMps = 8.0;
    double speedShakePerSpeed2 = 0.0004; // m per (m/s)^2

    // Master gain, 0..1. Zero disables every effect (motion-sickness option).
    double intensity = 1.0;
};

struct CameraOffset {
    // Eye displacement in world axes (Z-up world): z carries the gait vertical
    // sway, x/y carry the lateral sway along the travel direction and the seat
    // push opposite to acceleration.
    Vec3 positionM{};
    double pitchRad = 0.0; // apply around the camera right axis
    double yawRad = 0.0;
    double rollRad = 0.0;  // apply around the camera forward axis
};

class APORIA_API CameraEffects {
public:
    CameraEffects();
    explicit CameraEffects(const CameraEffectsConfig& config);
    ~CameraEffects();

    CameraEffects(const CameraEffects&) = delete;
    CameraEffects& operator=(const CameraEffects&) = delete;
    CameraEffects(CameraEffects&&) noexcept;
    CameraEffects& operator=(CameraEffects&&) noexcept;

    void setConfig(const CameraEffectsConfig& config) noexcept;
    [[nodiscard]] const CameraEffectsConfig& config() const noexcept;
    void setMassKg(double massKg) noexcept;
    [[nodiscard]] double massKg() const noexcept;
    // Comfort 0..1; stored in the config, clamped on use.
    void setIntensity(double intensity) noexcept;

    void reset() noexcept;

    // deltaSeconds is the frame time; acceleration is differentiated from the
    // velocity history internally, so the host submits no forces or inputs.
    void update(double deltaSeconds, const Vec3& velocityMps, bool onGround) noexcept;

    [[nodiscard]] CameraOffset offset() const noexcept;
    // Derived gait state, useful for telemetry and tests (0 while idle).
    [[nodiscard]] double cadenceHz() const noexcept;
    [[nodiscard]] double stepLengthM() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Optional host camera drive: free flight in a Z-up world. The host maps its
// keys/axes to a local input vector (x = right, y = up, z = forward) and the
// engine integrates the position; the look angles stay host-owned.
struct FreeFlyState {
    Vec3 position{};
    double yawRad = 0.0;
    double pitchRad = 0.0;
};

struct FreeFlyConfig {
    double baseSpeedMps = 8.0;
    double boostMultiplier = 4.0;
};

void APORIA_API freeFlyStep(FreeFlyState& state, double deltaSeconds,
                            const Vec3& moveLocal, bool boost,
                            const FreeFlyConfig& config = {}) noexcept;

} // namespace Aporia
