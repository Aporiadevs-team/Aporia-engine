#pragma once

#include "Aporia/Graphics/Graphics.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace Aporia::graphics_internal {

// A baked font: RGBA8 pixels (alpha carries coverage for TTF, RGB distance for
// MSDF) plus the glyph metric table ready to hand to TextRenderer.
struct BakedFontAtlas {
    std::vector<std::uint8_t> pixels;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    UiTextAtlasDesc atlas{};
};

// Rasterizes TTF/OTF bytes into an alpha coverage atlas at the given pixel size.
// RGB is white, alpha is coverage; atlas.mode is TtfBitmap.
[[nodiscard]] BakedFontAtlas bakeTtfAtlas(
    const std::vector<std::uint8_t>& fontBytes,
    float pixelSize,
    float oversample = 2.0f);

// Loads an msdf-atlas-gen JSON description and its PNG atlas image.
// atlas.mode is Msdf.
[[nodiscard]] BakedFontAtlas loadMsdfAtlas(
    const std::string& json,
    const std::vector<std::uint8_t>& pngBytes);

[[nodiscard]] std::vector<std::uint8_t> readFileBytes(const std::string& path);
[[nodiscard]] std::string readFileText(const std::string& path);

} // namespace Aporia::graphics_internal
