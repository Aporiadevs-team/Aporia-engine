#pragma once

#include "Aporia/Graphics/Graphics.hpp"

#include <cstdint>
#include <vulkan/vulkan.h>

namespace Aporia::graphics_internal {

// Maps a public TextureFormat to the Vulkan format and its byte size. Returns
// false for formats the texture path cannot upload.
[[nodiscard]] bool textureFormatInfo(
    Aporia::TextureFormat format,
    VkFormat& vulkanFormat,
    std::uint32_t& bytesPerPixel);

} // namespace Aporia::graphics_internal
