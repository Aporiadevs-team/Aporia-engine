// Backend contract shared by every graphics backend.
//
// Nothing here may mention D3D11, D3D12 or D3D9 types: the high-level
// renderer (lighting, shadows, materials, UI) talks to this interface only, and
// decides what to draw from BackendCapabilities instead of from the API name.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Aporia/Core/Export.hpp"

namespace Aporia::Backend {

enum class BackendKind : std::uint8_t { Auto, D3D12, D3D11, D3D9 };

APORIA_API const char* toString(BackendKind kind);
APORIA_API BackendKind backendKindFromString(const std::string& text);

enum class ShaderStage : std::uint8_t { Vertex, Fragment, Geometry, Compute };
enum class Topology : std::uint8_t { TriangleList, TriangleStrip, LineList, Points };
enum class CompareOp : std::uint8_t { Never, Less, Equal, LessEqual, Greater, NotEqual, GreaterEqual, Always };
enum class CullMode : std::uint8_t { None, Front, Back };
enum class BlendFactor : std::uint8_t {
    Zero, One, SourceColor, OneMinusSourceColor, SourceAlpha, OneMinusSourceAlpha,
    DestinationColor, OneMinusDestinationColor, DestinationAlpha, OneMinusDestinationAlpha,
    SourceAlphaSaturate
};
enum class BlendOp : std::uint8_t { Add, Subtract, ReverseSubtract, Min, Max };
enum class FilterMode : std::uint8_t { Nearest, Linear };
enum class AddressMode : std::uint8_t { Wrap, Clamp, Mirror };
enum class VertexFormat : std::uint8_t { Float32, Float32x2, Float32x3, Float32x4, Uint32, Uint8x4Unorm };
enum class PixelFormat : std::uint8_t {
    Rgba8Unorm, Bgra8Unorm, Rgba16Float, Rgba32Float, Bgra10A2Unorm, R32Float, Rg16Float,
    Depth32Float, Depth24Unorm, Depth16Unorm
};
enum class IndexFormat : std::uint8_t { Uint16, Uint32 };

enum class TextureUsage : std::uint32_t {
    ShaderResource = 1u << 0,
    RenderTarget = 1u << 1,
    DepthStencil = 1u << 2,
    Dynamic = 1u << 3
};

enum class BufferUsage : std::uint32_t {
    Vertex = 1u << 0,
    Index = 1u << 1,
    Constant = 1u << 2,
    ShaderResource = 1u << 3,
    RenderTarget = 1u << 4,
    DepthStencil = 1u << 5,
    // The host rewrites the content every frame. The Direct3D backends keep
    // every buffer in an upload heap, so the flag only documents the intent.
    Dynamic = 1u << 6
};

constexpr TextureUsage operator|(TextureUsage left, TextureUsage right) noexcept {
    return static_cast<TextureUsage>(static_cast<std::uint32_t>(left) |
                                     static_cast<std::uint32_t>(right));
}

constexpr TextureUsage operator&(TextureUsage left, TextureUsage right) noexcept {
    return static_cast<TextureUsage>(static_cast<std::uint32_t>(left) &
                                     static_cast<std::uint32_t>(right));
}

constexpr BufferUsage operator|(BufferUsage left, BufferUsage right) noexcept {
    return static_cast<BufferUsage>(static_cast<std::uint32_t>(left) |
                                     static_cast<std::uint32_t>(right));
}

constexpr BufferUsage operator&(BufferUsage left, BufferUsage right) noexcept {
    return static_cast<BufferUsage>(static_cast<std::uint32_t>(left) &
                                     static_cast<std::uint32_t>(right));
}

constexpr bool operator==(TextureUsage left, TextureUsage right) noexcept {
    return static_cast<std::uint32_t>(left) == static_cast<std::uint32_t>(right);
}

constexpr bool operator!=(TextureUsage left, TextureUsage right) noexcept {
    return !(left == right);
}

constexpr bool operator==(BufferUsage left, BufferUsage right) noexcept {
    return static_cast<std::uint32_t>(left) == static_cast<std::uint32_t>(right);
}

constexpr bool operator!=(BufferUsage left, BufferUsage right) noexcept {
    return !(left == right);
}

struct BufferHandle {
    std::uint64_t value = 0;
    bool valid() const { return value != 0; }
    friend bool operator==(BufferHandle, BufferHandle) = default;
};
struct TextureHandle {
    std::uint64_t value = 0;
    bool valid() const { return value != 0; }
    friend bool operator==(TextureHandle, TextureHandle) = default;
};
struct SamplerHandle {
    std::uint64_t value = 0;
    bool valid() const { return value != 0; }
    friend bool operator==(SamplerHandle, SamplerHandle) = default;
};
struct ShaderHandle {
    std::uint64_t value = 0;
    bool valid() const { return value != 0; }
    friend bool operator==(ShaderHandle, ShaderHandle) = default;
};
struct PipelineHandle {
    std::uint64_t value = 0;
    bool valid() const { return value != 0; }
    friend bool operator==(PipelineHandle, PipelineHandle) = default;
};

// What a backend can actually do. The renderer reads this instead of asking
// which API it runs on, so a D3D9 path and a D3D12 path share one renderer.
struct BackendCapabilities {
    BackendKind kind = BackendKind::Auto;
    int apiVersion = 0;      // 12, 11 or 9
    int shaderModel = 0;     // 6, 5 or 3
    bool computeShaders = false;
    bool geometryShaders = false;
    bool tessellation = false;
    bool textureLod = false;
    bool instancing = true;
    bool depthStencil = true;
    bool shadowMaps = true;
    bool multisampling = false;
    bool srgbOutput = false;
    bool dynamicUniforms = false;
    std::uint32_t maxRenderTargets = 1;
    std::uint32_t maxTextureSize = 0;
    std::uint32_t maxConstantBufferSize = 0;
    std::uint32_t maxVertexAttributes = 16;
};

struct BackendConfig {
    std::uintptr_t nativeWindow = 0;
    std::uint32_t width = 1280;
    std::uint32_t height = 720;
    bool vsync = true;
    bool validation = false;
};

struct SwapchainDesc {
    std::uintptr_t nativeWindow = 0;
    std::uint32_t width = 1280;
    std::uint32_t height = 720;
    bool vsync = true;
    PixelFormat format = PixelFormat::Bgra8Unorm;
};

struct BufferDesc {
    std::size_t size = 0;
    BufferUsage usage = BufferUsage::Vertex;
    std::vector<std::uint8_t> data;  // optional initial upload
};

struct TextureDesc {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t depth = 1;
    std::uint32_t mipLevels = 1;
    PixelFormat format = PixelFormat::Rgba8Unorm;
    TextureUsage usage = TextureUsage::ShaderResource;
    std::vector<std::uint8_t> data;  // optional initial upload
};

struct SamplerDesc {
    FilterMode filter = FilterMode::Linear;
    AddressMode address = AddressMode::Wrap;
};

struct VertexAttribute {
    // HLSL semantic index of the attribute (ATTRIBUTE0, ATTRIBUTE1, ...). Every
    // attribute is read from vertex buffer 0, so the mesh layout is interleaved.
    std::uint32_t slot = 0;
    std::uint32_t offset = 0;
    VertexFormat format = VertexFormat::Float32x3;
};

struct ShaderDesc {
    ShaderStage stage = ShaderStage::Vertex;
    std::string source;      // HLSL
    std::string entryPoint = "main";
    bool requiresGeometryStage = false;
};

struct RenderTargetDesc {
    PixelFormat format = PixelFormat::Bgra8Unorm;
    bool blendEnable = false;
    BlendOp blendOp = BlendOp::Add;
    BlendFactor srcColor = BlendFactor::One;
    BlendFactor dstColor = BlendFactor::Zero;
    BlendFactor srcAlpha = BlendFactor::One;
    BlendFactor dstAlpha = BlendFactor::Zero;
    bool writeAlpha = true;
};

struct PipelineDesc {
    ShaderHandle vertexShader{};
    ShaderHandle fragmentShader{};
    std::vector<VertexAttribute> attributes;
    std::uint32_t vertexStride = 0;
    Topology topology = Topology::TriangleList;
    CullMode cull = CullMode::Back;
    bool depthTest = true;
    CompareOp depthCompare = CompareOp::Less;
    bool depthWrite = true;
    bool hasDepth = false;
    PixelFormat depthFormat = PixelFormat::Depth32Float;
    std::vector<RenderTargetDesc> renderTargets;
};

struct RenderTargetBinding {
    enum class Source : std::uint8_t { Swapchain, Texture };
    Source source = Source::Texture;
    TextureHandle texture{};
    std::uint32_t mip = 0;
    std::uint32_t arraySlice = 0;
};

struct Viewport {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float minDepth = 0.0f;
    float maxDepth = 1.0f;
};

struct ScissorRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct ClearValue {
    float color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float depth = 1.0f;
};

struct RenderPassDesc {
    std::vector<RenderTargetBinding> colorTargets;
    RenderTargetBinding depthTarget{};
    bool hasDepth = false;
    bool clearColorEnabled = true;
    ClearValue clear{};
};

class GraphicsBackend {
public:
    virtual ~GraphicsBackend() = default;

