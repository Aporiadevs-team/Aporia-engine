#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "Graphics/DirectX/DirectXRenderer.hpp"

#include "AporiaHlslShaders.hpp"
#include "Aporia/Math/Matrix4.hpp"
#include "Graphics/Shadows/SunMatrix.hpp"
#include "Graphics/Support/Log.hpp"
#include "Graphics/Text/FontAtlasBuilder.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace Aporia::graphics_internal {
namespace {

using Backend::BackendKind;
using Backend::BufferDesc;
using Backend::BufferHandle;
using Backend::BufferUsage;
using Backend::CompareOp;
using Backend::CullMode;
using Backend::FilterMode;
using Backend::AddressMode;
using Backend::IndexFormat;
using Backend::PixelFormat;
using Backend::PipelineDesc;
using Backend::PipelineHandle;
using Backend::RenderPassDesc;
using Backend::RenderTargetBinding;
using Backend::RenderTargetDesc;
using Backend::SamplerDesc;
using Backend::SamplerHandle;
using Backend::ShaderDesc;
using Backend::ShaderHandle;
using Backend::ShaderStage;
using Backend::TextureDesc;
using Backend::TextureHandle;
using Backend::TextureUsage;
using Backend::VertexAttribute;
using Backend::VertexFormat;
using Backend::Viewport;

// Scene mesh vertex: position, normal, uv.
constexpr std::uint32_t kVertexStride = 32;
// UI rectangle: position, local offset, colour, shape.
constexpr std::uint32_t kUiRectStride = 48;
// UI glyph: position, uv, colour.
constexpr std::uint32_t kUiTextStride = 32;
constexpr std::size_t kUiBufferBytes = 1024u * 1024u;
constexpr std::uint32_t kMaxLights = 16;
constexpr std::uint32_t kMaxUiVertices = 16384;

// Must match the SceneConstants block in shaders/dx12/engine.hlsl.
struct SceneConstants {
    float viewProjection[16];
    float sunViewProjection[16];
    float sunDirection[4];
    float cameraPosition[4];
    float shadingParams[4];
    float screenParams[4];
    float fogColor[4];
    float fogParams[4];
    float cameraRight[4];
    float cameraUp[4];
    float cameraForward[4];
    float lightPosition[kMaxLights][4];
    float lightDirection[kMaxLights][4];
    float lightColor[kMaxLights][4];
    float lightParams[kMaxLights][4];
    float uiParams[4];
    float jitter[4];
    float aaParams[4];
    // x: PCF kernel radius in texels, y: filter (0 = PCF, 1 = PCSS),
    // z: light size in world units (penumbra estimate), w: unused.
    float shadowParams[4];
};

// Must match the ObjectConstants block in shaders/dx12/engine.hlsl.
struct ObjectConstants {
    float model[16];
    float color[4];
    float material[4];
};

// Must match the UiConstants block in shaders/dx12/engine.hlsl.
struct UiConstants {
    float glassMode = 0.0f;
    float intensity = 1.0f;
    float radius = 0.0f;
    float padding = 0.0f;
};

// Must match the BlurConstants block in shaders/dx12/engine.hlsl.
struct BlurConstants {
    float directionX = 1.0f;
    float directionY = 0.0f;
    float radius = 4.0f;
    float padding = 0.0f;
};

std::uint32_t decodeUtf8(std::string_view text, std::size_t& index) {
    if (index >= text.size()) {
        return 0;
    }
    const auto first = static_cast<unsigned char>(text[index]);
    std::uint32_t code = 0;
    std::size_t extra = 0;
    if (first < 0x80) {
        code = first;
    } else if ((first & 0xE0) == 0xC0) {
        code = first & 0x1Fu;
        extra = 1;
    } else if ((first & 0xF0) == 0xE0) {
        code = first & 0x0Fu;
        extra = 2;
    } else if ((first & 0xF8) == 0xF0) {
        code = first & 0x07u;
        extra = 3;
    } else {
        ++index;
        return 0xFFFDu;
    }
    if (index + extra >= text.size()) {
        ++index;
        return 0xFFFDu;
    }
    ++index;
    for (std::size_t step = 0; step < extra; ++step) {
        code = (code << 6) | (static_cast<unsigned char>(text[index]) & 0x3Fu);
        ++index;
    }
    return code;
}

const UiGlyph* findGlyph(const UiTextAtlasDesc& atlas, std::uint32_t codePoint) {
    for (const UiGlyph& glyph : atlas.glyphs) {
        if (glyph.codePoint == codePoint) {
            return &glyph;
        }
    }
    return nullptr;
}

// Builds the direct lookup the UI text pass uses: the atlas holds hundreds of
// glyphs, so a per-character linear scan would dominate a text heavy frame.
void buildGlyphIndex(
    const UiTextAtlasDesc& atlas, std::unordered_map<std::uint32_t, std::size_t>& index) {
    index.clear();
    index.reserve(atlas.glyphs.size() * 2);
    for (std::size_t position = 0; position < atlas.glyphs.size(); ++position) {
        index.emplace(atlas.glyphs[position].codePoint, position);
    }
}

float lineHeightFor(const UiTextAtlasDesc& atlas, float size) {
    const float em = atlas.emSize > 0.0f ? atlas.emSize : 32.0f;
    return atlas.lineHeight * (size / em);
}

// Column-major 4x4 multiply: out = a * b.
void multiplyMat4(const float* a, const float* b, float* out) {
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int index = 0; index < 4; ++index) {
                sum += a[index * 4 + row] * b[column * 4 + index];
            }
            out[column * 4 + row] = sum;
        }
    }
}

void normalize3(const float* in, float* out) {
    const float length = std::sqrt(in[0] * in[0] + in[1] * in[1] + in[2] * in[2]);
    if (length <= 1.0e-6f) {
        out[0] = 0.0f;
        out[1] = -1.0f;
        out[2] = 0.0f;
        return;
    }
    out[0] = in[0] / length;
    out[1] = in[1] / length;
    out[2] = in[2] / length;
}

std::uint32_t shadowMapSizeFor(Aporia::GraphicsQuality quality) {
    switch (quality) {
        case Aporia::GraphicsQuality::Low: return 512;
        case Aporia::GraphicsQuality::Medium: return 1024;
        case Aporia::GraphicsQuality::High: return 2048;
    }
    return 1024;
}

#if defined(_WIN32)
void windowClientSize(std::uintptr_t nativeWindow, std::uint32_t& width, std::uint32_t& height) {
    RECT client{};
    if (nativeWindow == 0 || !GetClientRect(reinterpret_cast<HWND>(nativeWindow), &client)) {
        return;
    }
    const long clientWidth = client.right - client.left;
    const long clientHeight = client.bottom - client.top;
    if (clientWidth > 0 && clientHeight > 0) {
        width = static_cast<std::uint32_t>(clientWidth);
        height = static_cast<std::uint32_t>(clientHeight);
    }
}
#endif

}  // namespace

DirectXRenderer::DirectXRenderer() = default;

DirectXRenderer::~DirectXRenderer() {
    shutdown();
}

bool DirectXRenderer::initialize(const Options& options, std::string& error) {
    shutdown();
    options_ = options;
    lastError_.clear();

    BackendKind kind = options.backend;
    if (kind == BackendKind::Auto) {
#if defined(APORIA_ENABLE_D3D12)
        kind = BackendKind::D3D12;
#else
        error = "No Direct3D backend is compiled into this build";
        return false;
#endif
    }
    backend_ = Backend::createBackend(kind);
    if (!backend_) {
        error = std::string("The ") + Backend::toString(kind) + " backend is not available";
        return false;
    }

    width_ = options_.headless ? options_.headlessWidth : options_.width;
    height_ = options_.headless ? options_.headlessHeight : options_.height;
#if defined(_WIN32)
    if (!options_.headless) {
        windowClientSize(options_.nativeWindow, width_, height_);
    }
#endif
    if (width_ == 0) {
        width_ = 1;
    }
    if (height_ == 0) {
        height_ = 1;
    }

    Backend::BackendConfig config;
    config.nativeWindow = options_.nativeWindow;
    config.width = width_;
    config.height = height_;
    config.vsync = options_.vsync;
    config.validation = options_.validation;
    if (!backend_->initialize(config, error)) {
        backend_.reset();
        return false;
    }

    if (!options_.headless) {
        Backend::SwapchainDesc swap;
        swap.nativeWindow = options_.nativeWindow;
        swap.width = width_;
        swap.height = height_;
        swap.vsync = options_.vsync;
        swap.format = PixelFormat::Bgra8Unorm;
        if (!backend_->createSwapchain(swap, error)) {
            backend_->shutdown();
            backend_.reset();
            return false;
        }
    }

    SamplerDesc linear;
    linear.filter = FilterMode::Linear;
    linear.address = AddressMode::Wrap;
    linearSampler_ = backend_->createSampler(linear);
    SamplerDesc clamp;
    clamp.filter = FilterMode::Linear;
    clamp.address = AddressMode::Clamp;
    clampSampler_ = backend_->createSampler(clamp);

    // White fallback so the albedo slot is always valid.
    TextureDesc white;
    white.width = 1;
    white.height = 1;
    white.format = PixelFormat::Rgba8Unorm;
    white.usage = TextureUsage::ShaderResource;
    white.data = {255, 255, 255, 255};
    defaultTexture_ = backend_->createTexture(white);
    defaultTextureUploads_ = defaultTexture_.valid() ? 1u : 0u;

    shadowMapSize_ = options_.shadows.mapSize > 0 ? options_.shadows.mapSize
                                                  : shadowMapSizeFor(options_.quality);

    if (!createPipelines(error)) {
        shutdown();
        return false;
    }
    if (!createTargets(error)) {
        shutdown();
        return false;
    }

    initialized_ = true;
    stats_ = Aporia::GraphicsStats{};
    stats_.headless = options_.headless;
    stats_.nativeWindow = !options_.headless;
    stats_.surfaceWidth = width_;
    stats_.surfaceHeight = height_;
    stats_.deviceName = std::string("Direct3D ") + std::to_string(backend_->capabilities().apiVersion);
    stats_.apiVersion = static_cast<std::uint32_t>(backend_->capabilities().apiVersion);
    stats_.quality = static_cast<std::uint32_t>(options_.quality);
    stats_.hdrEnabled = true;
    stats_.shadowMapSize = shadowMapSize_;
    stats_.antiAliasing = static_cast<std::uint32_t>(antiAliasing_);
    stats_.antiAliasingStrength = antiAliasingStrength_;
    return true;
}

