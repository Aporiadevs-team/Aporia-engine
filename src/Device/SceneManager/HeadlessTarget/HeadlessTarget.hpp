#pragma once

#if defined(_WIN32) && !defined(VK_USE_PLATFORM_WIN32_KHR)
#define VK_USE_PLATFORM_WIN32_KHR
#endif

#include <vulkan/vulkan.h>

#include <cstdint>

namespace Aporia::graphics_internal {

// Offscreen color target + framebuffer for headless rendering (no swapchain).
struct HeadlessTarget {
    static bool create(
        VkDevice device,
        const VkPhysicalDeviceMemoryProperties& memoryProperties,
        VkFormat colorFormat,
        VkExtent2D extent,
        VkRenderPass renderPass,
        VkImageView depthView,
        VkImage& outImage,
        VkDeviceMemory& outMemory,
        VkImageView& outView,
        VkFramebuffer& outFramebuffer) noexcept;

    static void destroy(
        VkDevice device,
        VkFramebuffer& framebuffer,
        VkImageView& view,
        VkImage& image,
        VkDeviceMemory& memory) noexcept;
};

} // namespace Aporia::graphics_internal
