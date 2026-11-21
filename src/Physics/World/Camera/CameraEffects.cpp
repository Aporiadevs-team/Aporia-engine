#include "CameraInternals.hpp"

#include <algorithm>
#include <cmath>

namespace Aporia {

struct CameraEffects::Impl {
    CameraEffectsConfig config{};
    double massKg = 75.0;
    physics_internal::BobbingState bobbing{};
    physics_internal::SeatState seat{};
    physics_internal::ShakeState shake{};
    Vec3 previousVelocity{};
    Vec3 accelerationSmoothed{};
    bool hasPreviousVelocity = false;
    CameraOffset offset{};
    double cadence = 0.0;
};

CameraEffects::CameraEffects()
    : CameraEffects(CameraEffectsConfig{}) {
}

CameraEffects::CameraEffects(const CameraEffectsConfig& config)
    : impl_(std::make_unique<Impl>()) {
    impl_->config = config;
    impl_->massKg = std::max(config.referenceMassKg, 0.1);
}

CameraEffects::~CameraEffects() = default;
CameraEffects::CameraEffects(CameraEffects&&) noexcept = default;
CameraEffects& CameraEffects::operator=(CameraEffects&&) noexcept = default;

void CameraEffects::setConfig(const CameraEffectsConfig& config) noexcept {
    if (impl_) {
        impl_->config = config;
    }
}

const CameraEffectsConfig& CameraEffects::config() const noexcept {
    static const CameraEffectsConfig kFallback{};
    return impl_ ? impl_->config : kFallback;
}

void CameraEffects::setMassKg(double massKg) noexcept {
    if (impl_) {
        impl_->massKg = std::max(massKg, 0.1);
    }
}

double CameraEffects::massKg() const noexcept {
    return impl_ ? impl_->massKg : 0.0;
}

void CameraEffects::setIntensity(double intensity) noexcept {
    if (impl_) {
        impl_->config.intensity = std::clamp(intensity, 0.0, 1.0);
    }
}

void CameraEffects::reset() noexcept {
    if (!impl_) {
        return;
    }
    auto& impl = *impl_;
    impl.bobbing = {};
    impl.seat = {};
    impl.shake = {};
    impl.previousVelocity = {};
    impl.accelerationSmoothed = {};
    impl.hasPreviousVelocity = false;
    impl.offset = {};
    impl.cadence = 0.0;
}

void CameraEffects::update(double deltaSeconds, const Vec3& velocityMps, bool onGround) noexcept {
    if (!impl_) {
        return;
    }
    auto& impl = *impl_;
    const double dt = std::clamp(deltaSeconds, 0.0, 0.1);
    if (dt <= 0.0) {
        return;
    }
    const CameraEffectsConfig& config = impl.config;

    const double horizontalSpeed =
        std::sqrt(velocityMps.x * velocityMps.x + velocityMps.y * velocityMps.y);

    // Acceleration is differentiated and low-pass filtered here, so the host
    // only submits the physics body state: no force, input or event plumbing.
    if (impl.hasPreviousVelocity) {
        const Vec3 acceleration = (velocityMps - impl.previousVelocity) / dt;
        const double blend = 1.0 - std::exp(-dt * 12.0);
        impl.accelerationSmoothed = impl.accelerationSmoothed * (1.0 - blend) + acceleration * blend;
    }
    impl.previousVelocity = velocityMps;
    impl.hasPreviousVelocity = true;

    const physics_internal::BobbingResult bob = physics_internal::updateBobbing(
        config, impl.massKg, impl.bobbing, dt, horizontalSpeed, velocityMps.z, onGround);
    impl.cadence = bob.cadence;

    const Vec3 seatOffset =
        physics_internal::updateSeatPush(config, impl.seat, impl.accelerationSmoothed, dt);
    const physics_internal::ShakeResult shake =
        physics_internal::updateSpeedShake(config, impl.shake, horizontalSpeed, dt);

    Vec3 rightAxis{};
    if (horizontalSpeed > 1.0e-3) {
        rightAxis = Vec3{-velocityMps.y / horizontalSpeed, velocityMps.x / horizontalSpeed, 0.0};
    }

    const double gain = std::clamp(config.intensity, 0.0, 1.0);
    impl.offset.positionM = (seatOffset
                             + rightAxis * bob.lateral
                             + Vec3{0.0, 0.0, bob.vertical + shake.vertical}) * gain;
    impl.offset.pitchRad = (bob.pitch + shake.pitch) * gain;
    impl.offset.rollRad = (bob.roll + shake.roll) * gain;
    impl.offset.yawRad = 0.0;
}

CameraOffset CameraEffects::offset() const noexcept {
    return impl_ ? impl_->offset : CameraOffset{};
}

double CameraEffects::cadenceHz() const noexcept {
    return impl_ ? impl_->cadence : 0.0;
}

double CameraEffects::stepLengthM() const noexcept {
    return impl_ ? physics_internal::stepLengthM(impl_->config, impl_->massKg) : 0.0;
}

} // namespace Aporia
