#include "TextureFormat.hpp"

namespace Aporia::graphics_internal {

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

} // namespace Aporia::graphics_internal
