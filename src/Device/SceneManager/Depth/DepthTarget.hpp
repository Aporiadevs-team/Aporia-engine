#pragma once

#if defined(_WIN32) && !defined(VK_USE_PLATFORM_WIN32_KHR)
#define VK_USE_PLATFORM_WIN32_KHR
#endif

#include <vulkan/vulkan.h>

#include <cstdint>

namespace Aporia::graphics_internal {

// Depth attachment creation for scene render targets.
struct DepthTarget {
    static bool create(
        VkDevice device,
        const VkPhysicalDeviceMemoryProperties& memoryProperties,
        VkFormat depthFormat,
        VkExtent2D extent,
        VkImage& outImage,
        VkDeviceMemory& outMemory,
        VkImageView& outView) noexcept;
};

} // namespace Aporia::graphics_internal
