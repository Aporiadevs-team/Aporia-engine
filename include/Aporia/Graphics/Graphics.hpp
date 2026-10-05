#pragma once

#include "Aporia/Core/Export.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
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

// Texture addressing mode. Repeat is the default so tiling UVs (e.g. -57.8)
// wrap instead of smearing the edge texel.
enum class SamplerMode : std::uint8_t {
    Repeat = 0,
    Clamp = 1,
    MirroredRepeat = 2,
};

struct TextureDesc {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    TextureFormat format = TextureFormat::Rgba8Unorm;
    const void* pixels = nullptr;
    std::size_t dataSize = 0;
    SamplerMode wrap = SamplerMode::Repeat;
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

// Distance metric used to pick a level in a level-of-detail group.
enum class LodBoundary : std::uint8_t {
    Sphere = 0, // bounding sphere: distance to the sphere surface
    Box = 1,    // oriented box: distance to the box surface (auto-sized from level 0)
};

class APORIA_API FontHandle {
public:
    constexpr FontHandle() noexcept = default;
    constexpr explicit FontHandle(std::uint64_t value) noexcept : value_(value) {}

    [[nodiscard]] constexpr std::uint64_t value() const noexcept { return value_; }
    [[nodiscard]] constexpr bool valid() const noexcept { return value_ != 0; }

    friend constexpr bool operator==(FontHandle, FontHandle) noexcept = default;

private:
    std::uint64_t value_ = 0;
};

inline constexpr FontHandle invalidFontHandle{};

#if defined(APORIA_EXPERIMENTAL)
// EXPERIMENTAL multi-layer material: up to four diffuse layers blended by two
// mask maps (road/terrain splat), bound per draw item so a single render() can
// use several textures at once. Lives in the exp build until stable.
class APORIA_API MaterialHandle {
public:
    constexpr MaterialHandle() noexcept = default;
    constexpr explicit MaterialHandle(std::uint64_t value) noexcept : value_(value) {}
    [[nodiscard]] constexpr std::uint64_t value() const noexcept { return value_; }
    [[nodiscard]] constexpr bool valid() const noexcept { return value_ != 0; }
    friend constexpr bool operator==(MaterialHandle, MaterialHandle) noexcept = default;
private:
    std::uint64_t value_ = 0;
};

inline constexpr MaterialHandle invalidMaterialHandle{};

struct MaterialDesc {
    TextureHandle diffuse[4]{}; // 0 = base (asphalt), 1..3 blend on top
    TextureHandle mask[2]{};    // mask0.r/g/b select layers 1/2/3, mask1 = detail
    // Per-layer UV transform (scalar, applied to both axes):
    //   uv = fragUv * uvScale[i] + uvOffset[i].
    float uvScale[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float uvOffset[4] = {0.0f, 0.0f, 0.0f, 0.0f};
};
#endif

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
#if defined(APORIA_EXPERIMENTAL)
    MaterialHandle material{}; // multi-layer material (see createMaterial)
#endif
    bool playerShadowCaster = false;
};

enum class ShadowFilter : std::uint8_t {
    Pcf = 0,
    Pcss = 1,
    Evsm = 2,
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
    ShadowFilter filter = ShadowFilter::Pcf;
    std::uint32_t blockerSamples = 16;
    std::uint32_t filterSamples = 32;
    bool contactShadows = false;
    float contactDistance = 2.0f;
    float cascadeSplitLambda = 0.75f;
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

struct UiGlyph {
    std::uint32_t codePoint = 0;
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
    float planeLeft = 0.0f;
    float planeTop = 0.0f;
    float planeWidth = 0.0f;
    float planeHeight = 0.0f;
    float advance = 0.0f;
};

struct UiTextAtlasDesc {
    TextureHandle texture;
    UiTextAtlasMode mode = UiTextAtlasMode::Msdf;
    std::vector<UiGlyph> glyphs;
    float emSize = 32.0f;
    float lineHeight = 16.0f;
    float ascender = 0.95f;
    float distanceRange = 4.0f;
};

struct UiTextRun {
    std::string_view text; // non-owning: caller keeps the storage alive for the frame
    float x = 0.0f;
    float y = 0.0f;
    float size = 16.0f;
    UiColor color{};
    float outlineWidth = 0.0f;
    UiColor outlineColor{};
};

struct UiDrawList {
    std::vector<std::pair<UiRect, UiColor>> roundedRects;
    std::vector<UiTextRun> textRuns;
    UiTextAtlasDesc textAtlas{};
    FontHandle font{};
    float cornerRadius = 0.0f;
    BlurConfig blur{};
};

struct GraphicsConfig {
    bool headless = true;
    // Offscreen target size for headless runs; 0 keeps the small 64x64 default.
    // Native targets always follow the window client area instead.
    std::uint32_t headlessWidth = 0;
    std::uint32_t headlessHeight = 0;
    bool enableValidation = false;
    bool showWindow = false; // keep a host-owned native window visible during render tests
    GraphicsQuality quality = GraphicsQuality::Medium;
    float sunDirection[3] = {0.35f, -0.5f, 0.8f}; // normalized on use; points toward the light
    float shadowExtent = 30.0f;                   // sun ortho half-extent around the camera (m)
    float mirrorZ = 0.0f;                         // planar reflection plane height (m)
    ShadowConfig shadows{};
    bool playerUltraShadows = false;
    AntiAliasingMode antiAliasing = AntiAliasingMode::SMAA;
    float antiAliasingStrength = 0.75f; // 0 = off, 1 = full blend
    BlurConfig blur{};
};

struct GraphicsStats {
    std::uint64_t frameCount = 0;
    std::uint64_t presentCount = 0;
    std::uint64_t textureCount = 0;
    std::uint64_t boundTextureId = 0;
    std::uint64_t textureUploadCount = 0;
    // Level-of-detail accounting for the last frame.
    std::uint64_t lodGroupCount = 0;
    std::uint64_t lodDrawTriangles = 0;  // triangles submitted after LOD selection
    std::uint64_t lodFullTriangles = 0;  // triangles level 0 would have submitted
    std::uint64_t lodSwitchCount = 0;    // cumulative level changes
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
    float antiAliasingStrength = 0.75f;         // live resolve strength
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

