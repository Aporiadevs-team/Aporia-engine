#include "Quality.hpp"

#include "Graphics/GlobalDrawer/GraphicsImpl.hpp"

#include <algorithm>
#include <string>

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
    textureStore.setTextureScalePercent(parameters.textureScalePercent);
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
    hdrRenderer.setQuality(quality);
    shadowRenderer.setConfig(shadowConfig);
}

bool Graphics::Impl::ensureDepthForExtent(std::uint32_t width, std::uint32_t height) {
    std::string error;
    if (!QualityTargets::ensureDepthForExtent(depthFormat, width, height, error)) {
        setError(error);
        return false;
    }
    return true;
}

bool Graphics::Impl::createHdrRenderPass() {
    std::string error;
    if (!QualityTargets::createHdrRenderPass(depthFormat, error)) {
        setError(error);
        return false;
    }
    return true;
}

bool Graphics::Impl::createShadowResources() {
    std::string error;
    if (!QualityTargets::createShadowResources(depthFormat, shadowMapSize, error)) {
        setError(error);
        return false;
    }
    return true;
}

bool Graphics::Impl::rebuildQualityTargets() {
    graphics_internal::QualityTargets::RebuildContext context;
    context.mainRenderPass = renderPass;
    context.colorFormat = colorFormat;
    context.depthFormat = depthFormat;
    context.headless = headless;
    context.swapchainExtent = swapchainExtent;
    context.renderWidth = renderWidth;
    context.renderHeight = renderHeight;
    context.hdrEnabled = hdrEnabled;
    context.reflectionsEnabled = reflectionsEnabled;
    context.reflectionScale = reflectionScale;
    context.shadowMapSize = shadowMapSize;
    context.antiAliasing = antiAliasing;
    context.sceneUniformSize = sizeof(SceneUniforms);
    context.scene = &scene;
    context.textures = &textureStore;
    context.taa = &taa;
    context.tonemapDescriptorLayout = pipelines.tonemapDescriptorLayout;
    context.tonemapDescriptorPool = pipelines.tonemapDescriptorPool;
    context.tonemapVertexShader = pipelines.tonemapVertexShader;
    context.taaFragmentShader = pipelines.taaFragmentShader;
    std::string error;
    if (!QualityTargets::rebuild(context, error)) {
        setError(error);
        return false;
    }
    return true;
}

void Graphics::Impl::destroyQualityTargets() noexcept {
    destroyTargets(device, pipelines.tonemapDescriptorPool, taa, scene);
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
    if (!impl_->rebuildQualityTargets()) {
        return false;
    }
    return impl_->rebuildMeshPipelines();
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
    impl_->shadowTaps = static_cast<int>((impl_->shadowConfig.pcfRadius * 2 + 1) * (impl_->shadowConfig.pcfRadius * 2 + 1));
    impl_->shadowStrength = impl_->shadowConfig.strength;
    impl_->shadowsEnabled = impl_->shadowConfig.enabled;
    if (initialized()) {
        impl_->rebuildQualityTargets();
    }
}

ShadowConfig Graphics::shadowConfig() const noexcept {
    return impl_ ? impl_->shadowConfig : ShadowConfig{};
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
