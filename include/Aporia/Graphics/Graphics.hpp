#pragma once

#include "Aporia/Core/Export.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Aporia {

struct MeshVertex {
    float position[3];
    float normal[3];
    float uv[2];
};

struct MeshData {
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
};

struct GraphicsContext {
    std::uintptr_t nativeWindow = 0;
};

enum class TextureFormat : std::uint8_t {
    Rgba8Unorm,
    Bgra8Unorm,
    Rgba8Srgb,
};

struct TextureDesc {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    TextureFormat format = TextureFormat::Rgba8Unorm;
    const void* pixels = nullptr;
    std::size_t dataSize = 0;
};

class APORIA_API TextureHandle {
public:
    constexpr TextureHandle() noexcept = default;
    constexpr explicit TextureHandle(std::uint64_t value) noexcept
        : value_(value) {
    }

    [[nodiscard]] constexpr std::uint64_t value() const noexcept {
        return value_;
    }

    [[nodiscard]] constexpr bool valid() const noexcept {
        return value_ != 0;
    }

    friend constexpr bool operator==(TextureHandle, TextureHandle) noexcept = default;

private:
    std::uint64_t value_ = 0;
};

inline constexpr TextureHandle invalidTextureHandle{};

class APORIA_API MeshHandle {
public:
    constexpr MeshHandle() noexcept = default;
    constexpr explicit MeshHandle(std::uint64_t value) noexcept
        : value_(value) {
    }

    [[nodiscard]] constexpr std::uint64_t value() const noexcept {
        return value_;
    }

    [[nodiscard]] constexpr bool valid() const noexcept {
        return value_ != 0;
    }

    friend constexpr bool operator==(MeshHandle, MeshHandle) noexcept = default;

private:
    std::uint64_t value_ = 0;
};

inline constexpr MeshHandle invalidMeshHandle{};

// View parameters for one frame. Matrices are column-major float32 in Vulkan
// clip space (see Mat4::perspectiveVulkan); copy from Mat4 with memcpy.
struct CameraState {
    float viewMatrix[16] = {};
    float projectionMatrix[16] = {};
    double position[3] = {0.0, 0.0, 0.0};
};

// Static scene environment submitted once per frame next to the camera:
// sun state for the shadow pass and the reflection plane.
struct EnvironmentState {
    float sunDirection[3] = {0.35f, -0.5f, 0.8f}; // points toward the light
    float sunIntensity = 1.0f;
    float mirrorZ = 0.0f; // planar reflection plane height (m); mirrors reflect across z=mirrorZ
};

// One drawable object for a frame.
struct RenderItem {
    std::uint64_t meshId = 0;
    float modelMatrix[16] = {};
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    bool playerShadowCaster = false;
};

// Manual shadow controls. Presets provide defaults, then these fields can be
// overridden independently for projects that need a specific shadow budget.
struct ShadowConfig {
    bool enabled = true;
    std::uint32_t mapSize = 1024;
    std::uint32_t cascadeCount = 1;
    std::uint32_t passCount = 1;
    std::uint32_t pcfRadius = 1;
    float bias = 0.0004f;
    float normalBias = 0.0001f;
    float strength = 0.35f;
    float distance = 30.0f;
};

// Abstract quality preset. One knob scales the whole picture: shadows, HDR,
// reflections and texture sharpness move together.
//   Low    - basic lighting, hard 512px shadows, no HDR/reflections,
//            textures auto-compressed to half size.
//   Medium - HDR target with ACES tonemap, soft 3x3 PCF shadows (1024px),
//            half-resolution planar reflections, textures at 3/4 size.
//   High   - max quality 5x5 PCF shadows (2048px), full-resolution
//            reflections, native textures without any reduction.
enum class GraphicsQuality : std::uint8_t {
    Low = 0,
    Medium = 1,
    High = 2,
};

enum class AntiAliasingMode : std::uint8_t {
    None = 0,
    SMAA = 1,
    TAA = 2,
};

struct UiRect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct UiColor {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};

enum class BlurMode : std::uint8_t {
    None = 0,
    Kawase = 1,
    Gaussian = 2,
};

struct BlurConfig {
    BlurMode mode = BlurMode::None;
    std::uint32_t iterations = 2;
    float radius = 4.0f;
    float intensity = 1.0f;
};

enum class UiTextAtlasMode : std::uint8_t {
    TtfBitmap = 0,
    Msdf = 1,
};

struct UiTextAtlasDesc {
    TextureHandle texture;
    UiTextAtlasMode mode = UiTextAtlasMode::Msdf;
    float lineHeight = 16.0f;
};

struct UiTextRun {
    std::string text;
    float x = 0.0f;
    float y = 0.0f;
    float size = 16.0f;
    UiColor color{};
};