void DirectXRenderer::shutdown() noexcept {
    if (!backend_) {
        initialized_ = false;
        return;
    }
    backend_->waitIdle();

    destroyTargets();
    for (auto& entry : meshes_) {
        backend_->destroyBuffer(entry.second.vertices);
        backend_->destroyBuffer(entry.second.indices);
    }
    meshes_.clear();
    lodGroups_.clear();
    levelVertices_.clear();
    for (auto& entry : textures_) {
        backend_->destroyTexture(entry.second.texture);
    }
    textures_.clear();
    for (auto& entry : fonts_) {
        backend_->destroyTexture(entry.second.texture);
    }
    fonts_.clear();
#if defined(APORIA_EXPERIMENTAL)
    materials_.clear();
#endif

    if (sceneVertexShader_.valid()) backend_->destroyShader(sceneVertexShader_);
    if (sceneFragmentShader_.valid()) backend_->destroyShader(sceneFragmentShader_);
    if (shadowVertexShader_.valid()) backend_->destroyShader(shadowVertexShader_);
    if (shadowFragmentShader_.valid()) backend_->destroyShader(shadowFragmentShader_);
    if (fullscreenVertexShader_.valid()) backend_->destroyShader(fullscreenVertexShader_);
    if (skyFragmentShader_.valid()) backend_->destroyShader(skyFragmentShader_);
    if (tonemapFragmentShader_.valid()) backend_->destroyShader(tonemapFragmentShader_);
    if (uiVertexShader_.valid()) backend_->destroyShader(uiVertexShader_);
    if (uiTextVertexShader_.valid()) backend_->destroyShader(uiTextVertexShader_);
    if (uiRectFragmentShader_.valid()) backend_->destroyShader(uiRectFragmentShader_);
    if (uiTextFragmentShader_.valid()) backend_->destroyShader(uiTextFragmentShader_);
    if (aaFragmentShader_.valid()) backend_->destroyShader(aaFragmentShader_);
    if (taaFragmentShader_.valid()) backend_->destroyShader(taaFragmentShader_);
    if (copyFragmentShader_.valid()) backend_->destroyShader(copyFragmentShader_);
    if (blurFragmentShader_.valid()) backend_->destroyShader(blurFragmentShader_);
    sceneVertexShader_ = {};
    sceneFragmentShader_ = {};
    shadowVertexShader_ = {};
    shadowFragmentShader_ = {};
    fullscreenVertexShader_ = {};
    skyFragmentShader_ = {};
    tonemapFragmentShader_ = {};
    uiVertexShader_ = {};
    uiTextVertexShader_ = {};
    uiRectFragmentShader_ = {};
    uiTextFragmentShader_ = {};
    aaFragmentShader_ = {};
    taaFragmentShader_ = {};
    copyFragmentShader_ = {};
    blurFragmentShader_ = {};

    if (meshPipeline_.valid()) backend_->destroyPipeline(meshPipeline_);
    if (shadowPipeline_.valid()) backend_->destroyPipeline(shadowPipeline_);
    if (skyPipeline_.valid()) backend_->destroyPipeline(skyPipeline_);
    if (tonemapPipeline_.valid()) backend_->destroyPipeline(tonemapPipeline_);
    if (tonemapLdrPipeline_.valid()) backend_->destroyPipeline(tonemapLdrPipeline_);
    if (uiRectPipeline_.valid()) backend_->destroyPipeline(uiRectPipeline_);
    if (uiRectLdrPipeline_.valid()) backend_->destroyPipeline(uiRectLdrPipeline_);
    if (uiTextPipeline_.valid()) backend_->destroyPipeline(uiTextPipeline_);
    if (uiTextLdrPipeline_.valid()) backend_->destroyPipeline(uiTextLdrPipeline_);
    meshPipeline_ = {};
    shadowPipeline_ = {};
    skyPipeline_ = {};
    tonemapPipeline_ = {};
    tonemapLdrPipeline_ = {};
    uiRectPipeline_ = {};
    uiRectLdrPipeline_ = {};
    uiTextPipeline_ = {};
    uiTextLdrPipeline_ = {};

    if (linearSampler_.valid()) backend_->destroySampler(linearSampler_);
    if (clampSampler_.valid()) backend_->destroySampler(clampSampler_);
    linearSampler_ = {};
    clampSampler_ = {};

    if (defaultTexture_.valid()) backend_->destroyTexture(defaultTexture_);
    defaultTexture_ = {};

    backend_->shutdown();
    backend_.reset();
    initialized_ = false;
}

bool DirectXRenderer::createPipelines(std::string& error) {
    const std::string source = aporia::generated::aporiaEngineHlsl();
    const auto makeShader = [&](ShaderStage stage, const char* entry, ShaderHandle& out) {
        ShaderDesc desc;
        desc.stage = stage;
        desc.source = source;
        desc.entryPoint = entry;
        std::string shaderError;
        out = backend_->createShader(desc, shaderError);
        if (!out.valid()) {
            error = std::string("shader ") + entry + " failed: " + shaderError;
            return false;
        }
        return true;
    };

    if (!makeShader(ShaderStage::Vertex, "SceneVS", sceneVertexShader_)) return false;
    if (!makeShader(ShaderStage::Fragment, "ScenePS", sceneFragmentShader_)) return false;
    if (!makeShader(ShaderStage::Vertex, "ShadowVS", shadowVertexShader_)) return false;
    if (!makeShader(ShaderStage::Fragment, "ShadowPS", shadowFragmentShader_)) return false;
    if (!makeShader(ShaderStage::Vertex, "FullscreenVS", fullscreenVertexShader_)) return false;
    if (!makeShader(ShaderStage::Fragment, "SkyPS", skyFragmentShader_)) return false;
    if (!makeShader(ShaderStage::Fragment, "TonemapPS", tonemapFragmentShader_)) return false;
    if (!makeShader(ShaderStage::Vertex, "UiVS", uiVertexShader_)) return false;
    if (!makeShader(ShaderStage::Vertex, "UiTextVS", uiTextVertexShader_)) return false;
    if (!makeShader(ShaderStage::Fragment, "UiRectPS", uiRectFragmentShader_)) return false;
    if (!makeShader(ShaderStage::Fragment, "UiTextPS", uiTextFragmentShader_)) return false;
    if (!makeShader(ShaderStage::Fragment, "AaPs", aaFragmentShader_)) return false;
    if (!makeShader(ShaderStage::Fragment, "TaaPs", taaFragmentShader_)) return false;
    if (!makeShader(ShaderStage::Fragment, "CopyPS", copyFragmentShader_)) return false;
    if (!makeShader(ShaderStage::Fragment, "BlurPS", blurFragmentShader_)) return false;

    const std::vector<VertexAttribute> meshAttributes{
        {0, 0, VertexFormat::Float32x3},
        {1, 12, VertexFormat::Float32x3},
        {2, 24, VertexFormat::Float32x2},
    };

    PipelineDesc mesh;
    mesh.vertexShader = sceneVertexShader_;
    mesh.fragmentShader = sceneFragmentShader_;
    mesh.attributes = meshAttributes;
    mesh.vertexStride = kVertexStride;
    // The engine builds the matrices for Direct3D clip space and the vertex
    // stage does not mirror Y, so the engine winding reaches the rasterizer
    // unchanged. The primitives are wound counter-clockwise in NDC (their
    // right-hand normal points outwards), and the D3D viewport flips Y, so
    // they arrive clockwise on screen: that is the D3D front face. Culling
    // the back faces keeps the visible side of the mesh.
    mesh.cull = CullMode::Back;
    mesh.depthTest = true;
    mesh.depthCompare = CompareOp::LessEqual;
    mesh.depthWrite = true;
    mesh.hasDepth = true;
    mesh.depthFormat = PixelFormat::Depth32Float;
    mesh.renderTargets = {RenderTargetDesc{PixelFormat::Rgba16Float}};
    meshPipeline_ = backend_->createPipeline(mesh, error);
    if (!meshPipeline_.valid()) {
        error = "mesh pipeline: " + error;
        return false;
    }

    PipelineDesc shadow;
    shadow.vertexShader = shadowVertexShader_;
    shadow.fragmentShader = shadowFragmentShader_;
    shadow.attributes = meshAttributes;
    shadow.vertexStride = kVertexStride;
    shadow.cull = CullMode::Back;
    shadow.depthTest = true;
    shadow.depthCompare = CompareOp::LessEqual;
    shadow.depthWrite = true;
    shadow.hasDepth = true;
    shadow.depthFormat = PixelFormat::Depth32Float;
    shadow.renderTargets = {RenderTargetDesc{PixelFormat::R32Float}};
    shadowPipeline_ = backend_->createPipeline(shadow, error);
    if (!shadowPipeline_.valid()) {
        error = "shadow pipeline: " + error;
        return false;
    }

    PipelineDesc sky;
    sky.vertexShader = fullscreenVertexShader_;
    sky.fragmentShader = skyFragmentShader_;
    sky.vertexStride = 0;
    sky.cull = CullMode::None;
    sky.depthTest = false;
    sky.depthWrite = false;
    sky.hasDepth = true;
    sky.depthFormat = PixelFormat::Depth32Float;
    sky.renderTargets = {RenderTargetDesc{PixelFormat::Rgba16Float}};
    skyPipeline_ = backend_->createPipeline(sky, error);
    if (!skyPipeline_.valid()) {
        error = "sky pipeline: " + error;
        return false;
    }

    const auto makeTonemap = [&](PixelFormat format) {
        PipelineDesc desc;
        desc.vertexShader = fullscreenVertexShader_;
        desc.fragmentShader = tonemapFragmentShader_;
        desc.vertexStride = 0;
        desc.cull = CullMode::None;
        desc.depthTest = false;
        desc.depthWrite = false;
        desc.hasDepth = false;
        desc.renderTargets = {RenderTargetDesc{format}};
        return backend_->createPipeline(desc, error);
    };
    tonemapPipeline_ = makeTonemap(PixelFormat::Bgra8Unorm);
    if (!tonemapPipeline_.valid()) {
        error = "tonemap pipeline: " + error;
        return false;
    }
    tonemapLdrPipeline_ = makeTonemap(PixelFormat::Rgba8Unorm);
    if (!tonemapLdrPipeline_.valid()) {
        error = "tonemap (ldr) pipeline: " + error;
        return false;
    }

    // Anti-aliasing and the final copy: all of them read one full screen
    // texture and write another one.
    const auto makeFullscreen = [&](ShaderHandle shader, PixelFormat format) {
        PipelineDesc desc;
        desc.vertexShader = fullscreenVertexShader_;
        desc.fragmentShader = shader;
        desc.vertexStride = 0;
        desc.cull = CullMode::None;
        desc.depthTest = false;
        desc.depthWrite = false;
        desc.hasDepth = false;
        desc.renderTargets = {RenderTargetDesc{format}};
        return backend_->createPipeline(desc, error);
    };

    aaPipeline_ = makeFullscreen(aaFragmentShader_, PixelFormat::Rgba8Unorm);
    if (!aaPipeline_.valid()) {
        error = "anti-aliasing pipeline: " + error;
        return false;
    }
    taaPipeline_ = makeFullscreen(taaFragmentShader_, PixelFormat::Rgba8Unorm);
    if (!taaPipeline_.valid()) {
        error = "temporal anti-aliasing pipeline: " + error;
        return false;
    }
    copyPipeline_ = makeFullscreen(copyFragmentShader_, PixelFormat::Bgra8Unorm);
    if (!copyPipeline_.valid()) {
        error = "copy pipeline: " + error;
        return false;
    }
    copyLdrPipeline_ = makeFullscreen(copyFragmentShader_, PixelFormat::Rgba8Unorm);
    if (!copyLdrPipeline_.valid()) {
        error = "copy (ldr) pipeline: " + error;
        return false;
    }
    blurPipeline_ = makeFullscreen(blurFragmentShader_, PixelFormat::Rgba8Unorm);
    if (!blurPipeline_.valid()) {
        error = "blur pipeline: " + error;
        return false;
    }

    RenderTargetDesc uiTarget;
    uiTarget.blendEnable = true;
    uiTarget.blendOp = Backend::BlendOp::Add;
    uiTarget.srcColor = Backend::BlendFactor::SourceAlpha;
    uiTarget.dstColor = Backend::BlendFactor::OneMinusSourceAlpha;
    uiTarget.srcAlpha = Backend::BlendFactor::One;
    uiTarget.dstAlpha = Backend::BlendFactor::OneMinusSourceAlpha;

    const auto makeUiRect = [&](PixelFormat format) {
        PipelineDesc desc;
        desc.vertexShader = uiVertexShader_;
        desc.fragmentShader = uiRectFragmentShader_;
        desc.attributes = {
            {0, 0, VertexFormat::Float32x2},
            {1, 8, VertexFormat::Float32x2},
            {2, 16, VertexFormat::Float32x4},
            {3, 32, VertexFormat::Float32x4},
        };
        desc.vertexStride = kUiRectStride;
        desc.cull = CullMode::None;
        desc.depthTest = false;
        desc.depthWrite = false;
        desc.hasDepth = false;
        desc.renderTargets = {uiTarget};
        desc.renderTargets[0].format = format;
        return backend_->createPipeline(desc, error);
    };
    uiRectPipeline_ = makeUiRect(PixelFormat::Bgra8Unorm);
    if (!uiRectPipeline_.valid()) {
        error = "ui rect pipeline: " + error;
        return false;
    }
    uiRectLdrPipeline_ = makeUiRect(PixelFormat::Rgba8Unorm);
    if (!uiRectLdrPipeline_.valid()) {
        error = "ui rect (ldr) pipeline: " + error;
        return false;
    }

    const auto makeUiText = [&](PixelFormat format) {
        PipelineDesc desc;
        desc.vertexShader = uiTextVertexShader_;
        desc.fragmentShader = uiTextFragmentShader_;
        desc.attributes = {
            {0, 0, VertexFormat::Float32x2},
            {1, 8, VertexFormat::Float32x2},
            {2, 16, VertexFormat::Float32x4},
        };
        desc.vertexStride = kUiTextStride;
        desc.cull = CullMode::None;
        desc.depthTest = false;
        desc.depthWrite = false;
        desc.hasDepth = false;
        desc.renderTargets = {uiTarget};
        desc.renderTargets[0].format = format;
        return backend_->createPipeline(desc, error);
    };
    uiTextPipeline_ = makeUiText(PixelFormat::Bgra8Unorm);
    if (!uiTextPipeline_.valid()) {
        error = "ui text pipeline: " + error;
        return false;
    }
    uiTextLdrPipeline_ = makeUiText(PixelFormat::Rgba8Unorm);
    if (!uiTextLdrPipeline_.valid()) {
        error = "ui text (ldr) pipeline: " + error;
        return false;
    }
    return true;
}

