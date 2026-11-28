#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Aporia {
class Graphics;
struct CameraState;
class MeshHandle;
}

namespace Aporia::graphics_internal {

// ImageRender: render a full PNG or JPEG image, no fuss.
//
// Minimal image renderer as its own responsibility. Decoding has no external
// dependencies: it uses the single-header stb_image already vendored in
// extern/stb (its own inflate/DCT; no zlib/libjpeg to link, no new library).
// Both PNG and JPEG are decoded to straight RGBA8; the only difference between
// formats is at decode time. The CPU side only ever touches the C++ standard
// library; the only non-std step is handing RGBA8 to the engine's existing
// texture/mesh pipeline (texture.vert/texture.frag). PNG may carry alpha;
// JPEG is always opaque (alpha=255).
class ImageRender {
public:
    struct Image {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::vector<std::uint8_t> rgba;  // straight RGBA8, width*height*4
    };

    // Decode a PNG or JPEG file into RGBA8. Returns false on missing file /
    // unsupported or bad image. No external dependencies (header-only stb_image; STL output).
    [[nodiscard]] static bool decodeImage(const std::string& path, Image& out);

    // Decode PNG. Thin alias of decodeImage (stb_image picks by content/extension).
    [[nodiscard]] static bool decodePng(const std::string& path, Image& out) { return decodeImage(path, out); }
    // Decode JPEG. Thin alias of decodeImage (stb_image picks by content/extension).
    [[nodiscard]] static bool decodeJpeg(const std::string& path, Image& out) { return decodeImage(path, out); }

    // Decode `imagePath`, upload a fresh engine texture, and keep it ready to
    // render. Works for PNG and JPEG. Returns false on decode/upload failure.
    [[nodiscard]] bool loadImage(Aporia::Graphics& graphics, const std::string& imagePath);
    // Thin helpers.
    [[nodiscard]] bool loadPng(Aporia::Graphics& graphics, const std::string& pngPath) { return loadImage(graphics, pngPath); }
    [[nodiscard]] bool loadJpeg(Aporia::Graphics& graphics, const std::string& jpegPath) { return loadImage(graphics, jpegPath); }

    // Draw the loaded image as a textured unit quad (screen/3D, caller camera).
    // `mesh` is a unit plane mesh; `modelMatrix16` is a column-major 4x4.
    // Returns false if nothing is loaded or render fails.
    [[nodiscard]] bool render(
        Aporia::Graphics& graphics,
        const Aporia::CameraState& camera,
        Aporia::MeshHandle mesh,
        const float modelMatrix16[16]) const;

    // Convenience: load + render in one call.
    [[nodiscard]] bool renderImage(
        Aporia::Graphics& graphics,
        const Aporia::CameraState& camera,
        const std::string& imagePath,
        Aporia::MeshHandle mesh,
        const float modelMatrix16[16]);

    [[nodiscard]] bool loaded() const noexcept { return loaded_; }
    [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }

private:
    bool loaded_ = false;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    // Engine texture handle stored opaquely to keep this header light.
    std::uint64_t textureValue_ = 0;
};

} // namespace Aporia::graphics_internal
