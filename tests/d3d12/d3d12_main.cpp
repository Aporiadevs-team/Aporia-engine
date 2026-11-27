// Direct3D 12 backend smoke test.
//
// Renders a lit triangle through the backend contract only (no engine types),
// reads the pixels back and checks that the triangle really landed on screen.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "Aporia/Graphics/Backend.hpp"

namespace {

constexpr wchar_t kWindowClass[] = L"AporiaD3D12TestWindow";
constexpr std::uint32_t kSize = 128;

LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_CLOSE) {
        DestroyWindow(window);
        return 0;
    }
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

struct Vertex {
    float position[3];
    float normal[3];
};

struct TestConstants {
    float viewProjection[16];
    float tint[4];
};

const char* vertexShaderSource = R"(
cbuffer Constants : register(b0) {
    float4x4 viewProjection;
    float4 tint;
};

struct VertexInput {
    float3 position : ATTRIBUTE0;
    float3 normal : ATTRIBUTE1;
};

struct VertexOutput {
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
};

VertexOutput main(VertexInput input) {
    VertexOutput output;
    output.position = mul(viewProjection, float4(input.position, 1.0f));
    output.normal = input.normal;
    return output;
}
)";

const char* fragmentShaderSource = R"(
cbuffer Constants : register(b0) {
    float4x4 viewProjection;
    float4 tint;
};

struct VertexOutput {
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
};

