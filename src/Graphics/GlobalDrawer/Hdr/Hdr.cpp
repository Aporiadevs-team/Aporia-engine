#include "Graphics/GlobalDrawer/GraphicsImpl.hpp"

namespace Aporia {

void Graphics::Impl::renderTonemap(VkFramebuffer target, std::uint32_t width, std::uint32_t height) {
    graphics_internal::HdrRenderer::TonemapContext context;
    context.commandBuffer = commandBuffer;
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
            rectRenderer.draw(buffer, layout, width, height, uiDrawList);
            renderUiText(buffer, width, height);
        });
}

} // namespace Aporia
