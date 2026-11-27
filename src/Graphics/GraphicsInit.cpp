#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "Graphics/GraphicsImpl.hpp"

#include "Graphics/Quality/Quality.hpp"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <utility>

namespace {
using Aporia::graphics_internal::engineLog;

// The backend the renderer drives. The build picks the default, the
// APORIA_BACKEND environment variable overrides it at runtime.
Aporia::Backend::BackendKind selectBackendKind() {
    std::string requested = "auto";
#if defined(APORIA_BACKEND_DEFAULT)
    requested = APORIA_BACKEND_DEFAULT;
#endif
    if (const char* environment = std::getenv("APORIA_BACKEND");
        environment != nullptr && environment[0] != '\0') {
        requested = environment;
    }
    const Aporia::Backend::BackendKind kind = Aporia::Backend::backendKindFromString(requested);
    if (kind != Aporia::Backend::BackendKind::Auto) {
        return kind;
    }
#if defined(APORIA_ENABLE_D3D12)
    return Aporia::Backend::BackendKind::D3D12;
#else
    return Aporia::Backend::BackendKind::Auto;
#endif
}
}  // namespace

namespace Aporia {

bool Graphics::initialize(const GraphicsContext& context, const GraphicsConfig& config) {
    shutdown();
    if (!impl_) {
        impl_ = std::make_unique<Impl>();
    }
    impl_->lastError.clear();
    // This branch ships the Direct3D backends only; the renderer picks the
    // concrete version behind the abstract backend contract.
    const Backend::BackendKind backend = selectBackendKind();
    if (backend == Backend::BackendKind::Auto) {
        impl_->setError("No Direct3D backend is compiled into this build");
        return false;
    }
    return impl_->initDirectX(context, config, backend);
}

void Graphics::shutdown() noexcept {
    if (impl_) {
        impl_->destroy();
    }
}

bool Graphics::Impl::initDirectX(
    const GraphicsContext& context,
    const GraphicsConfig& config,
    Backend::BackendKind backend) {
    frameCount = 0;
    presentCount = 0;
    headless = config.headless;
    renderWidth = config.headlessWidth > 0 ? config.headlessWidth : kDefaultHeadlessExtent;
    renderHeight = config.headlessHeight > 0 ? config.headlessHeight : kDefaultHeadlessExtent;

    environment.sunDirection[0] = config.sunDirection[0];
    environment.sunDirection[1] = config.sunDirection[1];
    environment.sunDirection[2] = config.sunDirection[2];
    environment.sunIntensity = 1.0f;
    environment.mirrorZ = config.mirrorZ;
    environment.ambient = 0.32f;
    shadowExtent = config.shadowExtent;

    // The quality preset defines the shadow budget; only an explicit
    // ShadowConfig from the host may replace it.
    applyQualityPreset(config.quality);
    reflectionsEnabled = reflectionsEnabled && config.reflections;
    antiAliasing = static_cast<std::uint32_t>(config.antiAliasing);
    antiAliasingStrength = std::clamp(config.antiAliasingStrength, 0.0f, 1.0f);
    blurConfig = config.blur;
    playerUltraShadows = config.playerUltraShadows;

    const Aporia::ShadowConfig defaultShadows{};
    const Aporia::ShadowConfig& requested = config.shadows;
    const bool customShadows =
        requested.enabled != defaultShadows.enabled ||
        requested.mapSize != defaultShadows.mapSize ||
        requested.cascadeCount != defaultShadows.cascadeCount ||
        requested.passCount != defaultShadows.passCount ||
        requested.pcfRadius != defaultShadows.pcfRadius ||
        requested.bias != defaultShadows.bias ||
        requested.normalBias != defaultShadows.normalBias ||
        requested.strength != defaultShadows.strength ||
        requested.distance != defaultShadows.distance ||
        requested.filter != defaultShadows.filter ||
        requested.blockerSamples != defaultShadows.blockerSamples ||
        requested.filterSamples != defaultShadows.filterSamples ||
        requested.contactShadows != defaultShadows.contactShadows ||
        requested.contactDistance != defaultShadows.contactDistance ||
        requested.cascadeSplitLambda != defaultShadows.cascadeSplitLambda;
    if (customShadows) {
        shadowConfig = requested;
        shadowConfig.mapSize = std::max(1u, requested.mapSize);
        shadowConfig.cascadeCount = std::max(1u, requested.cascadeCount);
        shadowConfig.passCount = std::max(1u, requested.passCount);
        shadowConfig.pcfRadius = std::min(8u, requested.pcfRadius);
    }
    shadowsEnabled = shadowConfig.enabled;
    shadowMapSize = shadowConfig.mapSize;
    shadowTaps = static_cast<std::uint32_t>((shadowConfig.pcfRadius * 2 + 1) * (shadowConfig.pcfRadius * 2 + 1));
    shadowStrength = shadowConfig.strength;
    // The renderer's sun ortho box is driven by shadowExtent; keep the config
    // field in sync so shadowConfig() reports the distance actually in use.
    shadowConfig.distance = shadowExtent;

    graphics_internal::DirectXRenderer::Options options;
    options.headless = config.headless;
    options.headlessWidth = renderWidth;
    options.headlessHeight = renderHeight;
    options.nativeWindow = context.nativeWindow;
    options.width = 1280;
    options.height = 720;
    options.vsync = true;
    options.quality = config.quality;
    options.shadows = shadowConfig;
    options.shadowExtent = config.shadowExtent;
    options.sky = config.sky;
    options.sunDirection[0] = config.sunDirection[0];
    options.sunDirection[1] = config.sunDirection[1];
    options.sunDirection[2] = config.sunDirection[2];
    options.exposure = 1.0f;
    options.backend = backend;
    options.validation = config.enableValidation;

    directX = std::make_unique<graphics_internal::DirectXRenderer>();
    std::string error;
    if (!directX->initialize(options, error)) {
        directX.reset();
        setError(error);
        return false;
    }
    // The anti-aliasing mode is a runtime knob, so the renderer starts with
    // the mode the host asked for.
    directX->setAntiAliasing(static_cast<Aporia::AntiAliasingMode>(antiAliasing));
    directX->setAntiAliasingStrength(antiAliasingStrength);
    engineLog("graphics backend: %s", Backend::toString(backend));
    return true;
}

void Graphics::Impl::destroy() noexcept {
    if (directX) {
        directX->shutdown();
        directX.reset();
    }
    pendingItems.clear();
    frameCount = 0;
    presentCount = 0;
    avgFrameMs = 0.0;
    headless = true;
    activeQuality = Aporia::GraphicsQuality::Medium;
    environment = Aporia::EnvironmentState{};
    weather = Aporia::WeatherState{};
    uiDrawList = Aporia::UiDrawList{};
    shadowConfig = Aporia::ShadowConfig{};
    shadowMapSize = 0;
    shadowTaps = 9;
    shadowStrength = 0.35f;
    shadowsEnabled = true;
    hdrEnabled = false;
    reflectionsEnabled = false;
    reflectionScale = 0.0f;
    antiAliasing = 1;
    antiAliasingStrength = 0.75f;
    validationEnabled = false;
    validationMessageCount = 0;
    deviceName.clear();
    lastError.clear();
}

} // namespace Aporia