bool DirectXRenderer::createTargets(std::string& error) {
    // The targets of the previous frame may still be in flight: releasing them
    // before the GPU is done leaves the queue reading freed memory, which the
    // driver reports as a hung device.
    if (backend_) {
        backend_->waitIdle();
    }
    destroyTargets();

    TextureDesc hdr;
    hdr.width = width_;
    hdr.height = height_;
    hdr.format = PixelFormat::Rgba16Float;
    hdr.usage = TextureUsage::RenderTarget | TextureUsage::ShaderResource;
    hdrTexture_ = backend_->createTexture(hdr);
    if (!hdrTexture_.valid()) {
        error = "failed to create the HDR target";
        return false;
    }

    TextureDesc depth;
    depth.width = width_;
    depth.height = height_;
    depth.format = PixelFormat::Depth32Float;
    depth.usage = TextureUsage::DepthStencil;
    depthTexture_ = backend_->createTexture(depth);
    if (!depthTexture_.valid()) {
        error = "failed to create the depth target";
        return false;
    }

    if (shadowMapSize_ > 0) {
        // The shadow map is a colour target that holds the light space depth:
        // a depth buffer cannot be sampled as a plain texture.
        TextureDesc shadow;
        shadow.width = shadowMapSize_;
        shadow.height = shadowMapSize_;
        shadow.format = PixelFormat::R32Float;
        shadow.usage = TextureUsage::RenderTarget | TextureUsage::ShaderResource;
        shadowMapTexture_ = backend_->createTexture(shadow);
        if (!shadowMapTexture_.valid()) {
            error = "failed to create the shadow map";
            return false;
        }

        TextureDesc shadowDepth;
        shadowDepth.width = shadowMapSize_;
        shadowDepth.height = shadowMapSize_;
        shadowDepth.format = PixelFormat::Depth32Float;
        shadowDepth.usage = TextureUsage::DepthStencil;
        shadowDepthTexture_ = backend_->createTexture(shadowDepth);
        if (!shadowDepthTexture_.valid()) {
            error = "failed to create the shadow depth buffer";
            return false;
        }
    }

    // The tone mapped image and the two targets the anti-aliasing passes
    // work with. They exist in every mode: the mode is a runtime knob, so
    // the targets cannot depend on it.
    TextureDesc ldr;
    ldr.width = width_;
    ldr.height = height_;
    ldr.format = PixelFormat::Rgba8Unorm;
    ldr.usage = TextureUsage::RenderTarget | TextureUsage::ShaderResource;
    ldrTexture_ = backend_->createTexture(ldr);
    if (!ldrTexture_.valid()) {
        error = "failed to create the tone mapped target";
        return false;
    }
    resolveTexture_ = backend_->createTexture(ldr);
    if (!resolveTexture_.valid()) {
        error = "failed to create the resolve target";
        return false;
    }
    for (TextureHandle& history : historyTexture_) {
        history = backend_->createTexture(ldr);
        if (!history.valid()) {
            error = "failed to create the temporal history target";
            return false;
        }
    }
    historyValid_ = false;

    if (options_.headless) {
        // A headless frame has no swapchain, so the final image goes into a
        // texture the host can read back.
        outputTexture_ = backend_->createTexture(ldr);
        if (!outputTexture_.valid()) {
            error = "failed to create the headless target";
            return false;
        }
    }

    BufferDesc uiDesc;
    uiDesc.size = kUiBufferBytes;
    uiDesc.usage = BufferUsage::Vertex | BufferUsage::Dynamic;
    for (std::uint32_t index = 0; index < kUiBufferCount; ++index) {
        uiRectBuffers_[index] = backend_->createBuffer(uiDesc);
        uiTextBuffers_[index] = backend_->createBuffer(uiDesc);
        if (!uiRectBuffers_[index].valid() || !uiTextBuffers_[index].valid()) {
            error = "failed to create the UI upload buffers";
            return false;
        }
    }
    uiBufferIndex_ = 0;
    return true;
}

void DirectXRenderer::destroyTargets() noexcept {
    if (!backend_) {
        return;
    }
    if (hdrTexture_.valid()) backend_->destroyTexture(hdrTexture_);
    if (depthTexture_.valid()) backend_->destroyTexture(depthTexture_);
    if (shadowMapTexture_.valid()) backend_->destroyTexture(shadowMapTexture_);
    if (shadowDepthTexture_.valid()) backend_->destroyTexture(shadowDepthTexture_);
    if (ldrTexture_.valid()) backend_->destroyTexture(ldrTexture_);
    if (resolveTexture_.valid()) backend_->destroyTexture(resolveTexture_);
    if (outputTexture_.valid()) backend_->destroyTexture(outputTexture_);
    for (TextureHandle& history : historyTexture_) {
        if (history.valid()) backend_->destroyTexture(history);
        history = {};
    }
    hdrTexture_ = {};
    depthTexture_ = {};
    shadowMapTexture_ = {};
    shadowDepthTexture_ = {};
    ldrTexture_ = {};
    resolveTexture_ = {};
    outputTexture_ = {};
    for (std::uint32_t index = 0; index < kUiBufferCount; ++index) {
        if (uiRectBuffers_[index].valid()) backend_->destroyBuffer(uiRectBuffers_[index]);
        if (uiTextBuffers_[index].valid()) backend_->destroyBuffer(uiTextBuffers_[index]);
        uiRectBuffers_[index] = {};
        uiTextBuffers_[index] = {};
    }
}

