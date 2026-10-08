#include "Graphics/GlobalDrawer/GraphicsImpl.hpp"

namespace Aporia {

void Graphics::Impl::recordUiOverlay(VkCommandBuffer cmd, VkPipelineLayout layout, std::uint32_t width, std::uint32_t height) {
    if (pipelines.uiPipeline != VK_NULL_HANDLE) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.uiPipeline);
    }
    rectRenderer.draw(cmd, layout, width, height, uiDrawList);
    renderUiText(cmd, width, height);
}

void Graphics::Impl::renderTonemap(VkCommandBuffer cmd, VkFramebuffer target, std::uint32_t width, std::uint32_t height) {
    graphics_internal::HdrRenderer::TonemapContext context;
    context.commandBuffer = cmd;
    context.renderPass = renderPass;
    context.pipeline = pipelines.tonemapPipeline;
    context.uiPipeline = pipelines.uiPipeline;
    context.pipelineLayout = pipelines.tonemapPipelineLayout;
    context.descriptorSet = tonemapDescriptorSet;
    context.antiAliasingStrength = antiAliasingStrength;
    context.antiAliasingMode = static_cast<std::int32_t>(antiAliasing);
    graphics_internal::HdrRenderer::renderTonemap(
        context, target, width, height,
        [this, width, height](VkCommandBuffer buffer, VkPipelineLayout layout) {
            recordUiOverlay(buffer, layout, width, height);
        });
}

} // namespace Aporia
