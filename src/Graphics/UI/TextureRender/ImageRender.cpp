#include "ImageRender.hpp"

#include "Aporia/Graphics/Graphics.hpp"

#include <cstring>

#include <stb_image.h>

namespace Aporia::graphics_internal {

bool ImageRender::decodeImage(const std::string& path, Image& out) {
    int width = 0;
    int height = 0;
    int channels = 0;
    // Force straight RGBA8 regardless of source color type; stb owns inflate
    // (PNG) and DCT/Huffman (JPEG). stb_image picks PNG vs JPEG by content.
    stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
    if (pixels == nullptr || width <= 0 || height <= 0) {
        out = Image{};
        return false;
    }

    const std::size_t byteCount =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    out.width = static_cast<std::uint32_t>(width);
    out.height = static_cast<std::uint32_t>(height);
    out.rgba.assign(pixels, pixels + byteCount);
    stbi_image_free(pixels);
    return true;
}

bool ImageRender::loadImage(Aporia::Graphics& graphics, const std::string& imagePath) {
    Image image;
    if (!decodeImage(imagePath, image)) {
        loaded_ = false;
        return false;
    }

    Aporia::TextureDesc desc{};
    desc.width = image.width;
    desc.height = image.height;
    desc.format = Aporia::TextureFormat::Rgba8Unorm;
    desc.pixels = image.rgba.data();
    desc.dataSize = image.rgba.size();
    desc.wrap = Aporia::SamplerMode::Repeat;

    const Aporia::TextureHandle texture = graphics.createTexture2D(desc);
    if (!texture.valid()) {
        loaded_ = false;
        return false;
    }

    textureValue_ = texture.value();
    width_ = image.width;
    height_ = image.height;
    loaded_ = true;
    return true;
}

bool ImageRender::render(
        Aporia::Graphics& graphics,
        const Aporia::CameraState& camera,
        Aporia::MeshHandle mesh,
        const float modelMatrix16[16]) const {
    if (!loaded_ || textureValue_ == 0) {
        return false;
    }

    Aporia::RenderItem item{};
    item.meshId = mesh.value();
    item.texture = Aporia::TextureHandle(textureValue_);
    item.transparent = true;
    item.color[0] = 1.0f;
    item.color[1] = 1.0f;
    item.color[2] = 1.0f;
    item.color[3] = 1.0f;
    std::memcpy(item.modelMatrix, modelMatrix16, sizeof(float) * 16);

    return graphics.render(camera, {item});
}

bool ImageRender::renderImage(
        Aporia::Graphics& graphics,
        const Aporia::CameraState& camera,
        const std::string& imagePath,
        Aporia::MeshHandle mesh,
        const float modelMatrix16[16]) {
    if (!loadImage(graphics, imagePath)) {
        return false;
    }
    return render(graphics, camera, mesh, modelMatrix16);
}

} // namespace Aporia::graphics_internal
