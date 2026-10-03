#include "Support.hpp"

#include <array>
#include <fstream>

namespace Aporia::graphics_internal {

QualityParameters qualityParameters(Aporia::GraphicsQuality quality) noexcept {
    switch (quality) {
    case Aporia::GraphicsQuality::Low:
        return {512, 1, false, false, 0.0f, 0.22f, 50};
    case Aporia::GraphicsQuality::Medium:
        return {1024, 9, true, true, 0.5f, 0.16f, 75};
    case Aporia::GraphicsQuality::High:
    default:
        return {4096, 49, true, true, 1.0f, 0.10f, 100};
    }
}

VkFormat chooseColorFormat(VkPhysicalDevice physicalDevice, VkFormat& format) {
    constexpr std::array<VkFormat, 4> candidates{
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_B8G8R8A8_UNORM,
        VK_FORMAT_A8B8G8R8_UNORM_PACK32,
        VK_FORMAT_R8G8B8A8_SRGB,
    };
    for (const VkFormat candidate : candidates) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(physicalDevice, candidate, &properties);
        if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) != 0) {
            format = candidate;
            return candidate;
        }
    }
    return VK_FORMAT_UNDEFINED;
}

VkFormat chooseHdrFormat(VkPhysicalDevice physicalDevice) {
    constexpr std::array<VkFormat, 2> candidates{
        VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_FORMAT_R32G32B32A32_SFLOAT,
    };
    for (const VkFormat candidate : candidates) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(physicalDevice, candidate, &properties);
        if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) != 0 &&
            (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0) {
            return candidate;
        }
    }
    return VK_FORMAT_UNDEFINED;
}

VkFormat chooseDepthFormat(VkPhysicalDevice physicalDevice) {
    const VkFormat candidates[] = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT,
    };
    for (const VkFormat candidate : candidates) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(physicalDevice, candidate, &properties);
        if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0) {
            return candidate;
        }
    }
    return VK_FORMAT_UNDEFINED;
}

bool textureFormatInfo(
        Aporia::TextureFormat format,
        VkFormat& vulkanFormat,
        std::uint32_t& bytesPerPixel) {
    switch (format) {
    case Aporia::TextureFormat::Rgba8Unorm:
        vulkanFormat = VK_FORMAT_R8G8B8A8_UNORM;
        bytesPerPixel = 4;
        return true;
    case Aporia::TextureFormat::Bgra8Unorm:
        vulkanFormat = VK_FORMAT_B8G8R8A8_UNORM;
        bytesPerPixel = 4;
        return true;
    case Aporia::TextureFormat::Rgba8Srgb:
        vulkanFormat = VK_FORMAT_R8G8B8A8_SRGB;
        bytesPerPixel = 4;
        return true;
    }
    return false;
}

std::vector<std::uint32_t> readSpirv(const std::string& path, bool& success) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        success = false;
        return {};
    }
    const std::streamoff byteCount = file.tellg();
    if (byteCount <= 0 || byteCount % static_cast<std::streamoff>(sizeof(std::uint32_t)) != 0) {
        success = false;
        return {};
    }
    std::vector<std::uint32_t> words(static_cast<std::size_t>(byteCount) / sizeof(std::uint32_t));
    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(words.data()), byteCount)) {
        success = false;
        return {};
    }
    success = true;
    return words;
}

bool chooseSurfaceFormat(
        VkPhysicalDevice physicalDevice,
        VkSurfaceKHR surface,
        VkSurfaceFormatKHR& selectedFormat) {
    std::uint32_t count = 0;
    if (vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &count, nullptr) != VK_SUCCESS || count == 0) {
        return false;
    }
    std::vector<VkSurfaceFormatKHR> formats(count);
    if (vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &count, formats.data()) != VK_SUCCESS) {
        return false;
    }
    for (const VkSurfaceFormatKHR& format : formats) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format.format, &properties);
        if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) != 0) {
            selectedFormat = format;
            return true;
        }
    }
    return false;
}

VkPresentModeKHR choosePresentMode(
        VkPhysicalDevice physicalDevice,
        VkSurfaceKHR surface) {
    std::uint32_t count = 0;
    if (vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &count, nullptr) != VK_SUCCESS || count == 0) {
        return VK_PRESENT_MODE_IMMEDIATE_KHR;
    }
    std::vector<VkPresentModeKHR> modes(count);
    if (vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &count, modes.data()) != VK_SUCCESS) {
        return VK_PRESENT_MODE_IMMEDIATE_KHR;
    }
    for (const VkPresentModeKHR mode : modes) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return mode;
        }
    }
    for (const VkPresentModeKHR mode : modes) {
        if (mode == VK_PRESENT_MODE_FIFO_KHR) {
            return mode;
        }
    }
    return modes.front();
}

#if defined(_WIN32)
bool getWindowExtent(HWND window, VkExtent2D& extent) noexcept {
    RECT clientRect{};
    if (window == nullptr || !GetClientRect(window, &clientRect)) {
        return false;
    }
    const int width = clientRect.right - clientRect.left;
    const int height = clientRect.bottom - clientRect.top;
    if (width <= 0 || height <= 0) {
        return false;
    }
    extent.width = static_cast<std::uint32_t>(width);
    extent.height = static_cast<std::uint32_t>(height);
    return true;
}
#endif

} // namespace Aporia::graphics_internal
