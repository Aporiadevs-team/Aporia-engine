// Scene renderer that drives the Direct3D backends through the abstract
// backend contract (Aporia::Backend::GraphicsBackend).
//
// The renderer sits behind the public Graphics facade: the facade forwards to
// it, so the scene data (meshes, textures, items) stays backend independent.
#pragma once

#include "Aporia/Graphics/Backend.hpp"
#include "Aporia/Graphics/Graphics.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Aporia::graphics_internal {

class DirectXRenderer {
public:
    struct Options {
        bool headless = true;
        std::uint32_t headlessWidth = 64;
        std::uint32_t headlessHeight = 64;
        std::uintptr_t nativeWindow = 0;
        std::uint32_t width = 1280;
        std::uint32_t height = 720;
        bool vsync = true;
        Aporia::GraphicsQuality quality = Aporia::GraphicsQuality::Medium;
        Aporia::ShadowConfig shadows{};
        float shadowExtent = 30.0f;
        bool sky = false;
        float sunDirection[3] = {0.35f, -0.5f, 0.8f};
        float exposure = 1.0f;
        // Which backend to drive. Auto picks Direct3D 12 when it is compiled.
        Aporia::Backend::BackendKind backend = Aporia::Backend::BackendKind::Auto;
        bool validation = false;
    };

    DirectXRenderer();
    ~DirectXRenderer();

    DirectXRenderer(const DirectXRenderer&) = delete;
    DirectXRenderer& operator=(const DirectXRenderer&) = delete;

    bool initialize(const Options& options, std::string& error);
    void shutdown() noexcept;
    [[nodiscard]] bool initialized() const noexcept { return initialized_; }

    // Scene data. The handles are engine handles, not backend handles: the
    // facade hands them back to the host unchanged.
    std::uint64_t createMesh(const MeshData& data);
    void destroyMesh(std::uint64_t handle) noexcept;
    [[nodiscard]] std::uint32_t meshVertexCount(std::uint64_t handle) const noexcept;

    // A level-of-detail group: the renderer picks the level closest to the
    // camera every frame and reports the triangle counters in the stats.
    std::uint64_t createLodMesh(
        const std::vector<MeshData>& levels,
        const std::vector<float>& maxDistances,
        Aporia::LodBoundary boundary);
    void destroyLodMesh(std::uint64_t handle) noexcept;
    [[nodiscard]] bool lodMeshValid(std::uint64_t handle) const noexcept;
    [[nodiscard]] std::uint32_t lodLevelCount(std::uint64_t handle) const noexcept;

    std::uint64_t createTexture(const TextureDesc& description);
    bool updateTexture(std::uint64_t handle, const void* pixels, std::size_t dataSize);
    void destroyTexture(std::uint64_t handle) noexcept;
    bool setFrameTexture(std::uint64_t handle) noexcept;
    // Live texture counters: the fallback white texture counts as one texture
    // and its initial upload as one upload, like any other texture.
    [[nodiscard]] std::uint64_t textureCount() const noexcept {
        return static_cast<std::uint64_t>(textures_.size()) + (defaultTexture_.valid() ? 1u : 0u);
    }
    [[nodiscard]] std::uint64_t textureUploadCount() const noexcept {
        std::uint64_t total = defaultTextureUploads_;
        for (const auto& entry : textures_) {
            total += entry.second.uploadCount;
        }
        return total;
    }
    [[nodiscard]] std::uint64_t boundTextureId() const noexcept { return boundTexture_; }

    // Fonts: a baked atlas plus the texture it was uploaded to.
    std::uint64_t createFontFromTtf(const std::string& ttfPath, float pixelSize, std::string& error);
    std::uint64_t createFontFromMsdf(
        const std::string& jsonPath, const std::string& pngPath, std::string& error);
    void destroyFont(std::uint64_t handle) noexcept;
    [[nodiscard]] bool fontValid(std::uint64_t handle) const noexcept;
    [[nodiscard]] float measureText(
        std::uint64_t handle, std::string_view text, float size) const noexcept;

    // Draws one frame and presents it.
    bool renderFrame(
        const CameraState& camera,
        const EnvironmentState& environment,
        const std::vector<RenderItem>& items);

    void waitIdle() noexcept;
    [[nodiscard]] bool readbackPixels(std::vector<std::uint8_t>& pixels) const;

    void setLights(const std::vector<LightDesc>& lights);
    void clearLights() noexcept;
    [[nodiscard]] std::size_t lightCount() const noexcept { return lights_.size(); }

    void setUiDrawList(UiDrawList drawList);
    void clearUiDrawList() noexcept;

