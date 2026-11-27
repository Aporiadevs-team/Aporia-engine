#include "AntiAliasing.hpp"

#include "Graphics/GraphicsImpl.hpp"

#include <algorithm>

namespace Aporia {

namespace graphics_internal {

float clampAntiAliasingStrength(float strength) noexcept {
    return std::clamp(strength, 0.0f, 1.0f);
}

} // namespace graphics_internal

void Graphics::setAntiAliasing(AntiAliasingMode mode) {
    if (!impl_) {
        return;
    }
    impl_->antiAliasing = static_cast<std::uint32_t>(mode);
    if (impl_->directX) {
        impl_->directX->setAntiAliasing(mode);
    }
}

AntiAliasingMode Graphics::antiAliasing() const noexcept {
    if (!impl_) {
        return AntiAliasingMode::None;
    }
    if (impl_->directX) {
        return impl_->directX->antiAliasing();
    }
    return static_cast<AntiAliasingMode>(impl_->antiAliasing);
}

void Graphics::setAntiAliasingStrength(float strength) noexcept {
    if (!impl_) {
        return;
    }
    impl_->antiAliasingStrength = graphics_internal::clampAntiAliasingStrength(strength);
    if (impl_->directX) {
        impl_->directX->setAntiAliasingStrength(impl_->antiAliasingStrength);
    }
}

float Graphics::antiAliasingStrength() const noexcept {
    if (!impl_) {
        return 0.0f;
    }
    if (impl_->directX) {
        return impl_->directX->antiAliasingStrength();
    }
    return impl_->antiAliasingStrength;
}

} // namespace Aporia
