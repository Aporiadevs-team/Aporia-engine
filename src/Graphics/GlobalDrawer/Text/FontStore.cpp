#include "FontStore.hpp"

#include "Device/SceneManager/SceneBindings/SceneBindings.hpp"
#include "Graphics/GlobalDrawer/Support/Support.hpp"
#include "Graphics/GlobalDrawer/Text/FontAtlasBuilder.hpp"
#include "Graphics/TextureManager/TextureStore.hpp"

#include <cstdio>
#include <utility>

namespace Aporia::graphics_internal {

FontHandle FontStore::createFromTtf(
    const std::string& ttfPath,
    float pixelSize,
    TextureStore& textures,
    SceneBindings& scene,
    std::string& error) {
    
    const std::vector<std::uint8_t> bytes = readFileBytes(ttfPath);
    
    if (bytes.empty()) {
        error = "TTF file could not be read";
        return invalidFontHandle;
    }
    BakedFontAtlas baked = bakeTtfAtlas(bytes, pixelSize);
    
    if (baked.pixels.empty() || baked.atlas.glyphs.empty()) {
        error = "TTF atlas baking failed";
        return invalidFontHandle;
    }

    TextureDesc textureDescription;
    textureDescription.width = baked.width;
    textureDescription.height = baked.height;
    textureDescription.format = TextureFormat::Rgba8Unorm;
    textureDescription.pixels = baked.pixels.data();
    textureDescription.dataSize = baked.pixels.size();
    
    const TextureHandle texture = textures.create(textureDescription, scene, error);
    
    if (!texture.valid()) {
        return invalidFontHandle;
    }

    Resource resource;
    resource.texture = texture;
    resource.atlas = std::move(baked.atlas);
    resource.atlas.texture = texture;
    const std::uint64_t id = nextFontId_++;
    fonts_[id] = std::move(resource);
    
    return FontHandle{id};
}

FontHandle FontStore::createFromMsdf(
    const std::string& jsonPath,
    const std::string& pngPath,
    TextureStore& textures,
    SceneBindings& scene,
    std::string& error) {
    const std::string json = readFileText(jsonPath);
    const std::vector<std::uint8_t> png = readFileBytes(pngPath);
    if (json.empty() || png.empty()) {
        error = "MSDF atlas files could not be read";
        return invalidFontHandle;
    }
    BakedFontAtlas baked = loadMsdfAtlas(json, png);
    if (baked.pixels.empty() || baked.atlas.glyphs.empty()) {
        error = "MSDF atlas loading failed";
        return invalidFontHandle;
    }

    TextureDesc textureDescription;
    textureDescription.width = baked.width;
    textureDescription.height = baked.height;
    textureDescription.format = TextureFormat::Rgba8Unorm;
    textureDescription.pixels = baked.pixels.data();
    textureDescription.dataSize = baked.pixels.size();
    const TextureHandle texture = textures.create(textureDescription, scene, error);
    if (!texture.valid()) {
        return invalidFontHandle;
    }

    Resource resource;
    resource.texture = texture;
    resource.atlas = std::move(baked.atlas);
    resource.atlas.texture = texture;
    const std::uint64_t id = nextFontId_++;
    fonts_[id] = std::move(resource);
    return FontHandle{id};
}

void FontStore::destroy(FontHandle handle, TextureStore& textures, SceneBindings& scene) noexcept {
    if (!handle.valid()) {
        return;
    }
    const auto fontIt = fonts_.find(handle.value());
    if (fontIt == fonts_.end()) {
        return;
    }
    std::string error;
    textures.destroy(fontIt->second.texture, scene, error);
    fonts_.erase(fontIt);
}

bool FontStore::valid(FontHandle handle) const noexcept {
    return handle.valid() && fonts_.count(handle.value()) != 0;
}

const FontStore::Resource* FontStore::find(FontHandle handle) const noexcept {
    const auto it = fonts_.find(handle.value());
    return it == fonts_.end() ? nullptr : &it->second;
}

void FontStore::release() noexcept {
    fonts_.clear();
    nextFontId_ = 1;
}

} // namespace Aporia::graphics_internal
