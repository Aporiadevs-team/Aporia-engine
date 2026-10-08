#include "HdrRenderer.hpp"
#include "../Rectangulars/RectRenderer.hpp"

#include <array>

namespace Aporia::graphics_internal {

void HdrRenderer::renderTonemap(
        const TonemapContext& context,
        VkFramebuffer target,
        std::uint32_t width,
        std::uint32_t height,
        const VkCommandBuffer* commands,
        std::uint32_t commandCount) noexcept {
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
    vkCmdBeginRenderPass(context.commandBuffer, &begin, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
    if (commands != nullptr && commandCount > 0) {
        vkCmdExecuteCommands(context.commandBuffer, commandCount, commands);
    }
    vkCmdEndRenderPass(context.commandBuffer);
}

} // namespace Aporia::graphics_internal
