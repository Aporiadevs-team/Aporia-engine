#pragma once

#if defined(_WIN32) && !defined(VK_USE_PLATFORM_WIN32_KHR)
#define VK_USE_PLATFORM_WIN32_KHR
#endif

#include <vulkan/vulkan.h>

namespace Aporia::graphics_internal {

// Picks the first candidate format the physical device can use as a color
// attachment. Returns VK_FORMAT_UNDEFINED when none of them qualify.
VkFormat chooseColorFormat(VkPhysicalDevice physicalDevice, VkFormat& format);
// First 16F/32F format usable as both a color attachment and a sampled image.
VkFormat chooseHdrFormat(VkPhysicalDevice physicalDevice);
// First depth (optionally stencil) format the device supports.
VkFormat chooseDepthFormat(VkPhysicalDevice physicalDevice);

} // namespace Aporia::graphics_internal