    void setQuality(Aporia::GraphicsQuality quality);
    void setShadowConfig(const Aporia::ShadowConfig& config);
    [[nodiscard]] Aporia::ShadowConfig shadowConfig() const noexcept { return options_.shadows; }
    void setShadowExtent(float metres) noexcept;
    [[nodiscard]] float shadowExtent() const noexcept { return options_.shadowExtent; }
    void setVsync(bool enabled) noexcept;
    [[nodiscard]] bool vsync() const noexcept { return options_.vsync; }
    void setAntiAliasing(Aporia::AntiAliasingMode mode) noexcept;
    [[nodiscard]] Aporia::AntiAliasingMode antiAliasing() const noexcept { return antiAliasing_; }
    void setAntiAliasingStrength(float strength) noexcept;
    [[nodiscard]] float antiAliasingStrength() const noexcept { return antiAliasingStrength_; }

#if defined(APORIA_EXPERIMENTAL)
    std::uint64_t createMaterial(const Aporia::MaterialDesc& description);
    bool updateMaterial(std::uint64_t handle, const Aporia::MaterialDesc& description);
    void destroyMaterial(std::uint64_t handle) noexcept;
    [[nodiscard]] bool materialValid(std::uint64_t handle) const noexcept;
#endif

    [[nodiscard]] Aporia::GraphicsStats stats() const noexcept { return stats_; }
    [[nodiscard]] const std::string& lastError() const noexcept { return lastError_; }

private:
    struct MeshResource {
        Aporia::Backend::BufferHandle vertices;
        Aporia::Backend::BufferHandle indices;
        std::uint32_t vertexCount = 0;
        std::uint32_t indexCount = 0;
    };

    struct LodGroup {
        std::vector<std::uint64_t> levels;
        std::vector<float> maxDistances;
        Aporia::LodBoundary boundary = Aporia::LodBoundary::Sphere;
        std::uint32_t currentLevel = 0;
        // Bounds of the highest detail level, in model space.
        float center[3] = {0.0f, 0.0f, 0.0f};
        float halfExtents[3] = {1.0f, 1.0f, 1.0f};
        float boundingRadius = 1.0f;
    };

    struct TextureResource {
        Aporia::Backend::TextureHandle texture;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint64_t uploadCount = 0;
    };

    struct FontResource {
        Aporia::Backend::TextureHandle texture;
        Aporia::UiTextAtlasDesc atlas;
        // The glyph table is searched once per character, so a direct lookup
        // replaces the linear scan over the whole atlas.
        std::unordered_map<std::uint32_t, std::size_t> glyphIndex;
    };

    bool createPipelines(std::string& error);
    bool createTargets(std::string& error);
    void destroyTargets() noexcept;
    bool resizeTargets(std::uint32_t width, std::uint32_t height, std::string& error);
    void drawItems(
        const CameraState& camera,
        const std::vector<RenderItem>& items,
        bool shadowPass,
        std::uint64_t& lodDrawTriangles,
        std::uint64_t& lodFullTriangles);
    // One ordered UI pass over the given layer range. `glassSource` is the
    // already composited image the glass rectangles sample; pass an invalid
    // handle to draw the rectangles solid.
    void drawUiPass(
        const Aporia::Backend::RenderTargetBinding& target,
        bool nativeFormat,
        int firstLayer,
        int lastLayer,
        Aporia::Backend::TextureHandle glassSource,
        bool glassEnabled);
    // Full screen blit used by the UI backdrop and the temporal history.
    void copyImage(
        Aporia::Backend::TextureHandle source,
        const Aporia::Backend::RenderTargetBinding& target,
        bool nativeFormat);
    // Separable Gaussian: blurs `source` into `destination` along one axis.
    void blurImage(
        Aporia::Backend::TextureHandle source,
        Aporia::Backend::TextureHandle destination,
        float directionX,
        float directionY,
        float radius);
    void writeSceneConstants(
        const CameraState& camera,
        const EnvironmentState& environment,
        const float sunMatrix[16]);
    void writeUiVertices(
        std::array<std::vector<float>, Aporia::kUiLayerCount>& rectVertices,
        std::array<std::vector<float>, Aporia::kUiLayerCount>& textVertices,
        const UiDrawList& ui);
    // Picks the level of a LOD group for one item and reports the triangles.
    [[nodiscard]] std::uint64_t resolveMesh(
        std::uint64_t meshId,
        const RenderItem& item,
        const CameraState& camera,
        std::uint64_t& drawTriangles,
        std::uint64_t& fullTriangles);

    std::unique_ptr<Aporia::Backend::GraphicsBackend> backend_;

    Aporia::Backend::ShaderHandle sceneVertexShader_{};
    Aporia::Backend::ShaderHandle sceneFragmentShader_{};
    Aporia::Backend::ShaderHandle shadowVertexShader_{};
    Aporia::Backend::ShaderHandle shadowFragmentShader_{};
    Aporia::Backend::ShaderHandle fullscreenVertexShader_{};
    Aporia::Backend::ShaderHandle skyFragmentShader_{};
    Aporia::Backend::ShaderHandle tonemapFragmentShader_{};
    Aporia::Backend::ShaderHandle uiVertexShader_{};
    Aporia::Backend::ShaderHandle uiTextVertexShader_{};
    Aporia::Backend::ShaderHandle uiRectFragmentShader_{};
    Aporia::Backend::ShaderHandle uiTextFragmentShader_{};
    Aporia::Backend::ShaderHandle aaFragmentShader_{};
    Aporia::Backend::ShaderHandle taaFragmentShader_{};
    Aporia::Backend::ShaderHandle copyFragmentShader_{};
    Aporia::Backend::ShaderHandle blurFragmentShader_{};