    virtual BackendKind kind() const = 0;
    virtual const BackendCapabilities& capabilities() const = 0;

    virtual bool initialize(const BackendConfig& config, std::string& error) = 0;
    virtual void shutdown() = 0;

    virtual bool createSwapchain(const SwapchainDesc& desc, std::string& error) = 0;
    virtual void resizeSwapchain(std::uint32_t width, std::uint32_t height) = 0;
    virtual void setVsync(bool enabled) = 0;

    virtual bool beginFrame(std::string& error) = 0;
    virtual void beginRenderPass(const RenderPassDesc& desc) = 0;
    virtual void endRenderPass() = 0;
    virtual bool endFrame(std::string& error) = 0;
    // Blocks until the GPU finished every command submitted so far.
    virtual void waitIdle() = 0;

    virtual void setPipeline(PipelineHandle pipeline) = 0;
    virtual void setViewport(const Viewport& viewport) = 0;
    virtual void setScissor(const ScissorRect& scissor) = 0;
    virtual void bindVertexBuffer(std::uint32_t slot, BufferHandle buffer, std::uint32_t offset) = 0;
    virtual void bindIndexBuffer(BufferHandle buffer, IndexFormat format) = 0;
    virtual void setConstantBuffer(std::uint32_t slot, const void* data, std::size_t size) = 0;
    virtual void setTexture(std::uint32_t slot, TextureHandle texture) = 0;
    virtual void setSampler(std::uint32_t slot, SamplerHandle sampler) = 0;
    virtual void draw(std::uint32_t vertexCount, std::uint32_t instanceCount = 1) = 0;
    virtual void drawIndexed(std::uint32_t indexCount, std::uint32_t instanceCount = 1) = 0;

