// Direct3D 12 backend: implements the backend contract with D3D12 devices,
// command lists, render targets and pipelines.
#pragma once

#include <memory>

#include "Aporia/Graphics/Backend.hpp"

namespace Aporia::Backend {

class D3D12Backend final : public GraphicsBackend {
public:
    D3D12Backend();
    ~D3D12Backend() override;

    D3D12Backend(const D3D12Backend&) = delete;
    D3D12Backend& operator=(const D3D12Backend&) = delete;

    BackendKind kind() const override;
    const BackendCapabilities& capabilities() const override;

    bool initialize(const BackendConfig& config, std::string& error) override;
    void shutdown() override;

    bool createSwapchain(const SwapchainDesc& desc, std::string& error) override;
    void resizeSwapchain(std::uint32_t width, std::uint32_t height) override;
    void setVsync(bool enabled) override;

    bool beginFrame(std::string& error) override;
    void beginRenderPass(const RenderPassDesc& desc) override;
    void endRenderPass() override;
    bool endFrame(std::string& error) override;
    void waitIdle() override;

    void setPipeline(PipelineHandle pipeline) override;
    void setViewport(const Viewport& viewport) override;
    void setScissor(const ScissorRect& scissor) override;
    void bindVertexBuffer(std::uint32_t slot, BufferHandle buffer, std::uint32_t offset) override;
    void bindIndexBuffer(BufferHandle buffer, IndexFormat format) override;
    void setConstantBuffer(std::uint32_t slot, const void* data, std::size_t size) override;
    void setTexture(std::uint32_t slot, TextureHandle texture) override;
    void setSampler(std::uint32_t slot, SamplerHandle sampler) override;
    void draw(std::uint32_t vertexCount, std::uint32_t instanceCount = 1) override;
    void drawIndexed(std::uint32_t indexCount, std::uint32_t instanceCount = 1) override;

    BufferHandle createBuffer(const BufferDesc& desc) override;
    void destroyBuffer(BufferHandle buffer) override;
    bool updateBuffer(BufferHandle buffer, const void* data, std::size_t size) override;
    TextureHandle createTexture(const TextureDesc& desc) override;
    void destroyTexture(TextureHandle texture) override;
    SamplerHandle createSampler(const SamplerDesc& desc) override;
    void destroySampler(SamplerHandle sampler) override;
    ShaderHandle createShader(const ShaderDesc& desc, std::string& error) override;
    void destroyShader(ShaderHandle shader) override;
    PipelineHandle createPipeline(const PipelineDesc& desc, std::string& error) override;
    void destroyPipeline(PipelineHandle pipeline) override;

    bool readPixels(
        TextureHandle texture, std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height,
        std::vector<std::uint8_t>& out) override;

private:
    void bindDescriptorTables();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace Aporia::Backend
