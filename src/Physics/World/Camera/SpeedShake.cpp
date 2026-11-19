#include "CameraInternals.hpp"

#include <cmath>

namespace Aporia::physics_internal {
namespace {

constexpr double kPi = 3.14159265358979323846;

double oscillator(double seconds, double hertz, double phase) noexcept {
    return std::sin(seconds * 2.0 * kPi * hertz + phase);
}

} // namespace

ShakeResult updateSpeedShake(const CameraEffectsConfig& config,
                             ShakeState& state,
                             double horizontalSpeed,
                             double deltaSeconds) noexcept {
    ShakeResult result;
    state.timeSeconds += deltaSeconds;

    // Real buffeting grows with dynamic pressure, i.e. with speed squared, and
    // only past the speed where the medium starts to matter.
    const double excess = horizontalSpeed - config.speedShakeStartMps;
    if (excess <= 0.0) {
        return result;
    }

    const double amplitude = config.speedShakePerSpeed2 * excess * excess;
    const double t = state.timeSeconds;
    // Incommensurate frequencies: the pattern never visibly repeats, and it is
    // deterministic, so tests and replays stay reproducible without an RNG.
    const double lateralNoise = oscillator(t, 13.7, 0.0) * 0.55 + oscillator(t, 21.3, 1.7) * 0.30;
    const double verticalNoise = oscillator(t, 11.9, 0.6) * 0.55 + oscillator(t, 27.1, 2.3) * 0.30;

    result.vertical = amplitude * verticalNoise;
    result.roll = amplitude * lateralNoise * 0.3;
    result.pitch = amplitude * verticalNoise * 0.2;
    return result;
}

} // namespace Aporia::physics_internal