    virtual BufferHandle createBuffer(const BufferDesc& desc) = 0;
    virtual void destroyBuffer(BufferHandle buffer) = 0;
    // Rewrites the content of a buffer created with BufferUsage::Dynamic.
    virtual bool updateBuffer(BufferHandle buffer, const void* data, std::size_t size) = 0;
    virtual TextureHandle createTexture(const TextureDesc& desc) = 0;
    virtual void destroyTexture(TextureHandle texture) = 0;
    virtual SamplerHandle createSampler(const SamplerDesc& desc) = 0;
    virtual void destroySampler(SamplerHandle sampler) = 0;
    virtual ShaderHandle createShader(const ShaderDesc& desc, std::string& error) = 0;
    virtual void destroyShader(ShaderHandle shader) = 0;
    virtual PipelineHandle createPipeline(const PipelineDesc& desc, std::string& error) = 0;
    virtual void destroyPipeline(PipelineHandle pipeline) = 0;

    // Reads back pixels of a texture (used by tests and by the HDR resolve).
    virtual bool readPixels(
        TextureHandle texture, std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height,
        std::vector<std::uint8_t>& out) = 0;
};

// Creates a backend by kind. Returns nullptr when the backend is not compiled in.
APORIA_API std::unique_ptr<GraphicsBackend> createBackend(BackendKind kind);

}  // namespace Aporia::Backend