float4 main(VertexOutput input) : SV_TARGET {
    float3 lightDirection = normalize(float3(0.3, 0.5, 0.8));
    float lambert = saturate(dot(normalize(input.normal), lightDirection));
    return float4(tint.rgb * (0.25 + 0.75 * lambert), 1.0f);
}
)";

}  // namespace

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_OWNDC;
    windowClass.lpfnWndProc = windowProcedure;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = kWindowClass;
    RegisterClassExW(&windowClass);

    HWND window = CreateWindowExW(
        0, kWindowClass, L"", WS_POPUP, 0, 0, static_cast<int>(kSize), static_cast<int>(kSize),
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (window == nullptr) {
        std::printf("[d3d12] failed to create the test window\n");
        return 1;
    }
    ShowWindow(window, SW_SHOWNOACTIVATE);

    auto backend = Aporia::Backend::createBackend(Aporia::Backend::BackendKind::D3D12);
    if (!backend) {
        std::printf("[d3d12] backend is not compiled in, skipping\n");
        return 77;
    }

    std::string error;
    Aporia::Backend::BackendConfig config;
    config.nativeWindow = reinterpret_cast<std::uintptr_t>(window);
    config.width = kSize;
    config.height = kSize;
    if (!backend->initialize(config, error)) {
        std::printf("[d3d12] initialize failed: %s\n", error.c_str());
        return 77;  // no D3D12 capable GPU on this machine
    }
    std::printf("[d3d12] backend %s, api version %d, shader model %d\n",
                Aporia::Backend::toString(backend->kind()), backend->capabilities().apiVersion,
                backend->capabilities().shaderModel);

    Aporia::Backend::SwapchainDesc swapchain;
    swapchain.nativeWindow = config.nativeWindow;
    swapchain.width = kSize;
    swapchain.height = kSize;
    swapchain.vsync = false;
    swapchain.format = Aporia::Backend::PixelFormat::Rgba8Unorm;
    if (!backend->createSwapchain(swapchain, error)) {
        std::printf("[d3d12] createSwapchain failed: %s\n", error.c_str());
        return 1;
    }

    Aporia::Backend::TextureDesc targetDesc;
    targetDesc.width = kSize;
    targetDesc.height = kSize;
    targetDesc.format = Aporia::Backend::PixelFormat::Rgba8Unorm;
    targetDesc.usage =
        Aporia::Backend::TextureUsage::RenderTarget | Aporia::Backend::TextureUsage::ShaderResource;
    const Aporia::Backend::TextureHandle target = backend->createTexture(targetDesc);

    Aporia::Backend::TextureDesc depthDesc;
    depthDesc.width = kSize;
    depthDesc.height = kSize;
    depthDesc.format = Aporia::Backend::PixelFormat::Depth32Float;
    depthDesc.usage = Aporia::Backend::TextureUsage::DepthStencil;
    const Aporia::Backend::TextureHandle depth = backend->createTexture(depthDesc);

    const Vertex vertices[3] = {
        {{-0.5f, -0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}},
        {{0.5f, -0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}},
        {{0.0f, 0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}},
    };
    Aporia::Backend::BufferDesc vertexDesc;
    vertexDesc.size = sizeof(vertices);
    vertexDesc.usage = Aporia::Backend::BufferUsage::Vertex;
    vertexDesc.data.assign(
        reinterpret_cast<const std::uint8_t*>(vertices),
        reinterpret_cast<const std::uint8_t*>(vertices) + sizeof(vertices));
    const Aporia::Backend::BufferHandle vertexBuffer = backend->createBuffer(vertexDesc);

    const std::uint16_t indices[3] = {0, 1, 2};
    Aporia::Backend::BufferDesc indexDesc;
    indexDesc.size = sizeof(indices);
    indexDesc.usage = Aporia::Backend::BufferUsage::Index;
    indexDesc.data.assign(
        reinterpret_cast<const std::uint8_t*>(indices),
        reinterpret_cast<const std::uint8_t*>(indices) + sizeof(indices));
    const Aporia::Backend::BufferHandle indexBuffer = backend->createBuffer(indexDesc);

    Aporia::Backend::ShaderDesc vertexShaderDesc;
    vertexShaderDesc.stage = Aporia::Backend::ShaderStage::Vertex;
    vertexShaderDesc.source = vertexShaderSource;
    const Aporia::Backend::ShaderHandle vertexShader = backend->createShader(vertexShaderDesc, error);
    if (!vertexShader.valid()) {
        std::printf("[d3d12] vertex shader failed: %s\n", error.c_str());
        return 1;
    }

    Aporia::Backend::ShaderDesc fragmentShaderDesc;
    fragmentShaderDesc.stage = Aporia::Backend::ShaderStage::Fragment;
    fragmentShaderDesc.source = fragmentShaderSource;
    const Aporia::Backend::ShaderHandle fragmentShader =
        backend->createShader(fragmentShaderDesc, error);
    if (!fragmentShader.valid()) {
        std::printf("[d3d12] fragment shader failed: %s\n", error.c_str());
        return 1;
    }

    Aporia::Backend::PipelineDesc pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexStride = sizeof(Vertex);
    pipelineDesc.attributes = {
        {0, 0, Aporia::Backend::VertexFormat::Float32x3},
        {1, 12, Aporia::Backend::VertexFormat::Float32x3},
    };
    pipelineDesc.cull = Aporia::Backend::CullMode::None;
    pipelineDesc.hasDepth = true;
    pipelineDesc.depthFormat = Aporia::Backend::PixelFormat::Depth32Float;
    pipelineDesc.renderTargets = {{Aporia::Backend::PixelFormat::Rgba8Unorm}};
    const Aporia::Backend::PipelineHandle pipeline = backend->createPipeline(pipelineDesc, error);
    if (!pipeline.valid()) {
        std::printf("[d3d12] pipeline failed: %s\n", error.c_str());
        return 1;
    }

    TestConstants constants{};
    constants.viewProjection[0] = 1.0f;
    constants.viewProjection[5] = 1.0f;
    constants.viewProjection[10] = 1.0f;
    constants.viewProjection[15] = 1.0f;
    constants.tint[0] = 0.9f;
    constants.tint[1] = 0.4f;
    constants.tint[2] = 0.2f;
    constants.tint[3] = 1.0f;

    Aporia::Backend::Viewport viewport;
    viewport.width = static_cast<float>(kSize);
    viewport.height = static_cast<float>(kSize);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    Aporia::Backend::ScissorRect scissor{0, 0, static_cast<int>(kSize), static_cast<int>(kSize)};

    if (!backend->beginFrame(error)) {
        std::printf("[d3d12] beginFrame failed: %s\n", error.c_str());
        return 1;
    }
    Aporia::Backend::RenderPassDesc pass;
    pass.colorTargets.push_back(
        {Aporia::Backend::RenderTargetBinding::Source::Texture, target, 0, 0});
    pass.hasDepth = true;
    pass.depthTarget = {Aporia::Backend::RenderTargetBinding::Source::Texture, depth, 0, 0};
    pass.clear.color[0] = 0.05f;
    pass.clear.color[1] = 0.06f;
    pass.clear.color[2] = 0.08f;
    pass.clear.color[3] = 1.0f;
    backend->beginRenderPass(pass);
    backend->setPipeline(pipeline);
    backend->setViewport(viewport);
    backend->setScissor(scissor);
    backend->bindVertexBuffer(0, vertexBuffer, 0);
    backend->bindIndexBuffer(indexBuffer, Aporia::Backend::IndexFormat::Uint16);
    backend->setConstantBuffer(0, &constants, sizeof(constants));
    backend->drawIndexed(3, 1);
    backend->endRenderPass();
    if (!backend->endFrame(error)) {
        std::printf("[d3d12] endFrame failed: %s\n", error.c_str());
        return 1;
    }

    std::vector<std::uint8_t> pixels;
    if (!backend->readPixels(target, 0, 0, kSize, kSize, pixels)) {
        std::printf("[d3d12] readPixels failed\n");
        return 1;
    }
    std::size_t litPixels = 0;
    for (std::size_t index = 0; index + 3 < pixels.size(); index += 4) {
        if (pixels[index] > 40) {
            ++litPixels;
        }
    }
    std::printf("[d3d12] lit pixels: %zu of %zu\n", litPixels, pixels.size() / 4);
    if (litPixels < 100) {
        std::printf("[d3d12] FAIL: the triangle was not rasterized\n");
        return 1;
    }

    // One more frame straight into the swapchain, then present.
    if (!backend->beginFrame(error)) {
        std::printf("[d3d12] beginFrame failed: %s\n", error.c_str());
        return 1;
    }
    Aporia::Backend::RenderPassDesc swapPass;
    swapPass.colorTargets.push_back({Aporia::Backend::RenderTargetBinding::Source::Swapchain, {}, 0, 0});
    // The pipeline was built with a depth format, so the pass has to bind a
    // matching depth attachment.
    swapPass.hasDepth = true;
    swapPass.depthTarget = {Aporia::Backend::RenderTargetBinding::Source::Texture, depth, 0, 0};
    swapPass.clear.color[0] = 0.1f;
    swapPass.clear.color[1] = 0.12f;
    swapPass.clear.color[2] = 0.2f;
    swapPass.clear.color[3] = 1.0f;
    backend->beginRenderPass(swapPass);
    backend->setPipeline(pipeline);
    backend->setViewport(viewport);
    backend->setScissor(scissor);
    backend->bindVertexBuffer(0, vertexBuffer, 0);
    backend->bindIndexBuffer(indexBuffer, Aporia::Backend::IndexFormat::Uint16);
    backend->setConstantBuffer(0, &constants, sizeof(constants));
    backend->drawIndexed(3, 1);
    backend->endRenderPass();
    if (!backend->endFrame(error)) {
        std::printf("[d3d12] endFrame failed: %s\n", error.c_str());
        return 1;
    }

    std::printf("[d3d12] OK\n");
    return 0;
}
