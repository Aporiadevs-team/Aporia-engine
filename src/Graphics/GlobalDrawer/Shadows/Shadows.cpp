#include "Graphics/GlobalDrawer/GraphicsImpl.hpp"

namespace Aporia {

void Graphics::Impl::renderShadowMap(VkCommandBuffer buffer, const float sunMatrix[16]) {
    graphics_internal::ShadowRenderer::RenderContext context;
    context.commandBuffer = buffer;
    context.renderPass = shadow.renderPass;
    context.framebuffer = shadow.framebuffer;
    context.mapSize = shadowMapSize;
    context.pipeline = pipelines.shadowPipeline;
    context.pipelineLayout = pipelines.shadowPipelineLayout;
    context.descriptorSet = scene.set(0);
    graphics_internal::ShadowRenderer::renderShadowMap(context, sunMatrix, pendingItems, meshStore.resources());
}

} // namespace Aporia
