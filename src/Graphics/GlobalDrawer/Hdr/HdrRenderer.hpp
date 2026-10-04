#pragma once

#include "Aporia/Graphics/Graphics.hpp"

#include <cstdint>
#include <functional>
#include <vulkan/vulkan.h>

namespace Aporia::graphics_internal {

// Owns HDR/tonemap render passes and the color/depth targets they render into.
// The tonemap pass itself is recorded by Graphics; this module owns only the
// GPU resources and their creation/destruction.
class HdrRenderer {
public:
    struct TonemapContext {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkPipeline uiPipeline = VK_NULL_HANDLE;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        float antiAliasingStrength = 0.0f;
        std::int32_t antiAliasingMode = 0;
    };

    // Fullscreen tonemap pass from the HDR target into the final target. The
    // overlay callback lets the caller draw UI inside the same render pass.
    static void renderTonemap(
        const TonemapContext& context,
        VkFramebuffer target,
        std::uint32_t width,
        std::uint32_t height,
        const std::function<void(VkCommandBuffer, VkPipelineLayout)>& overlay) noexcept;

    void setQuality(GraphicsQuality quality) noexcept { quality_ = quality; }
    void setAntiAliasing(AntiAliasingMode mode) noexcept { antiAliasing_ = mode; }
    [[nodiscard]] GraphicsQuality quality() const noexcept { return quality_; }
    [[nodiscard]] AntiAliasingMode antiAliasing() const noexcept { return antiAliasing_; }

private:
    GraphicsQuality quality_ = GraphicsQuality::Medium;
    AntiAliasingMode antiAliasing_ = AntiAliasingMode::SMAA;
};

} // namespace Aporia::graphics_internal
