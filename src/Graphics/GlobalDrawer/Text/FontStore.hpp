#pragma once

#include "Aporia/Graphics/Graphics.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.h>

namespace Aporia::graphics_internal {

class TextureStore;
class SceneBindings;

// Owns font handles: the baked/loaded atlas and the texture it was uploaded to.
// Layout and rendering stay in TextRenderer; this store only manages lifetime.
class FontStore {
public:
    struct Resource {
        TextureHandle texture;
        Aporia::UiTextAtlasDesc atlas;
    };

    [[nodiscard]] FontHandle createFromTtf(
        const std::string& ttfPath,
        float pixelSize,
        TextureStore& textures,
        SceneBindings& scene,
        std::string& error);
    [[nodiscard]] FontHandle createFromMsdf(
        const std::string& jsonPath,
        const std::string& pngPath,
        TextureStore& textures,
        SceneBindings& scene,
        std::string& error);
    void destroy(FontHandle handle, TextureStore& textures, SceneBindings& scene) noexcept;
    [[nodiscard]] bool valid(FontHandle handle) const noexcept;
    [[nodiscard]] const Resource* find(FontHandle handle) const noexcept;
    void release() noexcept;

private:
    std::unordered_map<std::uint64_t, Resource> fonts_;
    std::uint64_t nextFontId_ = 1;
};

} // namespace Aporia::graphics_internal