bool DirectXRenderer::resizeTargets(std::uint32_t width, std::uint32_t height, std::string& error) {
    if (width == width_ && height == height_) {
        return true;
    }
    width_ = width;
    height_ = height;
    stats_.surfaceWidth = width_;
    stats_.surfaceHeight = height_;
    if (!options_.headless && backend_) {
        backend_->resizeSwapchain(width, height);
    }
    return createTargets(error);
}

std::uint64_t DirectXRenderer::createMesh(const MeshData& data) {
    if (!backend_ || data.vertices.empty() || data.indices.empty()) {
        return 0;
    }
    std::vector<std::uint8_t> vertices(data.vertices.size() * sizeof(MeshVertex));
    std::memcpy(vertices.data(), data.vertices.data(), vertices.size());

    Backend::BufferDesc vertexDesc;
    vertexDesc.size = vertices.size();
    vertexDesc.usage = BufferUsage::Vertex;
    vertexDesc.data = vertices;
    const BufferHandle vertexBuffer = backend_->createBuffer(vertexDesc);

    std::vector<std::uint8_t> indices(data.indices.size() * sizeof(std::uint32_t));
    std::memcpy(indices.data(), data.indices.data(), indices.size());
    Backend::BufferDesc indexDesc;
    indexDesc.size = indices.size();
    indexDesc.usage = BufferUsage::Index;
    indexDesc.data = indices;
    const BufferHandle indexBuffer = backend_->createBuffer(indexDesc);

    if (!vertexBuffer.valid() || !indexBuffer.valid()) {
        if (vertexBuffer.valid()) backend_->destroyBuffer(vertexBuffer);
        if (indexBuffer.valid()) backend_->destroyBuffer(indexBuffer);
        return 0;
    }

    MeshResource resource;
    resource.vertices = vertexBuffer;
    resource.indices = indexBuffer;
    resource.vertexCount = static_cast<std::uint32_t>(data.vertices.size());
    resource.indexCount = static_cast<std::uint32_t>(data.indices.size());

    const std::uint64_t id = nextHandle_++;
    meshes_.emplace(id, resource);
    levelVertices_.emplace(id, data.vertices);
    return id;
}

void DirectXRenderer::destroyMesh(std::uint64_t handle) noexcept {
    const auto group = lodGroups_.find(handle);
    if (group != lodGroups_.end()) {
        destroyLodMesh(handle);
        return;
    }
    const auto entry = meshes_.find(handle);
    if (entry == meshes_.end()) {
        return;
    }
    if (backend_) {
        backend_->destroyBuffer(entry->second.vertices);
        backend_->destroyBuffer(entry->second.indices);
    }
    meshes_.erase(entry);
    levelVertices_.erase(handle);
}

std::uint32_t DirectXRenderer::meshVertexCount(std::uint64_t handle) const noexcept {
    const auto group = lodGroups_.find(handle);
    if (group != lodGroups_.end()) {
        if (group->second.levels.empty()) {
            return 0;
        }
        const auto entry = meshes_.find(group->second.levels.front());
        return entry == meshes_.end() ? 0 : entry->second.vertexCount;
    }
    const auto entry = meshes_.find(handle);
    return entry == meshes_.end() ? 0 : entry->second.vertexCount;
}

std::uint64_t DirectXRenderer::createLodMesh(
    const std::vector<MeshData>& levels,
    const std::vector<float>& maxDistances,
    Aporia::LodBoundary boundary) {
    if (!backend_ || levels.empty()) {
        return 0;
    }
    LodGroup group;
    group.boundary = boundary;
    for (const MeshData& level : levels) {
        const std::uint64_t id = createMesh(level);
        if (id == 0) {
            for (std::uint64_t created : group.levels) {
                destroyMesh(created);
            }
            return 0;
        }
        group.levels.push_back(id);
    }
    for (std::size_t index = 0; index + 1 < group.levels.size(); ++index) {
        const float distance = index < maxDistances.size() ? maxDistances[index] : 0.0f;
        group.maxDistances.push_back(distance);
    }

    // The bounds come from the highest detail level, like the reference
    // renderer does.
    const auto entry = meshes_.find(group.levels.front());
    if (entry != meshes_.end()) {
        const auto vertices = levelVertices_.find(group.levels.front());
        if (vertices != levelVertices_.end()) {
            Aporia::Vec3 minimum{1e30f, 1e30f, 1e30f};
            Aporia::Vec3 maximum{-1e30f, -1e30f, -1e30f};
            for (const MeshVertex& vertex : vertices->second) {
                minimum.x = std::min(minimum.x, static_cast<double>(vertex.position[0]));
                minimum.y = std::min(minimum.y, static_cast<double>(vertex.position[1]));
                minimum.z = std::min(minimum.z, static_cast<double>(vertex.position[2]));
                maximum.x = std::max(maximum.x, static_cast<double>(vertex.position[0]));
                maximum.y = std::max(maximum.y, static_cast<double>(vertex.position[1]));
                maximum.z = std::max(maximum.z, static_cast<double>(vertex.position[2]));
            }
            group.center[0] = static_cast<float>((minimum.x + maximum.x) * 0.5);
            group.center[1] = static_cast<float>((minimum.y + maximum.y) * 0.5);
            group.center[2] = static_cast<float>((minimum.z + maximum.z) * 0.5);
            group.halfExtents[0] = static_cast<float>(std::max((maximum.x - minimum.x) * 0.5, 1.0e-3));
            group.halfExtents[1] = static_cast<float>(std::max((maximum.y - minimum.y) * 0.5, 1.0e-3));
            group.halfExtents[2] = static_cast<float>(std::max((maximum.z - minimum.z) * 0.5, 1.0e-3));
            group.boundingRadius = std::sqrt(
                group.halfExtents[0] * group.halfExtents[0] +
                group.halfExtents[1] * group.halfExtents[1] +
                group.halfExtents[2] * group.halfExtents[2]);
        }
    }

    const std::uint64_t id = nextHandle_++;
    lodGroups_.emplace(id, std::move(group));
    return id;
}

void DirectXRenderer::destroyLodMesh(std::uint64_t handle) noexcept {
    const auto entry = lodGroups_.find(handle);
    if (entry == lodGroups_.end()) {
        return;
    }
    const std::vector<std::uint64_t> levels = entry->second.levels;
    lodGroups_.erase(entry);
    for (std::uint64_t level : levels) {
        destroyMesh(level);
    }
}

bool DirectXRenderer::lodMeshValid(std::uint64_t handle) const noexcept {
    return lodGroups_.count(handle) != 0;
}

std::uint32_t DirectXRenderer::lodLevelCount(std::uint64_t handle) const noexcept {
    const auto entry = lodGroups_.find(handle);
    return entry == lodGroups_.end() ? 0 : static_cast<std::uint32_t>(entry->second.levels.size());
}

std::uint64_t DirectXRenderer::createTexture(const Aporia::TextureDesc& description) {
    if (!backend_ || description.width == 0 || description.height == 0) {
        return 0;
    }
    TextureDesc desc;
    desc.width = description.width;
    desc.height = description.height;
    desc.format = description.format == TextureFormat::Bgra8Unorm ? PixelFormat::Bgra8Unorm
                                                                 : PixelFormat::Rgba8Unorm;
    desc.usage = TextureUsage::ShaderResource;
    if (description.pixels != nullptr && description.dataSize > 0) {
        const std::size_t expected = static_cast<std::size_t>(description.width) *
                                     static_cast<std::size_t>(description.height) * 4u;
        const std::size_t size = std::min(description.dataSize, expected);
        const auto* bytes = static_cast<const std::uint8_t*>(description.pixels);
        desc.data.assign(bytes, bytes + size);
    }
    const TextureHandle handle = backend_->createTexture(desc);
    if (!handle.valid()) {
        return 0;
    }
    TextureResource resource;
    resource.texture = handle;
    resource.width = description.width;
    resource.height = description.height;
    // The initial upload counts like any other one.
    resource.uploadCount = 1;
    const std::uint64_t id = nextHandle_++;
    textures_.emplace(id, resource);
    return id;
}

bool DirectXRenderer::updateTexture(std::uint64_t handle, const void* pixels, std::size_t dataSize) {
    const auto entry = textures_.find(handle);
    if (entry == textures_.end() || pixels == nullptr || dataSize == 0) {
        return false;
    }
    TextureDesc desc;
    desc.width = entry->second.width;
    desc.height = entry->second.height;
    desc.format = PixelFormat::Rgba8Unorm;
    desc.usage = TextureUsage::ShaderResource;
    const std::size_t expected = static_cast<std::size_t>(entry->second.width) *
                                 static_cast<std::size_t>(entry->second.height) * 4u;
    const std::size_t size = std::min(dataSize, expected);
    const auto* bytes = static_cast<const std::uint8_t*>(pixels);
    desc.data.assign(bytes, bytes + size);
    const TextureHandle replacement = backend_->createTexture(desc);
    if (!replacement.valid()) {
        return false;
    }
    backend_->destroyTexture(entry->second.texture);
    entry->second.texture = replacement;
    ++entry->second.uploadCount;
    return true;
}

void DirectXRenderer::destroyTexture(std::uint64_t handle) noexcept {
    const auto entry = textures_.find(handle);
    if (entry == textures_.end()) {
        return;
    }
    if (backend_) {
        backend_->destroyTexture(entry->second.texture);
    }
    textures_.erase(entry);
    if (boundTexture_ == handle) {
        boundTexture_ = 0;
    }
}

bool DirectXRenderer::setFrameTexture(std::uint64_t handle) noexcept {
    if (handle == 0) {
        boundTexture_ = 0;
        return true;
    }
    if (textures_.count(handle) == 0) {
        return false;
    }
    boundTexture_ = handle;
    return true;
}