    Aporia::Backend::PipelineHandle meshPipeline_{};
    Aporia::Backend::PipelineHandle shadowPipeline_{};
    Aporia::Backend::PipelineHandle skyPipeline_{};
    Aporia::Backend::PipelineHandle tonemapPipeline_{};
    Aporia::Backend::PipelineHandle tonemapLdrPipeline_{};
    Aporia::Backend::PipelineHandle uiRectPipeline_{};
    Aporia::Backend::PipelineHandle uiRectLdrPipeline_{};
    Aporia::Backend::PipelineHandle uiTextPipeline_{};
    Aporia::Backend::PipelineHandle uiTextLdrPipeline_{};
    Aporia::Backend::PipelineHandle aaPipeline_{};
    Aporia::Backend::PipelineHandle taaPipeline_{};
    Aporia::Backend::PipelineHandle copyPipeline_{};
    Aporia::Backend::PipelineHandle copyLdrPipeline_{};
    Aporia::Backend::PipelineHandle blurPipeline_{};

    Aporia::Backend::SamplerHandle linearSampler_{};
    Aporia::Backend::SamplerHandle clampSampler_{};

    // White 1x1 texture: keeps the albedo slot valid when the host has no
    // texture of its own.
    Aporia::Backend::TextureHandle defaultTexture_{};

    Aporia::Backend::TextureHandle hdrTexture_{};
    Aporia::Backend::TextureHandle depthTexture_{};
    Aporia::Backend::TextureHandle shadowMapTexture_{};
    Aporia::Backend::TextureHandle shadowDepthTexture_{};
    Aporia::Backend::TextureHandle ldrTexture_{};
    // Tone mapped image the anti-aliasing passes read from.
    Aporia::Backend::TextureHandle resolveTexture_{};
    // Final image of a headless frame: what the host reads back.
    Aporia::Backend::TextureHandle outputTexture_{};
    // Temporal history, ping-ponged so the resolve never reads and writes
    // the same image.
    Aporia::Backend::TextureHandle historyTexture_[2]{};
    // UI compositing: the display image the glass rectangles sample and the
    // ping-pong target of the backdrop blur.
    Aporia::Backend::TextureHandle uiBackdropTexture_{};
    Aporia::Backend::TextureHandle uiBlurTexture_{};

    // UI geometry: one ring of vertex buffers so a frame never overwrites the
    // data the GPU is still reading.
    static constexpr std::uint32_t kUiBufferCount = 4;
    std::array<Aporia::Backend::BufferHandle, kUiBufferCount> uiRectBuffers_{};
    std::array<Aporia::Backend::BufferHandle, kUiBufferCount> uiTextBuffers_{};
    std::uint32_t uiBufferIndex_ = 0;
    // The UI vertex streams are rebuilt every frame; keeping the storage
    // between frames removes a multi-hundred-kilobyte allocation per frame.
    std::array<std::vector<float>, Aporia::kUiLayerCount> uiRectVertices_;
    std::array<std::vector<float>, Aporia::kUiLayerCount> uiTextVertices_;

    std::unordered_map<std::uint64_t, MeshResource> meshes_;
    std::unordered_map<std::uint64_t, LodGroup> lodGroups_;
    std::unordered_map<std::uint64_t, TextureResource> textures_;
    std::unordered_map<std::uint64_t, FontResource> fonts_;
    // Source vertices of every mesh, kept for the LOD bounds.
    std::unordered_map<std::uint64_t, std::vector<MeshVertex>> levelVertices_;
#if defined(APORIA_EXPERIMENTAL)
    std::unordered_map<std::uint64_t, Aporia::MaterialDesc> materials_;
#endif

    std::vector<LightDesc> lights_;
    UiDrawList uiDrawList_{};
    Aporia::GraphicsStats stats_{};
    Options options_{};
    std::string lastError_;
    std::uint64_t nextHandle_ = 1;
    std::uint64_t boundTexture_ = 0;
    // Uploads of the fallback white texture (one at creation).
    std::uint64_t defaultTextureUploads_ = 0;
    std::uint64_t lodSwitchCount_ = 0;
    // Texture slots the backend already holds, so a descriptor is only
    // rewritten when the bound texture actually changes.
    Aporia::Backend::TextureHandle boundAlbedo_{};
    Aporia::Backend::TextureHandle boundShadowMap_{};
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::uint32_t shadowMapSize_ = 0;
    Aporia::AntiAliasingMode antiAliasing_ = Aporia::AntiAliasingMode::None;
    float antiAliasingStrength_ = 0.0f;
    // Temporal anti-aliasing state: the jitter phase and the offset of the
    // previous frame, both in screen pixels.
    std::uint32_t jitterPhase_ = 0;
    float jitter_[2] = {0.0f, 0.0f};
    float previousJitter_[2] = {0.0f, 0.0f};
    int historyReadIndex_ = 0;
    bool historyValid_ = false;
    bool initialized_ = false;
};

}  // namespace Aporia::graphics_internal
