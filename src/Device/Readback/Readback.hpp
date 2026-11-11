#pragma once

#if defined(_WIN32) && !defined(VK_USE_PLATFORM_WIN32_KHR)
#define VK_USE_PLATFORM_WIN32_KHR
#endif

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace Aporia::graphics_internal {

struct ReadbackContext {
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkImage colorImage = VK_NULL_HANDLE;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    bool isHdrFormat = false;  // true = R16G16B16A16_SFLOAT, false = RGBA8
};

// Copies the color image into host memory as tightly-packed RGBA8. The source
// image is expected to be COLOR_ATTACHMENT_OPTIMAL on entry and is left in that
// layout on success.
bool readbackColorImage(const ReadbackContext& context, std::vector<std::uint8_t>& pixels);

} // namespace Aporia::graphics_internal