std::uint64_t DirectXRenderer::createFontFromTtf(const std::string& ttfPath, float pixelSize, std::string& error) {
    if (!backend_) {
        error = "the renderer is not initialized";
        return 0;
    }
    const std::vector<std::uint8_t> bytes = readFileBytes(ttfPath);
    if (bytes.empty()) {
        error = "failed to read the font file: " + ttfPath;
        return 0;
    }
    const BakedFontAtlas baked = bakeTtfAtlas(bytes, pixelSize);
    if (baked.width == 0 || baked.height == 0) {
        error = "failed to rasterize the font: " + ttfPath;
        return 0;
    }
    TextureDesc desc;
    desc.width = baked.width;
    desc.height = baked.height;
    desc.format = PixelFormat::Rgba8Unorm;
    desc.usage = TextureUsage::ShaderResource;
    desc.data = baked.pixels;
    const TextureHandle texture = backend_->createTexture(desc);
    if (!texture.valid()) {
        error = "failed to upload the font atlas";
        return 0;
    }
    FontResource resource;
    resource.texture = texture;
    resource.atlas = baked.atlas;
    buildGlyphIndex(resource.atlas, resource.glyphIndex);
    const std::uint64_t id = nextHandle_++;
    fonts_.emplace(id, resource);
    return id;
}

std::uint64_t DirectXRenderer::createFontFromMsdf(
    const std::string& jsonPath, const std::string& pngPath, std::string& error) {
    if (!backend_) {
        error = "the renderer is not initialized";
        return 0;
    }
    const std::string json = readFileText(jsonPath);
    if (json.empty()) {
        error = "failed to read the font description: " + jsonPath;
        return 0;
    }
    const std::vector<std::uint8_t> png = readFileBytes(pngPath);
    if (png.empty()) {
        error = "failed to read the font atlas: " + pngPath;
        return 0;
    }
    const BakedFontAtlas baked = loadMsdfAtlas(json, png);
    if (baked.width == 0 || baked.height == 0) {
        error = "failed to load the msdf atlas: " + pngPath;
        return 0;
    }
    TextureDesc desc;
    desc.width = baked.width;
    desc.height = baked.height;
    desc.format = PixelFormat::Rgba8Unorm;
    desc.usage = TextureUsage::ShaderResource;
    desc.data = baked.pixels;
    const TextureHandle texture = backend_->createTexture(desc);
    if (!texture.valid()) {
        error = "failed to upload the font atlas";
        return 0;
    }
    FontResource resource;
    resource.texture = texture;
    resource.atlas = baked.atlas;
    buildGlyphIndex(resource.atlas, resource.glyphIndex);
    const std::uint64_t id = nextHandle_++;
    fonts_.emplace(id, resource);
    return id;
}

void DirectXRenderer::destroyFont(std::uint64_t handle) noexcept {
    const auto entry = fonts_.find(handle);
    if (entry == fonts_.end()) {
        return;
    }
    if (backend_) {
        backend_->destroyTexture(entry->second.texture);
    }
    fonts_.erase(entry);
}

bool DirectXRenderer::fontValid(std::uint64_t handle) const noexcept {
    return fonts_.count(handle) != 0;
}

float DirectXRenderer::measureText(std::uint64_t handle, std::string_view text, float size) const noexcept {
    const auto entry = fonts_.find(handle);
    if (entry == fonts_.end()) {
        return 0.0f;
    }
    const UiTextAtlasDesc& atlas = entry->second.atlas;
    const float em = atlas.emSize > 0.0f ? atlas.emSize : 32.0f;
    const float scale = size / em;
    float width = 0.0f;
    float maxWidth = 0.0f;
    std::size_t index = 0;
    while (index < text.size()) {
        const std::uint32_t codePoint = decodeUtf8(text, index);
        if (codePoint == '\n') {
            maxWidth = std::max(maxWidth, width);
            width = 0.0f;
            continue;
        }
        if (const UiGlyph* glyph = findGlyph(atlas, codePoint)) {
            width += glyph->advance * scale;
        }
    }
    return std::max(maxWidth, width);
}

void DirectXRenderer::setLights(const std::vector<LightDesc>& lights) {
    lights_ = lights;
}

void DirectXRenderer::clearLights() noexcept {
    lights_.clear();
}

void DirectXRenderer::setUiDrawList(UiDrawList drawList) {
    uiDrawList_ = std::move(drawList);
}

void DirectXRenderer::clearUiDrawList() noexcept {
    uiDrawList_ = UiDrawList{};
}

void DirectXRenderer::setQuality(Aporia::GraphicsQuality quality) {
    options_.quality = quality;
    stats_.quality = static_cast<std::uint32_t>(quality);
    // The preset owns the shadow budget, so the map follows it.
    const std::uint32_t requested = shadowMapSizeFor(quality);
    if (requested != shadowMapSize_) {
        shadowMapSize_ = requested;
        std::string error;
        if (backend_ && !createTargets(error)) {
            lastError_ = error;
        }
    }
    stats_.shadowMapSize = shadowMapSize_;
}

void DirectXRenderer::setShadowConfig(const Aporia::ShadowConfig& config) {
    options_.shadows = config;
    const std::uint32_t requested = config.mapSize > 0 ? config.mapSize
                                                       : shadowMapSizeFor(options_.quality);
    if (requested == shadowMapSize_) {
        return;
    }
    shadowMapSize_ = requested;
    std::string error;
    if (backend_ && !createTargets(error)) {
        lastError_ = error;
    }
    stats_.shadowMapSize = shadowMapSize_;
}

void DirectXRenderer::setVsync(bool enabled) noexcept {
    options_.vsync = enabled;
    // The swapchain keeps its own copy of the flag: Present() reads it every
    // frame, so a runtime toggle has to reach the backend as well.
    if (backend_) {
        backend_->setVsync(enabled);
    }
}

void DirectXRenderer::setShadowExtent(float metres) noexcept {
    // The extent feeds the sun ortho box and the PCF step (shadowParams[2]),
    // both rebuilt from options_ every frame, so no target recreation here.
    options_.shadowExtent = std::clamp(metres, 1.0f, 500.0f);
}

void DirectXRenderer::setAntiAliasing(Aporia::AntiAliasingMode mode) noexcept {
    antiAliasing_ = mode;
    stats_.antiAliasing = static_cast<std::uint32_t>(mode);
}

void DirectXRenderer::setAntiAliasingStrength(float strength) noexcept {
    antiAliasingStrength_ = strength;
    stats_.antiAliasingStrength = strength;
}

#if defined(APORIA_EXPERIMENTAL)
std::uint64_t DirectXRenderer::createMaterial(const Aporia::MaterialDesc& description) {
    const std::uint64_t id = nextHandle_++;
    materials_.emplace(id, description);
    return id;
}

bool DirectXRenderer::updateMaterial(std::uint64_t handle, const Aporia::MaterialDesc& description) {
    const auto entry = materials_.find(handle);
    if (entry == materials_.end()) {
        return false;
    }
    entry->second = description;
    return true;
}

void DirectXRenderer::destroyMaterial(std::uint64_t handle) noexcept {
    materials_.erase(handle);
}

bool DirectXRenderer::materialValid(std::uint64_t handle) const noexcept {
    return materials_.count(handle) != 0;
}
#endif

void DirectXRenderer::waitIdle() noexcept {
    if (backend_) {
        backend_->waitIdle();
    }
}

bool DirectXRenderer::readbackPixels(std::vector<std::uint8_t>& pixels) const {
    std::printf("[dbg] renderer readback backend=%d output=%llu headless=%d %ux%u\n",
                backend_ ? 1 : 0, (unsigned long long)outputTexture_.value,
                options_.headless ? 1 : 0, width_, height_);
    std::fflush(stdout);
    if (!backend_ || !outputTexture_.valid()) {
        return false;
    }
    return backend_->readPixels(outputTexture_, 0, 0, width_, height_, pixels);
}

std::uint64_t DirectXRenderer::resolveMesh(
    std::uint64_t meshId,
    const RenderItem& item,
    const CameraState& camera,
    std::uint64_t& drawTriangles,
    std::uint64_t& fullTriangles) {
    const auto groupIt = lodGroups_.find(meshId);
    if (groupIt == lodGroups_.end()) {
        return meshId;
    }
    LodGroup& group = groupIt->second;

    const float* m = item.modelMatrix;
    const std::array<double, 3> axisX{m[0], m[1], m[2]};
    const std::array<double, 3> axisY{m[4], m[5], m[6]};
    const std::array<double, 3> axisZ{m[8], m[9], m[10]};
    const auto lengthOf = [](const std::array<double, 3>& v) {
        return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    };
    const double scaleX = lengthOf(axisX);
    const double scaleY = lengthOf(axisY);
    const double scaleZ = lengthOf(axisZ);
    const double centerX = m[12] + axisX[0] * group.center[0] + axisY[0] * group.center[1] +
                           axisZ[0] * group.center[2];
    const double centerY = m[13] + axisX[1] * group.center[0] + axisY[1] * group.center[1] +
                           axisZ[1] * group.center[2];
    const double centerZ = m[14] + axisX[2] * group.center[0] + axisY[2] * group.center[1] +
                           axisZ[2] * group.center[2];
    const double deltaX = camera.position[0] - centerX;
    const double deltaY = camera.position[1] - centerY;
    const double deltaZ = camera.position[2] - centerZ;

    double distance = 0.0;
    if (group.boundary == Aporia::LodBoundary::Box && scaleX > 1.0e-6 &&
        scaleY > 1.0e-6 && scaleZ > 1.0e-6) {
        const auto unit = [&](const std::array<double, 3>& v, double length) {
            return std::array<double, 3>{v[0] / length, v[1] / length, v[2] / length};
        };
        const std::array<double, 3> ux = unit(axisX, scaleX);
        const std::array<double, 3> uy = unit(axisY, scaleY);
        const std::array<double, 3> uz = unit(axisZ, scaleZ);
        const double extentX = scaleX * group.halfExtents[0];
        const double extentY = scaleY * group.halfExtents[1];
        const double extentZ = scaleZ * group.halfExtents[2];
        const auto project = [&](const std::array<double, 3>& u) {
            return deltaX * u[0] + deltaY * u[1] + deltaZ * u[2];
        };
        const double px = std::clamp(project(ux), -extentX, extentX);
        const double py = std::clamp(project(uy), -extentY, extentY);
        const double pz = std::clamp(project(uz), -extentZ, extentZ);
        const double closestX = ux[0] * px + uy[0] * py + uz[0] * pz;
        const double closestY = ux[1] * px + uy[1] * py + uz[1] * pz;
        const double closestZ = ux[2] * px + uy[2] * py + uz[2] * pz;
        const double ox = deltaX - closestX;
        const double oy = deltaY - closestY;
        const double oz = deltaZ - closestZ;
        distance = std::sqrt(ox * ox + oy * oy + oz * oz);
    } else {
        const double radius = group.boundingRadius * std::max({scaleX, scaleY, scaleZ});
        const double centerDistance =
            std::sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
        distance = std::max(0.0, centerDistance - radius);
    }

    std::uint32_t level = static_cast<std::uint32_t>(group.levels.size() - 1);
    for (std::uint32_t candidate = 0; candidate + 1 < group.levels.size(); ++candidate) {
        const float limit = candidate < group.maxDistances.size() ? group.maxDistances[candidate]
                                                                  : 0.0f;
        if (distance <= limit) {
            level = candidate;
            break;
        }
    }
    if (level != group.currentLevel) {
        ++lodSwitchCount_;
        group.currentLevel = level;
    }

    const auto drawEntry = meshes_.find(group.levels[level]);
    const auto fullEntry = meshes_.find(group.levels.front());
    drawTriangles += drawEntry == meshes_.end() ? 0 : drawEntry->second.indexCount / 3u;
    fullTriangles += fullEntry == meshes_.end() ? 0 : fullEntry->second.indexCount / 3u;
    return group.levels[level];
}

