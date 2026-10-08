#include "Smooth.hpp"

#include "Graphics/GlobalDrawer/GraphicsImpl.hpp"

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
    impl_->hdrRenderer.setAntiAliasing(mode);
    if (initialized()) {
        impl_->rebuildQualityTargets();
    }
}

AntiAliasingMode Graphics::antiAliasing() const noexcept {
    if (!impl_) {
        return AntiAliasingMode::None;
    }
    return static_cast<AntiAliasingMode>(impl_->antiAliasing);
}

void Graphics::setAntiAliasingStrength(float strength) noexcept {
    if (!impl_) {
        return;
    }
    impl_->antiAliasingStrength = graphics_internal::clampAntiAliasingStrength(strength);
}

float Graphics::antiAliasingStrength() const noexcept {
    return impl_ != nullptr ? impl_->antiAliasingStrength : 0.0f;
}

} // namespace Aporia
