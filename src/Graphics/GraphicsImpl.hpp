#pragma once

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

// Private implementation of the Graphics facade.
//
// This branch ships the Direct3D backends only: the renderer lives behind the
// abstract backend contract (Aporia::Backend::GraphicsBackend), so the facade
// never talks to a graphics API directly.

#include "Aporia/Graphics/Graphics.hpp"
#include "Aporia/Math/Matrix4.hpp"

#include "Graphics/DirectX/DirectXRenderer.hpp"
#include "Graphics/SceneManager/TimeManager/TimeManager.hpp"
#include "Graphics/Support/Log.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Aporia {

// Offscreen target edge used when GraphicsConfig leaves the headless size at 0.
inline constexpr std::uint32_t kDefaultHeadlessExtent = 64;

// Private implementation of the Graphics facade. Declared here so its member
// functions can be spread across several translation units by responsibility.
struct Graphics::Impl {
    // The renderer: one Direct3D backend behind the abstract contract.
    std::unique_ptr<graphics_internal::DirectXRenderer> directX;

    // Scene state the facade owns and hands to the renderer every frame.
    Aporia::EnvironmentState environment{};
    Aporia::WeatherState weather{};
    Aporia::UiDrawList uiDrawList{};
    Aporia::BlurConfig blurConfig{};
    CameraState pendingCamera{};
    std::vector<RenderItem> pendingItems;

    // Shadow/quality state mirrored from the renderer so the facade can answer
    // the statistics and configuration queries without touching the backend.
    Aporia::ShadowConfig shadowConfig{};
    Aporia::GraphicsQuality activeQuality = Aporia::GraphicsQuality::Medium;
    std::uint32_t shadowMapSize = 0;
    std::uint32_t shadowTaps = 9;
    float shadowStrength = 0.35f;
    float shadowExtent = 30.0f;
    bool shadowsEnabled = true;
    bool playerUltraShadows = false;
    std::uint32_t maxLights = 16;

    // Render feature flags mirrored from the renderer.
    bool hdrEnabled = false;
    bool reflectionsEnabled = false;
    float reflectionScale = 0.0f;
    std::uint32_t antiAliasing = 1;
    float antiAliasingStrength = 0.75f;

    // Time-of-day clock (drives sunrises and sunsets).
    graphics_internal::TimeManager timeManager;
    bool timeDrivenSun = false;
    // Sky-ambient multiplier for the current sun elevation (1 = day). Only the
    // time-driven sun changes it; a host-driven sun keeps the full ambient.
    float nightAmbientScale = 1.0f;

#if defined(APORIA_SEASONS)
    Aporia::Season season = Aporia::Season::Summer;
    Aporia::SeasonPreset seasonAtmosphere{};
    bool seasonActive = false;
#endif

    // Frame counters and timings.
    std::uint64_t frameCount = 0;
    std::uint64_t presentCount = 0;
    double avgFrameMs = 0.0;
    float frameDeltaSeconds = 1.0f / 60.0f;
    // Wall clock of the previous frame. The simulation clock advances by the
    // real time between two frames, not by how long the render call took:
    // using the render time made the sun crawl on a fast machine and stutter
    // whenever a frame was slow.
    std::int64_t lastFrameCounter = 0;
    bool hasLastFrameCounter = false;

    // Target description (headless offscreen size or native window).
    bool headless = true;
    std::uint32_t renderWidth = kDefaultHeadlessExtent;
    std::uint32_t renderHeight = kDefaultHeadlessExtent;

    bool validationEnabled = false;
    std::uint32_t validationMessageCount = 0;
    std::string deviceName;
    std::string lastError;

#if defined(APORIA_EXPERIMENTAL)
    // EXPERIMENTAL renderer threading switch (kept for API compatibility).
    bool multithreadedRender = true;
#endif

    // ---- inline helpers -----------------------------------------------------
    void setError(std::string message) {
        Aporia::graphics_internal::engineLog("[error] %s", message.c_str());
        lastError = std::move(message);
    }

    // ---- members defined per domain ----------------------------------------
    // Applies the quality preset to the mirrored facade state.
    void applyQualityPreset(Aporia::GraphicsQuality quality) noexcept;
    // Starts the Direct3D renderer behind the facade.
    bool initDirectX(
        const GraphicsContext& context,
        const GraphicsConfig& config,
        Backend::BackendKind backend);
    void destroy() noexcept;

    // Season presets modulate the host environment instead of replacing it: the
    // sun direction always comes from EnvironmentState, the preset only scales
    // intensity and sets the ambient floor.
    [[nodiscard]] float effectiveSunIntensity() const noexcept {
#if defined(APORIA_SEASONS)
        if (seasonActive) {
            return environment.sunIntensity * seasonAtmosphere.sunIntensityScale;
        }
#endif
        return environment.sunIntensity;
    }

    [[nodiscard]] float effectiveAmbient() const noexcept {
#if defined(APORIA_SEASONS)
        if (seasonActive) {
            return seasonAtmosphere.ambient;
        }
#endif
        return environment.ambient;
    }
};

} // namespace Aporia