void DirectXRenderer::drawItems(
    const CameraState& camera,
    const std::vector<RenderItem>& items,
    bool shadowPass,
    std::uint64_t& lodDrawTriangles,
    std::uint64_t& lodFullTriangles) {
    backend_->setPipeline(shadowPass ? shadowPipeline_ : meshPipeline_);
    if (!shadowPass) {
        backend_->setSampler(0, linearSampler_);
        backend_->setSampler(1, clampSampler_);
    }

    for (const RenderItem& item : items) {
        std::uint64_t meshId = item.meshId;
        if (!shadowPass) {
            meshId = resolveMesh(item.meshId, item, camera, lodDrawTriangles, lodFullTriangles);
        }
        const auto entry = meshes_.find(meshId);
        if (entry == meshes_.end() || entry->second.indexCount == 0) {
            continue;
        }

        ObjectConstants object{};
        std::memcpy(object.model, item.modelMatrix, sizeof(object.model));
        object.color[0] = item.color[0];
        object.color[1] = item.color[1];
        object.color[2] = item.color[2];
        object.color[3] = item.color[3];
        object.material[0] = item.emissive;
        backend_->setConstantBuffer(1, &object, sizeof(object));

        if (!shadowPass) {
            const std::uint64_t albedoId = item.texture.valid() ? item.texture.value() : boundTexture_;
            TextureHandle albedo = defaultTexture_;
            const auto texture = textures_.find(albedoId);
            if (texture != textures_.end()) {
                albedo = texture->second.texture;
            }
            if (albedo != boundAlbedo_) {
                backend_->setTexture(0, albedo);
                boundAlbedo_ = albedo;
            }
            if (shadowMapTexture_.valid()) {
                // The shadow map is a render target during the shadow pass, so
                // it leaves the sampled state every frame. Rebinding restores
                // it even when the handle itself did not change.
                backend_->setTexture(1, shadowMapTexture_);
                boundShadowMap_ = shadowMapTexture_;
            }
        }

        backend_->bindVertexBuffer(0, entry->second.vertices, 0);
        backend_->bindIndexBuffer(entry->second.indices, IndexFormat::Uint32);
        if (shadowPass && std::getenv("APORIA_DIAG_SHADOW") != nullptr) {
            static int dbgDraws = 0;
            if (dbgDraws < 24) {
                ++dbgDraws;
                std::printf("[dbg] shadow draw mesh=%llu idx=%u pos=(%.2f %.2f %.2f)\n",
                            static_cast<unsigned long long>(meshId),
                            entry->second.indexCount,
                            item.modelMatrix[12], item.modelMatrix[13], item.modelMatrix[14]);
                std::fflush(stdout);
            }
        }
        backend_->drawIndexed(entry->second.indexCount);
    }
}

void DirectXRenderer::writeSceneConstants(
    const CameraState& camera,
    const EnvironmentState& environment,
    const float sunMatrix[16]) {
    SceneConstants constants{};
    multiplyMat4(camera.projectionMatrix, camera.viewMatrix, constants.viewProjection);
    std::memcpy(constants.sunViewProjection, sunMatrix, sizeof(constants.sunViewProjection));

    normalize3(environment.sunDirection, constants.sunDirection);
    constants.sunDirection[3] = environment.sunIntensity;
    constants.cameraPosition[0] = static_cast<float>(camera.position[0]);
    constants.cameraPosition[1] = static_cast<float>(camera.position[1]);
    constants.cameraPosition[2] = static_cast<float>(camera.position[2]);

    constants.shadingParams[0] = environment.ambient;
    constants.shadingParams[1] = options_.shadows.strength;
    constants.shadingParams[2] = static_cast<float>(std::min<std::size_t>(lights_.size(), kMaxLights));
    constants.shadingParams[3] = shadowMapSize_ > 0 ? 1.0f / static_cast<float>(shadowMapSize_) : 0.0f;

    constants.screenParams[0] = static_cast<float>(width_);
    constants.screenParams[1] = static_cast<float>(height_);
    constants.screenParams[2] = options_.exposure;
    constants.screenParams[3] = static_cast<float>(shadowMapSize_);

    constants.fogColor[3] = 0.0f;
    constants.fogParams[0] = 0.0f;
    constants.fogParams[1] = 0.0f;

    // The camera basis is taken from the view matrix rows. The storage is
    // column-major (flat index = column * 4 + row), and lookAt writes right
    // into row 0, up into row 1 and -forward into row 2, so the components of
    // one basis vector sit 4 floats apart, not next to each other.
    constants.cameraRight[0] = camera.viewMatrix[0];
    constants.cameraRight[1] = camera.viewMatrix[4];
    constants.cameraRight[2] = camera.viewMatrix[8];
    constants.cameraUp[0] = camera.viewMatrix[1];
    constants.cameraUp[1] = camera.viewMatrix[5];
    constants.cameraUp[2] = camera.viewMatrix[9];
    constants.cameraForward[0] = -camera.viewMatrix[2];
    constants.cameraForward[1] = -camera.viewMatrix[6];
    constants.cameraForward[2] = -camera.viewMatrix[10];

    const std::size_t lightCount = std::min<std::size_t>(lights_.size(), kMaxLights);
    for (std::size_t index = 0; index < lightCount; ++index) {
        const LightDesc& light = lights_[index];
        std::memcpy(constants.lightPosition[index], light.position, sizeof(light.position));
        constants.lightPosition[index][3] = light.type == LightType::Directional   ? 0.0f
                                           : light.type == LightType::Spot        ? 2.0f
                                                                                    : 1.0f;
        std::memcpy(constants.lightDirection[index], light.direction, sizeof(light.direction));
        constants.lightDirection[index][3] = 0.0f;
        constants.lightColor[index][0] = light.color[0];
        constants.lightColor[index][1] = light.color[1];
        constants.lightColor[index][2] = light.color[2];
        constants.lightColor[index][3] = light.intensity;
        constants.lightParams[index][0] = light.radius;
        constants.lightParams[index][1] = light.softness;
        constants.lightParams[index][2] = std::cos(light.innerAngle);
        constants.lightParams[index][3] = std::cos(light.outerAngle);
    }

    // The text pass needs to know which atlas it samples.
    float textMode = 0.0f;
    float distanceRange = 4.0f;
    if (uiDrawList_.font.valid()) {
        const auto font = fonts_.find(uiDrawList_.font.value());
        if (font != fonts_.end()) {
            textMode = font->second.atlas.mode == UiTextAtlasMode::Msdf ? 1.0f : 0.0f;
            distanceRange = font->second.atlas.distanceRange;
        }
    } else {
        textMode = uiDrawList_.textAtlas.mode == UiTextAtlasMode::Msdf ? 1.0f : 0.0f;
        distanceRange = uiDrawList_.textAtlas.distanceRange;
    }
    constants.uiParams[0] = textMode;
    constants.uiParams[1] = distanceRange > 0.0f ? distanceRange : 4.0f;
    constants.uiParams[2] = 0.0f;
    constants.uiParams[3] = 1.0f;

    // The temporal pass moves the scene by a fraction of a pixel and reads
    // the history of the previous frame, so both offsets travel with the
    // constants.
    constants.jitter[0] = jitter_[0];
    constants.jitter[1] = jitter_[1];
    constants.jitter[2] = previousJitter_[0];
    constants.jitter[3] = previousJitter_[1];
    constants.aaParams[0] = historyValid_ ? 0.9f : 0.0f;
    constants.aaParams[1] = antiAliasingStrength_;
    // The sky rebuilds its view ray from the camera basis, so it needs the
    // same vertical field of view the projection was built with. The Y scale
    // of a perspective matrix is 1 / tan(fov / 2); without this the sky would
    // use an implicit 90 degree cone and lag behind the world on every turn.
    const float projectionYScale = camera.projectionMatrix[5];
    constants.aaParams[2] = projectionYScale > 1.0e-6f ? 1.0f / projectionYScale : 1.0f;
    constants.aaParams[3] = 0.0f;

    // Shadow filtering: the preset picks the kernel radius and whether the
    // contact hardening pass runs. Without these the shader would always use
    // a one texel kernel and every shadow edge would stay hard.
    constants.shadowParams[0] = static_cast<float>(options_.shadows.pcfRadius);
    constants.shadowParams[1] = options_.shadows.filter == Aporia::ShadowFilter::Pcss ? 1.0f : 0.0f;
    constants.shadowParams[2] = std::max(0.01f, options_.shadowExtent * 0.5f / 12.0f);
    constants.shadowParams[3] = 0.0f;

    backend_->setConstantBuffer(0, &constants, sizeof(constants));
}

