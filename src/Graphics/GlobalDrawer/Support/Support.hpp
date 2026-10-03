#pragma once

#include "Aporia/Graphics/Graphics.hpp"

#include <cstdint>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace Aporia::graphics_internal {

// Quality preset parameter table: one place scales the whole picture.
struct QualityParameters {
    std::uint32_t shadowMapSize;
    int shadowTaps;          // specialization constant: 1, 9, 49
    bool hdr;
    bool reflections;
    float reflectionScale;   // reflection target relative to main target
    float shadowStrength;    // ambient floor inside shadows
    std::uint32_t textureScalePercent;
};

[[nodiscard]] QualityParameters qualityParameters(Aporia::GraphicsQuality quality) noexcept;
[[nodiscard]] VkFormat chooseColorFormat(VkPhysicalDevice physicalDevice, VkFormat& format);
[[nodiscard]] VkFormat chooseHdrFormat(VkPhysicalDevice physicalDevice);
[[nodiscard]] VkFormat chooseDepthFormat(VkPhysicalDevice physicalDevice);
[[nodiscard]] bool textureFormatInfo(
    Aporia::TextureFormat format,
    VkFormat& vulkanFormat,
    std::uint32_t& bytesPerPixel);

[[nodiscard]] std::vector<std::uint32_t> readSpirv(const std::string& path, bool& success);

[[nodiscard]] bool chooseSurfaceFormat(
    VkPhysicalDevice physicalDevice,
    VkSurfaceKHR surface,
    VkSurfaceFormatKHR& selectedFormat);
[[nodiscard]] VkPresentModeKHR choosePresentMode(
    VkPhysicalDevice physicalDevice,
    VkSurfaceKHR surface,
    bool vsync);

#if defined(_WIN32)
[[nodiscard]] bool getWindowExtent(HWND window, VkExtent2D& extent) noexcept;
#endif

} // namespace Aporia::graphics_internal
