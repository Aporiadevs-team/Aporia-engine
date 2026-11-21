#include "CameraInternals.hpp"

#include <algorithm>
#include <cmath>

namespace Aporia::physics_internal {
namespace {

constexpr double kPi = 3.14159265358979323846;

} // namespace

Vec3 updateSeatPush(const CameraEffectsConfig& config,
                    SeatState& state,
                    const Vec3& acceleration,
                    double deltaSeconds) noexcept {
    // The eye lags opposite to acceleration: throttle-in pushes it backwards,
    // braking pulls it forward. Only a second-order response, no host events.
    const double maxOffset = std::max(config.seatMaxM, 0.0);
    Vec3 target = acceleration * (-config.seatPushPerAccel);
    const double length = target.length();
    if (length > maxOffset && length > 0.0) {
        target *= maxOffset / length;
    }

    const double omega = 2.0 * kPi * std::max(config.seatSpringHz, 0.1);
    const Vec3 spring = (target - state.offset) * (omega * omega) - state.velocity * (2.0 * omega);
    state.velocity += spring * deltaSeconds;
    state.offset += state.velocity * deltaSeconds;
    return state.offset;
}

} // namespace Aporia::physics_internal
