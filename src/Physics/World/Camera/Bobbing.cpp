#include "CameraInternals.hpp"

#include <algorithm>
#include <cmath>

namespace Aporia::physics_internal {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kMinMassKg = 0.1;
constexpr double kLandingSpringHz = 2.5;
constexpr double kActivityRateHz = 8.0;

} // namespace

double stepLengthM(const CameraEffectsConfig& config, double massKg) noexcept {
    const double mass = std::max(massKg, kMinMassKg);
    const double reference = std::max(config.referenceMassKg, kMinMassKg);
    const double referenceStep = std::max(config.referenceStepM, 0.0);
    // Isometric scaling: stride length follows body size, not proportions.
    return referenceStep * std::cbrt(mass / reference);
}

BobbingResult updateBobbing(const CameraEffectsConfig& config,
                            double massKg,
                            BobbingState& state,
                            double deltaSeconds,
                            double horizontalSpeed,
                            double verticalSpeed,
                            bool onGround) noexcept {
    BobbingResult result;

    const double step = stepLengthM(config, massKg);
    const double cadence = step > 0.0 ? horizontalSpeed / step : 0.0;
    result.cadence = cadence;

    // The phase advances with travelled distance, so standing still freezes the
    // gait and the envelope fades every offset back to zero.
    const double moving = horizontalSpeed > 0.05 ? 1.0 : 0.0;
    state.activity += (moving - state.activity) * (1.0 - std::exp(-deltaSeconds * kActivityRateHz));

    state.phase += 2.0 * kPi * cadence * deltaSeconds;
    state.phase = std::fmod(state.phase, 2.0 * kPi);

    const double gain = std::clamp(horizontalSpeed / std::max(config.walkSpeedRefMps, 0.1), 0.0, 1.5);

    // Touchdown: the impact velocity feeds a critically damped spring, so the
    // dip scales with fall speed and body size and never needs an input event.
    const double impactSpeed = std::min(verticalSpeed, state.lastVerticalSpeed);
    if (onGround && !state.wasGrounded && impactSpeed < -0.5) {
        state.dipVelocity += config.landingPerStep * step * std::abs(impactSpeed);
    }
    const double omega = 2.0 * kPi * kLandingSpringHz;
    state.dipVelocity += (-omega * omega * state.dip - 2.0 * omega * state.dipVelocity) * deltaSeconds;
    state.dip += state.dipVelocity * deltaSeconds;

    const double sway = state.activity * gain;
    const double side = std::sin(state.phase * 0.5);
    result.vertical = step * config.verticalPerStep * sway * std::sin(state.phase) - state.dip;
    result.lateral = step * config.lateralPerStep * sway * side;
    result.roll = step * config.rollPerStep * sway * side;
    result.pitch = step * config.pitchPerStep * sway * std::sin(state.phase);

    state.wasGrounded = onGround;
    state.lastVerticalSpeed = verticalSpeed;
    return result;
}

} // namespace Aporia::physics_internal