struct UiDrawList {
    std::vector<std::pair<UiRect, UiColor>> roundedRects;
    std::vector<UiTextRun> textRuns;
    UiTextAtlasDesc textAtlas{};
    float cornerRadius = 0.0f;
    BlurConfig blur{};
};

struct GraphicsConfig {
    bool headless = true;
    bool enableValidation = false;
    bool showWindow = false; // keep a host-owned native window visible during render tests
    GraphicsQuality quality = GraphicsQuality::Medium;
    float sunDirection[3] = {0.35f, -0.5f, 0.8f}; // normalized on use; points toward the light
    float shadowExtent = 30.0f;                   // sun ortho half-extent around the camera (m)
    float mirrorZ = 0.0f;                         // planar reflection plane height (m)
    ShadowConfig shadows{};
    bool playerUltraShadows = false;
    AntiAliasingMode antiAliasing = AntiAliasingMode::SMAA;
    BlurConfig blur{};
};

struct GraphicsStats {
    std::uint64_t frameCount = 0;
    std::uint64_t presentCount = 0;
    std::uint64_t textureCount = 0;
    std::uint64_t boundTextureId = 0;
    std::uint64_t textureUploadCount = 0;
    std::uint64_t deviceLocalMemory = 0;
    std::uint32_t apiVersion = 0;
    std::uint32_t driverVersion = 0;
    std::uint32_t memoryTypeCount = 0;
    std::uint32_t surfaceWidth = 0;
    std::uint32_t surfaceHeight = 0;
    bool headless = true;
    bool nativeWindow = false;
    bool validationEnabled = false;
    std::uint32_t validationMessageCount = 0;
    std::string deviceName;
    // Quality preset effects (also reported by the optimization test).
    std::uint32_t quality = 1;                 // GraphicsQuality value
    bool hdrEnabled = false;                   // HDR target + ACES tonemap active
    bool reflectionsEnabled = false;           // planar reflection pass active
    std::uint32_t shadowMapSize = 0;           // sun shadow map edge (px)
    std::uint32_t textureScalePercent = 100;   // texture auto-downscale (100 = native)
    std::uint32_t antiAliasing = 1;             // AntiAliasingMode value
    double avgFrameMs = 0.0;                   // rolling CPU frame time average
};

class APORIA_API Graphics {
public:
    Graphics();
    ~Graphics();

    Graphics(const Graphics&) = delete;
    Graphics& operator=(const Graphics&) = delete;
    Graphics(Graphics&&) noexcept;
    Graphics& operator=(Graphics&&) noexcept;

    bool initialize(
        const GraphicsContext& context = {},
        const GraphicsConfig& config = {});
    void shutdown() noexcept;

    // Re-applies the quality preset (recreates quality targets in place).
    bool setQuality(GraphicsQuality quality);
    void setShadowConfig(const ShadowConfig& config);
    [[nodiscard]] ShadowConfig shadowConfig() const noexcept;
    void enablePlayerUltraShadows(bool enabled) noexcept;
    [[nodiscard]] bool playerUltraShadowsEnabled() const noexcept;

    [[nodiscard]] bool initialized() const noexcept;
    [[nodiscard]] bool renderFrame() noexcept;
    [[nodiscard]] bool renderHeadlessFrame() noexcept;

    // Draws the given items with the given camera into the current target.
    bool render(const CameraState& camera, const std::vector<RenderItem>& items);

    // Same as render() with scene environment (sun, mirror plane).
    bool render(const CameraState& camera, const EnvironmentState& environment, const std::vector<RenderItem>& items);

    // Vulkan UI draw list. The backend owns the pass; callers only describe
    // rectangles and text runs, so no GDI/OS drawing is involved.
    void setUiDrawList(UiDrawList drawList);
    void clearUiDrawList() noexcept;

    // Copies the last rendered headless color target into host memory (RGBA8,
    // renderWidth * renderHeight * 4 bytes). Native targets are not readable.
    [[nodiscard]] bool readbackPixels(std::vector<std::uint8_t>& pixels) const;

    TextureHandle createTexture2D(const TextureDesc& description);
    bool updateTexture2D(
        TextureHandle handle,
        const void* pixels,
        std::size_t dataSize);
    bool setTexture2D(TextureHandle handle) noexcept;
    void destroyTexture2D(TextureHandle handle) noexcept;

    MeshHandle createMesh(const MeshData& data);
    void destroyMesh(MeshHandle handle) noexcept;
    [[nodiscard]] std::uint64_t meshVertexCount(MeshHandle handle) const noexcept;

    [[nodiscard]] GraphicsStats stats() const noexcept;
    [[nodiscard]] const std::string& lastError() const noexcept;

private:
    struct Impl;

    bool renderNativeFrame() noexcept;

    std::unique_ptr<Impl> impl_;
};

}
