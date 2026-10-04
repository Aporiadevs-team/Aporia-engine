#include "HdrRenderer.hpp"
#include "../Rectangulars/RectRenderer.hpp"

#include <array>

namespace Aporia::graphics_internal {

void HdrRenderer::renderTonemap(
        const TonemapContext& context,
        VkFramebuffer target,
        std::uint32_t width,
        std::uint32_t height,
        const std::function<void(VkCommandBuffer, VkPipelineLayout)>& overlay) noexcept {
    if (context.commandBuffer == VK_NULL_HANDLE || context.renderPass == VK_NULL_HANDLE ||
        target == VK_NULL_HANDLE || width == 0 || height == 0) {
        return;
    }

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    begin.renderPass = context.renderPass;
    begin.framebuffer = target;
    begin.renderArea.extent = {width, height};
    begin.clearValueCount = static_cast<std::uint32_t>(clearValues.size());
    begin.pClearValues = clearValues.data();
    vkCmdBeginRenderPass(context.commandBuffer, &begin, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{};
    viewport.width = static_cast<float>(width);
    viewport.height = static_cast<float>(height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(context.commandBuffer, 0, 1, &viewport);
    VkRect2D scissor{};
    scissor.extent = {width, height};
    vkCmdSetScissor(context.commandBuffer, 0, 1, &scissor);

    vkCmdBindPipeline(context.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, context.pipeline);
    vkCmdBindDescriptorSets(context.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            context.pipelineLayout, 0, 1, &context.descriptorSet, 0, nullptr);
    // Base resolve: neutral UI push constants draw the tonemapped frame across
    // the whole target; the appended AA block carries the live resolve strength
    // and mode. Overlay draws then blend UI on top with their own (shorter) push.
    struct TonemapPush {
        graphics_internal::RectRenderer::PushConstants ui;
        float aa[4];
    };
    TonemapPush push{};
    push.aa[0] = context.antiAliasingStrength;
    push.aa[1] = static_cast<float>(context.antiAliasingMode);
    vkCmdPushConstants(context.commandBuffer, context.pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(push), &push);
    vkCmdDraw(context.commandBuffer, 4, 1, 0, 0);
    if (overlay) {
        // The UI pass is a separate pipeline (SDF/blur) blended on top.
        if (context.uiPipeline != VK_NULL_HANDLE) {
            vkCmdBindPipeline(context.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, context.uiPipeline);
        }
        overlay(context.commandBuffer, context.pipelineLayout);
    }
    vkCmdEndRenderPass(context.commandBuffer);
}

} // namespace Aporia::graphics_internal