    // Presentation vsync. Off selects MAILBOX/IMMEDIATE; on selects FIFO.
    // Recreates the swapchain when a native window is active.
    void setVsync(bool enabled) noexcept;
    [[nodiscard]] bool vsync() const noexcept;

    // Anti-aliasing. Mode changes recompile the resolve pipeline; strength is a
    // live 0..1 blend knob (0 disables the resolve, 1 applies it fully).
    void setAntiAliasing(AntiAliasingMode mode);
    [[nodiscard]] AntiAliasingMode antiAliasing() const noexcept;
    void setAntiAliasingStrength(float strength) noexcept;
    [[nodiscard]] float antiAliasingStrength() const noexcept;

    [[nodiscard]] bool initialized() const noexcept;
    // Blocks until the GPU has finished all submitted work. Useful for timing
    // and for deterministic teardown in tests.
    void waitIdle() noexcept;
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

#if defined(APORIA_EXPERIMENTAL)
    // EXPERIMENTAL multi-layer materials bound per RenderItem.
    MaterialHandle createMaterial(const MaterialDesc& description);
    bool updateMaterial(MaterialHandle handle, const MaterialDesc& description);
    void destroyMaterial(MaterialHandle handle) noexcept;
    [[nodiscard]] bool materialValid(MaterialHandle handle) const noexcept;
#endif

    MeshHandle createMesh(const MeshData& data);
    void destroyMesh(MeshHandle handle) noexcept;
    [[nodiscard]] std::uint64_t meshVertexCount(MeshHandle handle) const noexcept;

    // Level-of-detail group. `levels` are ordered from the highest detail to the
    // lowest; `maxDistances` (ascending, metres) holds one range per level except
    // the last, which always applies. The distance is measured from the camera
    // to the item's bounding volume, chosen by `boundary` (sphere or oriented
    // box, auto-sized from level 0). The returned handle plugs into any place a
    // MeshHandle is accepted (RenderItem::meshId, MeshRef, ...); the engine
    // selects the concrete level every frame and reports it in GraphicsStats.
    MeshHandle createLodMesh(
        const std::vector<MeshData>& levels,
        const std::vector<float>& maxDistances,
        LodBoundary boundary = LodBoundary::Sphere);
    void destroyLodMesh(MeshHandle handle) noexcept;
    [[nodiscard]] bool lodMeshValid(MeshHandle handle) const noexcept;
    [[nodiscard]] std::uint32_t lodLevelCount(MeshHandle handle) const noexcept;

    // Bakes a TTF/OTF into an alpha coverage atlas and registers it as a font.
    FontHandle createFontFromTtf(const std::string& ttfPath, float pixelSize = 32.0f);
    // Loads an msdf-atlas-gen JSON description plus its PNG atlas.
    FontHandle createFontFromMsdf(const std::string& jsonPath, const std::string& pngPath);
    void destroyFont(FontHandle handle) noexcept;
    [[nodiscard]] bool fontValid(FontHandle handle) const noexcept;

    [[nodiscard]] GraphicsStats stats() const noexcept;
    [[nodiscard]] const std::string& lastError() const noexcept;

private:
    struct Impl;

    bool renderNativeFrame() noexcept;

    std::unique_ptr<Impl> impl_;
};

}
