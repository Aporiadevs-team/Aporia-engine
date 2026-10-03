#pragma once

#include "Aporia/Graphics/Graphics.hpp"

#include <vulkan/vulkan.h>
#include <cstdint>

namespace Aporia::graphics_internal {

class RectRenderer {
public:
    struct PushConstants {
        float rect[4];
        float color[4];
        float radius[4];
        float smoothing[4];
        float blur[4];
    };

    void drawRect(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout,
                  const UiRect& rect, const UiColor& color) const noexcept;
    void drawRoundedRect(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout,
                         const UiRect& rect, const UiColor& color, float radius) const noexcept;
    void drawBlurredRect(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout,
                         const UiRect& rect, const UiColor& color, float radius,
                         const BlurConfig& blur) const noexcept;
    void draw(
        VkCommandBuffer commandBuffer,
        VkPipelineLayout pipelineLayout,
        std::uint32_t width,
        std::uint32_t height) const noexcept;

    void setDrawList(const UiDrawList& drawList) { drawList_ = drawList; }
    void clear() noexcept { drawList_ = {}; }
    [[nodiscard]] const UiDrawList& drawList() const noexcept { return drawList_; }

private:
    UiDrawList drawList_{};
};

}