void DirectXRenderer::writeUiVertices(
    std::vector<float>& rectVertices,
    std::vector<float>& textVertices,
    const UiDrawList& ui,
    const std::unordered_map<std::uint32_t, std::size_t>* glyphIndex) {
    rectVertices.clear();
    textVertices.clear();

    const float cornerRadius = ui.cornerRadius;
    if (!ui.roundedRects.empty()) {
        rectVertices.resize(ui.roundedRects.size() * 6 * 12);
        float* write = rectVertices.data();
        for (const auto& [rect, color] : ui.roundedRects) {
            const float halfWidth = std::max(rect.width * 0.5f, 0.0f);
            const float halfHeight = std::max(rect.height * 0.5f, 0.0f);
            const float centerX = rect.x + halfWidth;
            const float centerY = rect.y + halfHeight;
            const float radius = std::min(cornerRadius, std::min(halfWidth, halfHeight));

            const float corners[6][2] = {
                {-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f},
                {-1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f},
            };
            for (const auto& corner : corners) {
                *write++ = centerX + corner[0] * halfWidth;
                *write++ = centerY + corner[1] * halfHeight;
                *write++ = corner[0] * halfWidth;
                *write++ = corner[1] * halfHeight;
                *write++ = color.r;
                *write++ = color.g;
                *write++ = color.b;
                *write++ = color.a;
                *write++ = halfWidth;
                *write++ = halfHeight;
                *write++ = radius;
                *write++ = 0.0f;
            }
        }
    }

    UiTextAtlasDesc atlas = ui.textAtlas;
    const std::unordered_map<std::uint32_t, std::size_t>* index = glyphIndex;
    if (ui.font.valid()) {
        const auto font = fonts_.find(ui.font.value());
        if (font != fonts_.end()) {
            atlas = font->second.atlas;
            if (!font->second.glyphIndex.empty()) {
                index = &font->second.glyphIndex;
            }
        }
    }
    if (atlas.glyphs.empty() || ui.textRuns.empty()) {
        return;
    }
    const float em = atlas.emSize > 0.0f ? atlas.emSize : 32.0f;
    // Every visible glyph expands into six vertices of eight floats; sizing
    // the stream up front keeps the whole pass allocation free.
    std::size_t glyphCount = 0;
    for (const UiTextRun& run : ui.textRuns) {
        glyphCount += run.text.size();
    }
    textVertices.resize(glyphCount * 6 * 8);
    float* write = textVertices.data();
    std::size_t written = 0;
    for (const UiTextRun& run : ui.textRuns) {
        const float scale = run.size / em;
        const float lineAdvance = lineHeightFor(atlas, run.size);
        float cursorX = run.x;
        float cursorY = run.y;
        std::size_t position = 0;
        const std::string_view text = run.text;
        while (position < text.size()) {
            const std::uint32_t codePoint = decodeUtf8(text, position);
            if (codePoint == '\n') {
                cursorX = run.x;
                cursorY += lineAdvance;
                continue;
            }
            const UiGlyph* glyph = nullptr;
            if (index != nullptr && !index->empty()) {
                const auto entry = index->find(codePoint);
                if (entry != index->end()) {
                    glyph = &atlas.glyphs[entry->second];
                }
            } else {
                glyph = findGlyph(atlas, codePoint);
            }
            if (glyph == nullptr) {
                continue;
            }
            if (glyph->planeWidth > 0.0f && glyph->planeHeight > 0.0f) {
                const float left = cursorX + glyph->planeLeft * run.size;
                const float top = cursorY + (atlas.ascender - glyph->planeTop) * run.size;
                const float width = glyph->planeWidth * run.size;
                const float height = glyph->planeHeight * run.size;
                const float uvs[6][2] = {
                    {glyph->u0, glyph->v0}, {glyph->u1, glyph->v0}, {glyph->u1, glyph->v1},
                    {glyph->u0, glyph->v0}, {glyph->u1, glyph->v1}, {glyph->u0, glyph->v1},
                };
                const float positions[6][2] = {
                    {left, top}, {left + width, top}, {left + width, top + height},
                    {left, top}, {left + width, top + height}, {left, top + height},
                };
                for (std::uint32_t vertex = 0; vertex < 6; ++vertex) {
                    *write++ = positions[vertex][0];
                    *write++ = positions[vertex][1];
                    *write++ = uvs[vertex][0];
                    *write++ = uvs[vertex][1];
                    *write++ = run.color.r;
                    *write++ = run.color.g;
                    *write++ = run.color.b;
                    *write++ = run.color.a;
                }
                written += 6;
            }
            cursorX += glyph->advance * scale;
        }
    }
    textVertices.resize(written * 8);
}

void DirectXRenderer::drawUi() {
    writeUiVertices(uiRectVertices_, uiTextVertices_, uiDrawList_, nullptr);
    std::vector<float>& rectVertices = uiRectVertices_;
    std::vector<float>& textVertices = uiTextVertices_;
    if (rectVertices.empty() && textVertices.empty()) {
        return;
    }

    const bool native = !options_.headless;
    const auto acquireSlot = [&]() {
        const std::uint32_t slot = uiBufferIndex_ % kUiBufferCount;
        uiBufferIndex_ = (uiBufferIndex_ + 1) % kUiBufferCount;
        return slot;
    };
    // One frame may carry more vertices than a single upload buffer holds, so
    // both passes are split into chunks instead of dropping the whole batch.
    constexpr std::size_t kUiBufferFloats = kUiBufferBytes / sizeof(float);

    if (!rectVertices.empty()) {
        backend_->setPipeline(native ? uiRectPipeline_ : uiRectLdrPipeline_);
        std::size_t offset = 0;
        while (offset + 12 <= rectVertices.size()) {
            std::size_t count = std::min(kUiBufferFloats, rectVertices.size() - offset);
            count -= count % 12;
            if (count == 0) {
                break;
            }
            const std::uint32_t slot = acquireSlot();
            const std::size_t bytes = count * sizeof(float);
            backend_->updateBuffer(uiRectBuffers_[slot], rectVertices.data() + offset, bytes);
            backend_->bindVertexBuffer(0, uiRectBuffers_[slot], 0);
            backend_->draw(static_cast<std::uint32_t>(count / 12));
            offset += count;
        }
    }

    if (!textVertices.empty()) {
        TextureHandle atlasTexture = defaultTexture_;
        if (uiDrawList_.font.valid()) {
            const auto font = fonts_.find(uiDrawList_.font.value());
            if (font != fonts_.end()) {
                atlasTexture = font->second.texture;
            }
        } else if (uiDrawList_.textAtlas.texture.valid()) {
            const auto texture = textures_.find(uiDrawList_.textAtlas.texture.value());
            if (texture != textures_.end()) {
                atlasTexture = texture->second.texture;
            }
        }
        backend_->setTexture(0, atlasTexture);
        backend_->setSampler(0, linearSampler_);
        backend_->setPipeline(native ? uiTextPipeline_ : uiTextLdrPipeline_);
        std::size_t offset = 0;
        while (offset + 8 <= textVertices.size()) {
            std::size_t count = std::min(kUiBufferFloats, textVertices.size() - offset);
            count -= count % 8;
            if (count == 0) {
                break;
            }
            const std::uint32_t slot = acquireSlot();
            const std::size_t bytes = count * sizeof(float);
            backend_->updateBuffer(uiTextBuffers_[slot], textVertices.data() + offset, bytes);
            backend_->bindVertexBuffer(0, uiTextBuffers_[slot], 0);
            backend_->draw(static_cast<std::uint32_t>(count / 8));
            offset += count;
        }
    }
}

