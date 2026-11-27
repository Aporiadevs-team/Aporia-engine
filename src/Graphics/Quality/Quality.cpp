#include "Quality.hpp"

#include "Graphics/GraphicsImpl.hpp"

#include <algorithm>

namespace Aporia {

using graphics_internal::QualityParameters;
using graphics_internal::qualityParameters;

namespace graphics_internal {

QualityParameters qualityParameters(Aporia::GraphicsQuality quality) noexcept {
    switch (quality) {
    case Aporia::GraphicsQuality::Low:
        return {512, 1, false, false, 0.0f, 0.22f, 50, 0};
    case Aporia::GraphicsQuality::Medium:
        return {1024, 9, true, true, 0.5f, 0.16f, 75, 8};
    case Aporia::GraphicsQuality::High:
    default:
        return {4096, 49, true, true, 1.0f, 0.10f, 100, 16};
    }
}

} // namespace graphics_internal

void Graphics::Impl::applyQualityPreset(Aporia::GraphicsQuality quality) noexcept {
    const QualityParameters parameters = qualityParameters(quality);
    shadowMapSize = parameters.shadowMapSize;
    shadowTaps = parameters.shadowTaps;
    hdrEnabled = parameters.hdr;
    reflectionsEnabled = parameters.reflections;
    reflectionScale = parameters.reflectionScale;
    shadowStrength = parameters.shadowStrength;
    maxLights = parameters.maxLights;
    shadowConfig.mapSize = parameters.shadowMapSize;
    shadowConfig.cascadeCount = 1;
    shadowConfig.passCount = 1;
    shadowConfig.pcfRadius = parameters.shadowTaps <= 1 ? 0u : parameters.shadowTaps <= 9 ? 1u : 3u;
    shadowConfig.strength = parameters.shadowStrength;
    shadowConfig.filter = quality == Aporia::GraphicsQuality::High ? Aporia::ShadowFilter::Pcss : Aporia::ShadowFilter::Pcf;
    shadowConfig.contactShadows = quality != Aporia::GraphicsQuality::Low;
    shadowConfig.contactDistance = quality == Aporia::GraphicsQuality::High ? 3.0f : 1.5f;
    activeQuality = quality;
}

bool Graphics::setQuality(GraphicsQuality quality) {
    if (!impl_) {
        return false;
    }
    if (!initialized()) {
        impl_->setError("Graphics is not initialized");
        return false;
    }
    if (quality == impl_->activeQuality) {
        return true;
    }
    impl_->applyQualityPreset(quality);
    impl_->directX->setQuality(quality);
    return true;
}

void Graphics::setShadowConfig(const ShadowConfig& config) {
    if (!impl_) {
        return;
    }
    impl_->shadowConfig = config;
    impl_->shadowConfig.mapSize = std::max(1u, config.mapSize);
    impl_->shadowConfig.cascadeCount = std::max(1u, config.cascadeCount);
    impl_->shadowConfig.passCount = std::max(1u, config.passCount);
    impl_->shadowConfig.pcfRadius = std::min(8u, config.pcfRadius);
    impl_->shadowMapSize = impl_->shadowConfig.mapSize;
    impl_->shadowTaps = static_cast<std::uint32_t>((impl_->shadowConfig.pcfRadius * 2 + 1) * (impl_->shadowConfig.pcfRadius * 2 + 1));
    impl_->shadowStrength = impl_->shadowConfig.strength;
    impl_->shadowsEnabled = impl_->shadowConfig.enabled;
    if (impl_->directX) {
        impl_->directX->setShadowConfig(impl_->shadowConfig);
    }
}

ShadowConfig Graphics::shadowConfig() const noexcept {
    if (!impl_) {
        return ShadowConfig{};
    }
    if (impl_->directX) {
        return impl_->directX->shadowConfig();
    }
    return impl_->shadowConfig;
}

void Graphics::setShadowDistance(float metres) {
    if (!impl_) {
        return;
    }
    // The extent is a half size around the camera, so it has to stay positive
    // and small enough that the shadow map keeps a usable texel density.
    const float clamped = std::clamp(metres, 1.0f, 500.0f);
    impl_->shadowConfig.distance = clamped;
    impl_->shadowExtent = clamped;
    if (impl_->directX) {
        impl_->directX->setShadowExtent(clamped);
    }
}

float Graphics::shadowDistance() const noexcept {
    return impl_ ? impl_->shadowExtent : 0.0f;
}

void Graphics::scaleShadowDistance(float factor) {
    if (!impl_ || factor <= 0.0f) {
        return;
    }
    setShadowDistance(impl_->shadowExtent * factor);
}

void Graphics::enablePlayerUltraShadows(bool enabled) noexcept {
    if (impl_) {
        impl_->playerUltraShadows = enabled;
    }
}
bool Graphics::playerUltraShadowsEnabled() const noexcept {
    return impl_ != nullptr && impl_->playerUltraShadows;
}

} // namespace Aporia
