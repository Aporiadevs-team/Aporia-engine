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
    static bool createDepthImage(
        VkDevice device,
        const VkPhysicalDeviceMemoryProperties& memoryProperties,
        VkFormat depthFormat,
        VkExtent2D extent,
        VkImage& outImage,
        VkDeviceMemory& outMemory,
        VkImageView& outView) noexcept;

    static bool createColorTarget(
        VkDevice device,
        const VkPhysicalDeviceMemoryProperties& memoryProperties,
        VkFormat depthFormat,
        VkFormat format,
        VkImageUsageFlags extraUsage,
        std::uint32_t width,
        std::uint32_t height,
        VkRenderPass compatibleRenderPass,
        VkImageView& outView,
        VkFramebuffer& outFramebuffer,
        VkImage& outImage,
        VkDeviceMemory& outMemory,
        VkImageView& outDepthView,
        VkImage& outDepthImage,
        VkDeviceMemory& outDepthMemory) noexcept;

    // Color+depth render pass. `colorFinalLayout` is SHADER_READ_ONLY for
    // sampleable targets (HDR tonemap, planar reflection) and PRESENT_SRC_KHR
    // or COLOR_ATTACHMENT_OPTIMAL for the main scene pass.
    static bool createRenderPass(
        VkDevice device,
        VkFormat colorFormat,
        VkFormat depthFormat,
        VkImageLayout colorFinalLayout,
        VkRenderPass& outRenderPass) noexcept;

    struct TonemapContext {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
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