bool DirectXRenderer::renderFrame(
    const CameraState& camera,
    const EnvironmentState& environment,
    const std::vector<RenderItem>& items) {
    if (!initialized_ || !backend_) {
        lastError_ = "the renderer is not initialized";
        return false;
    }

    std::string error;

    // The host owns the window, so its size is polled every frame: a resize
    // has to rebuild the swapchain and the render targets.
#if defined(_WIN32)
    if (!options_.headless && options_.nativeWindow != 0) {
        std::uint32_t clientWidth = width_;
        std::uint32_t clientHeight = height_;
        windowClientSize(options_.nativeWindow, clientWidth, clientHeight);
        if ((clientWidth != width_ || clientHeight != height_) &&
            !resizeTargets(clientWidth, clientHeight, error)) {
            lastError_ = error;
            return false;
        }
    }
#endif

    if (!backend_->beginFrame(error)) {
        lastError_ = error;
        return false;
    }

    // TEMP DIAG: the previous frame is complete here, so the shadow map holds
    // exactly what the last frame's pixel shader sampled. Report the depth
    // range over the whole map: a range wider than the ground depth means
    // casters made it into the map.
    if (shadowMapTexture_.valid() && std::getenv("APORIA_DIAG_SHADOW") != nullptr) {
        static int diagFrames = 0;
        if (diagFrames < 4) {
            ++diagFrames;
            std::vector<std::uint8_t> pixels;
            const std::uint32_t probe = std::min<std::uint32_t>(2048, shadowMapSize_);
            const std::uint32_t origin = (shadowMapSize_ - probe) / 2;
            if (backend_->readPixels(shadowMapTexture_, origin, origin, probe, probe, pixels)) {
                float lowest = 1.0e30f;
                float highest = -1.0e30f;
                std::size_t dark = 0;
                std::size_t count = 0;
                for (std::size_t index = 0; index + 4 <= pixels.size(); index += 4) {
                    float value = 0.0f;
                    std::memcpy(&value, pixels.data() + index, sizeof(value));
                    if (!std::isfinite(value)) {
                        continue;
                    }
                    lowest = std::min(lowest, value);
                    highest = std::max(highest, value);
                    if (value < 0.30f) {
                        ++dark;
                    }
                    ++count;
                }
                std::printf("[diag] frame %d shadowMap %ux%u: min=%.4f max=%.4f dark(<0.30)=%zu/%zu w=%u h=%u\n",
                            diagFrames, probe, probe, lowest, highest, dark, count,
                            width_, height_);
            } else {
                std::printf("[diag] frame %d shadowMap read failed\n", diagFrames);
            }
            std::fflush(stdout);
        }
    }

    // The temporal pass walks through a small set of sub-pixel offsets, so
    // every frame samples a slightly different point of the geometry.
    const bool temporal = antiAliasing_ == Aporia::AntiAliasingMode::TAA;
    previousJitter_[0] = jitter_[0];
    previousJitter_[1] = jitter_[1];
    if (temporal) {
        static constexpr float kHalton[8][2] = {
            {0.0f, 0.0f},
            {0.5f, 0.3333333f},
            {0.25f, 0.6666667f},
            {0.75f, 0.1111111f},
            {0.125f, 0.4444444f},
            {0.625f, 0.7777778f},
            {0.375f, 0.2222222f},
            {0.875f, 0.5555556f},
        };
        const float (&phase)[2] = kHalton[jitterPhase_ % 8];
        jitterPhase_ = (jitterPhase_ + 1) % 8;
        jitter_[0] = (phase[0] - 0.5f) * 0.9f;
        jitter_[1] = (phase[1] - 0.5f) * 0.9f;
    } else {
        jitter_[0] = 0.0f;
        jitter_[1] = 0.0f;
        historyValid_ = false;
    }

    float sunMatrix[16] = {};
    buildSunMatrix(environment.sunDirection, camera.position, options_.shadowExtent,
                   shadowMapSize_ > 0 ? shadowMapSize_ : 1, sunMatrix);
    writeSceneConstants(camera, environment, sunMatrix);

    std::uint64_t lodDrawTriangles = 0;
    std::uint64_t lodFullTriangles = 0;

    // Shadow pass: the closest caster wins the depth test, so the colour
    // target holds the light space depth of the nearest surface.
    if (options_.shadows.enabled && shadowMapTexture_.valid()) {
        RenderPassDesc pass;
        pass.colorTargets = {RenderTargetBinding{RenderTargetBinding::Source::Texture,
                                                 shadowMapTexture_}};
        pass.depthTarget = RenderTargetBinding{RenderTargetBinding::Source::Texture,
                                               shadowDepthTexture_};
        pass.hasDepth = true;
        pass.clearColorEnabled = true;
        pass.clear.color[0] = 1.0f;
        pass.clear.color[1] = 1.0f;
        pass.clear.color[2] = 1.0f;
        pass.clear.color[3] = 1.0f;
        pass.clear.depth = 1.0f;
        backend_->beginRenderPass(pass);
        backend_->setViewport(Viewport{0.0f, 0.0f, static_cast<float>(shadowMapSize_),
                                       static_cast<float>(shadowMapSize_), 0.0f, 1.0f});
        drawItems(camera, items, true, lodDrawTriangles, lodFullTriangles);
        backend_->endRenderPass();
    }

    // Scene pass.
    {
        RenderPassDesc pass;
        pass.colorTargets = {RenderTargetBinding{RenderTargetBinding::Source::Texture, hdrTexture_}};
        pass.depthTarget = RenderTargetBinding{RenderTargetBinding::Source::Texture, depthTexture_};
        pass.hasDepth = true;
        pass.clearColorEnabled = true;
        pass.clear.color[0] = 0.02f;
        pass.clear.color[1] = 0.03f;
        pass.clear.color[2] = 0.05f;
        pass.clear.color[3] = 1.0f;
        pass.clear.depth = 1.0f;
        backend_->beginRenderPass(pass);
        backend_->setViewport(Viewport{0.0f, 0.0f, static_cast<float>(width_),
                                       static_cast<float>(height_), 0.0f, 1.0f});
        if (options_.sky) {
            backend_->setPipeline(skyPipeline_);
            // The sky is procedural: it must not sample the image it writes to.
            backend_->setTexture(0, defaultTexture_);
            backend_->setSampler(0, linearSampler_);
            backend_->draw(3);
        }
        drawItems(camera, items, false, lodDrawTriangles, lodFullTriangles);
        backend_->endRenderPass();
    }

    // The final image goes to the swapchain in a window and to a texture in a
    // headless frame, where the host reads it back.
    const bool native = !options_.headless;
    const auto outputBinding = [&]() {
        RenderTargetBinding binding;
        binding.source = native ? RenderTargetBinding::Source::Swapchain
                                : RenderTargetBinding::Source::Texture;
        binding.texture = native ? TextureHandle{} : outputTexture_;
        return binding;
    };
    const bool useAntiAliasing = antiAliasing_ != Aporia::AntiAliasingMode::None;

    // Tone map. Without anti-aliasing the result goes straight to the output,
    // otherwise it lands in the intermediate image the resolve pass reads.
    {
        RenderPassDesc pass;
        pass.colorTargets = {useAntiAliasing
                                 ? RenderTargetBinding{RenderTargetBinding::Source::Texture, ldrTexture_}
                                 : outputBinding()};
        pass.clearColorEnabled = true;
        pass.clear.color[0] = 0.0f;
        pass.clear.color[1] = 1.0f;
        pass.clear.color[2] = 0.0f;
        pass.clear.color[3] = 1.0f;
        backend_->beginRenderPass(pass);
        backend_->setPipeline(native && !useAntiAliasing ? tonemapPipeline_ : tonemapLdrPipeline_);
        backend_->setViewport(Viewport{0.0f, 0.0f, static_cast<float>(width_),
                                       static_cast<float>(height_), 0.0f, 1.0f});
        backend_->setTexture(0, hdrTexture_);
        backend_->setSampler(0, linearSampler_);
        backend_->draw(3);
        backend_->endRenderPass();
    }

    if (useAntiAliasing) {
        // The resolve writes the image the copy pass hands to the output.
        TextureHandle resolved = resolveTexture_;
        if (antiAliasing_ == Aporia::AntiAliasingMode::TAA) {
            // The temporal resolve ping-pongs between two history images, so
            // the resolve never reads and writes the same texture.
            const TextureHandle historyRead = historyTexture_[historyReadIndex_];
            const TextureHandle historyWrite = historyTexture_[1 - historyReadIndex_];
            RenderPassDesc pass;
            pass.colorTargets = {RenderTargetBinding{RenderTargetBinding::Source::Texture,
                                                     historyWrite}};
            pass.clearColorEnabled = true;
            pass.clear.color[0] = 0.0f;
            pass.clear.color[1] = 0.0f;
            pass.clear.color[2] = 0.0f;
            pass.clear.color[3] = 1.0f;
            backend_->beginRenderPass(pass);
            backend_->setPipeline(taaPipeline_);
            backend_->setViewport(Viewport{0.0f, 0.0f, static_cast<float>(width_),
                                           static_cast<float>(height_), 0.0f, 1.0f});
            backend_->setTexture(0, ldrTexture_);
            backend_->setTexture(2, historyRead);
            backend_->setSampler(0, linearSampler_);
            backend_->draw(3);
            backend_->endRenderPass();
            resolved = historyWrite;
            historyReadIndex_ = 1 - historyReadIndex_;
            historyValid_ = true;
        } else {
            RenderPassDesc pass;
            pass.colorTargets = {RenderTargetBinding{RenderTargetBinding::Source::Texture,
                                                     resolveTexture_}};
            pass.clearColorEnabled = true;
            pass.clear.color[0] = 0.0f;
            pass.clear.color[1] = 0.0f;
            pass.clear.color[2] = 0.0f;
            pass.clear.color[3] = 1.0f;
            backend_->beginRenderPass(pass);
            backend_->setPipeline(aaPipeline_);
            backend_->setViewport(Viewport{0.0f, 0.0f, static_cast<float>(width_),
                                           static_cast<float>(height_), 0.0f, 1.0f});
            backend_->setTexture(0, ldrTexture_);
            backend_->setSampler(0, linearSampler_);
            backend_->draw(3);
            backend_->endRenderPass();
        }

        RenderPassDesc pass;
        pass.colorTargets = {outputBinding()};
        pass.clearColorEnabled = true;
        pass.clear.color[0] = 0.0f;
        pass.clear.color[1] = 0.0f;
        pass.clear.color[2] = 0.0f;
        pass.clear.color[3] = 1.0f;
        backend_->beginRenderPass(pass);
        backend_->setPipeline(native ? copyPipeline_ : copyLdrPipeline_);
        backend_->setViewport(Viewport{0.0f, 0.0f, static_cast<float>(width_),
                                       static_cast<float>(height_), 0.0f, 1.0f});
        backend_->setTexture(0, resolved);
        backend_->setSampler(0, linearSampler_);
        backend_->draw(3);
        backend_->endRenderPass();
    }

    // User interface, drawn in display space on top of the tone mapped image.
    if (!uiDrawList_.roundedRects.empty() || !uiDrawList_.textRuns.empty()) {
        RenderPassDesc pass;
        pass.colorTargets = {outputBinding()};
        pass.clearColorEnabled = false;
        backend_->beginRenderPass(pass);
        backend_->setViewport(Viewport{0.0f, 0.0f, static_cast<float>(width_),
                                       static_cast<float>(height_), 0.0f, 1.0f});
        drawUi();
        backend_->endRenderPass();
    }

    if (!backend_->endFrame(error)) {
        lastError_ = error;
        return false;
    }

    ++stats_.frameCount;
    ++stats_.presentCount;
    stats_.textureCount = static_cast<std::uint64_t>(textures_.size());
    stats_.boundTextureId = boundTexture_;
    std::uint64_t uploadCount = 0;
    for (const auto& entry : textures_) {
        uploadCount += entry.second.uploadCount;
    }
    stats_.textureUploadCount = uploadCount;
    stats_.lodGroupCount = static_cast<std::uint64_t>(lodGroups_.size());
    stats_.lodDrawTriangles = lodDrawTriangles;
    stats_.lodFullTriangles = lodFullTriangles;
    stats_.lodSwitchCount = lodSwitchCount_;
    stats_.shadowMapSize = shadowMapSize_;
    return true;
}

}  // namespace Aporia::graphics_internal
