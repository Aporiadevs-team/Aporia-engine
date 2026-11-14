#include "Graphics/GlobalDrawer/GraphicsImpl.hpp"

namespace Aporia {

void Graphics::Impl::renderShadowMap(VkCommandBuffer buffer, const float sunMatrix[16], std::uint32_t uniformSlot) {
    graphics_internal::ShadowRenderer::RenderContext context;
    context.commandBuffer = buffer;
    context.renderPass = shadow.renderPass;
    context.framebuffer = shadow.framebuffer;
    context.mapSize = shadowMapSize;
    context.pipeline = pipelines.shadowPipeline;
    context.pipelineLayout = pipelines.shadowPipelineLayout;
    // The sun matrix lives in the per-frame-slot uniform buffer; binding slot 0
    // fed the pass a frame-old sun matrix whenever the camera moved.
    context.descriptorSet = scene.set(uniformSlot);
    graphics_internal::ShadowRenderer::renderShadowMap(context, sunMatrix, pendingItems, meshStore.resources());
}

} // namespace Aporia
