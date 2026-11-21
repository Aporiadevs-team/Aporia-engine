#include "Aporia/Physics/Camera.hpp"

#include <algorithm>
#include <cmath>

namespace Aporia {
namespace {

// Just under 90 degrees: keeps the forward axis defined at the poles.
constexpr double kMaxPitchRad = 1.5533430342749536;

} // namespace

void freeFlyStep(FreeFlyState& state, double deltaSeconds, const Vec3& moveLocal, bool boost,
                 const FreeFlyConfig& config) noexcept {
    const double dt = std::max(deltaSeconds, 0.0);
    if (dt <= 0.0) {
        return;
    }

    Vec3 move = moveLocal;
    const double length = move.length();
    if (length > 1.0) {
        move *= 1.0 / length; // no diagonal speed-up
    }

    const double pitch = std::clamp(state.pitchRad, -kMaxPitchRad, kMaxPitchRad);
    const double cy = std::cos(state.yawRad);
    const double sy = std::sin(state.yawRad);
    const double cp = std::cos(pitch);
    const double sp = std::sin(pitch);
    const Vec3 forward{cp * cy, cp * sy, sp};
    const Vec3 right{-sy, cy, 0.0};
    const Vec3 up{0.0, 0.0, 1.0};

    const Vec3 direction = forward * move.z + right * move.x + up * move.y;
    const double speed = std::max(config.baseSpeedMps, 0.0)
        * (boost ? std::max(config.boostMultiplier, 0.0) : 1.0);
    state.position += direction * (speed * dt);
}

} // namespace Aporia
