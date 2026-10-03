#include "RectRenderer.hpp"

namespace Aporia::graphics_internal {

namespace {

RectRenderer::PushConstants makePushConstants(
        const UiRect& rect,
        const UiColor& color,
        float radius,
        const BlurConfig& blur) noexcept {
    RectRenderer::PushConstants push{};
    push.rect[0] = rect.x;
    push.rect[1] = rect.y;
    push.rect[2] = rect.width;
    push.rect[3] = rect.height;
    push.color[0] = color.r;
    push.color[1] = color.g;
    push.color[2] = color.b;
    push.color[3] = color.a;
    push.radius[0] = radius;
    push.radius[1] = radius;
    push.radius[2] = radius;
    push.radius[3] = radius;
    push.smoothing[0] = 1.0f;
    push.blur[0] = static_cast<float>(blur.mode);
    push.blur[1] = blur.radius;
    push.blur[2] = blur.intensity;
    return push;
}

void submitQuad(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout,
                const RectRenderer::PushConstants& push) noexcept {
    vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(push), &push);
    vkCmdDraw(commandBuffer, 4, 1, 0, 0);
}

} // namespace

void RectRenderer::drawRect(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout,
                            const UiRect& rect, const UiColor& color) const noexcept {
    submitQuad(commandBuffer, pipelineLayout, makePushConstants(rect, color, 0.0f, {}));
}

void RectRenderer::drawRoundedRect(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout,
                                   const UiRect& rect, const UiColor& color,
                                   float radius) const noexcept {
    submitQuad(commandBuffer, pipelineLayout, makePushConstants(rect, color, radius, {}));
}

void RectRenderer::drawBlurredRect(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout,
                                   const UiRect& rect, const UiColor& color, float radius,
                                   const BlurConfig& blur) const noexcept {
    submitQuad(commandBuffer, pipelineLayout, makePushConstants(rect, color, radius, blur));
}

void RectRenderer::draw(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout,
                        std::uint32_t width, std::uint32_t height,
                        const UiDrawList& drawList) const noexcept {
    if (drawList.roundedRects.empty() || commandBuffer == VK_NULL_HANDLE ||
        pipelineLayout == VK_NULL_HANDLE || width == 0 || height == 0) {
        return;
    }

    for (const auto& item : drawList.roundedRects) {
        if (drawList.blur.mode != BlurMode::None) {
            drawBlurredRect(commandBuffer, pipelineLayout, item.first, item.second,
                            drawList.cornerRadius, drawList.blur);
        } else if (drawList.cornerRadius > 0.0f) {
            drawRoundedRect(commandBuffer, pipelineLayout, item.first, item.second,
                            drawList.cornerRadius);
        } else {
            drawRect(commandBuffer, pipelineLayout, item.first, item.second);
        }
    }
}

} // namespace Aporia::graphics_internal
