#pragma once

#if defined(_WIN32) && !defined(VK_USE_PLATFORM_WIN32_KHR)
#define VK_USE_PLATFORM_WIN32_KHR
#endif

#include <vulkan/vulkan.h>

namespace Aporia::graphics_internal {

// Color + depth render pass shared by the scene, HDR and reflection targets.
// The caller supplies the final color layout (SHADER_READ_ONLY for sampleable
// targets, PRESENT_SRC/COLOR_ATTACHMENT for the presentation targets).
struct RenderPassFactory {
    static bool create(
        VkDevice device,
        VkFormat colorFormat,
        VkFormat depthFormat,
        VkImageLayout colorFinalLayout,
        VkRenderPass& outRenderPass) noexcept;

    // Append variant: colour is loaded (not cleared) and left sampleable, so a
    // composite target can be blurred and then have more UI drawn onto it.
    static bool createLoad(
        VkDevice device,
        VkFormat colorFormat,
        VkFormat depthFormat,
        VkRenderPass& outRenderPass) noexcept;
};

} // namespace Aporia::graphics_internal
