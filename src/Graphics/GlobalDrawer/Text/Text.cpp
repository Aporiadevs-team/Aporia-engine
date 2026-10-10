#include "Graphics/GlobalDrawer/GraphicsImpl.hpp"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace Aporia {

void Graphics::Impl::renderUiText(VkCommandBuffer buffer, std::uint32_t width, std::uint32_t height,
                                  const Aporia::UiDrawList& ui) {
    
    if (!textRenderer.initialized() || ui.textRuns.empty()) {
        
        return;
    }

    const UiTextAtlasDesc* atlasDesc = &ui.textAtlas;
    TextureHandle atlasTextureHandle = ui.textAtlas.texture;
    if (ui.font.valid()) {
        const auto* fontIt = fontStore.find(ui.font);
        if (fontIt == nullptr) {
            return;
        }
        atlasDesc = &fontIt->atlas;
        atlasTextureHandle = fontIt->texture;
    }

    const auto textureIt = textureStore.resources().find(atlasTextureHandle.value());
    if (textureIt == textureStore.resources().end() || textureIt->second.view == VK_NULL_HANDLE) {
        
        return;
    }
    
    graphics_internal::TextRenderer::AtlasTexture atlasTexture;
    atlasTexture.view = textureIt->second.view;
    atlasTexture.sampler = textureIt->second.sampler;
    atlasTexture.width = static_cast<float>(textureIt->second.width);
    atlasTexture.height = static_cast<float>(textureIt->second.height);
    atlasTexture.distanceRange = atlasDesc->distanceRange;
    atlasTexture.fontSize = atlasDesc->emSize;
    textRenderer.setMode(atlasDesc->mode == UiTextAtlasMode::TtfBitmap
        ? graphics_internal::TextRenderer::Mode::Ttf
        : graphics_internal::TextRenderer::Mode::Msdf);
    textRenderer.setAtlasTexture(atlasTexture, currentFrameSlot);
    

    // Signature over everything that affects the glyph batch: the atlas and
    // each run's text/position/size/color. Unchanged content reuses the
    // cached layout and the TextRenderer skips the UBO upload.
    const auto hashBytes = [](std::uint64_t hash, const void* data, std::size_t size) noexcept {
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        for (std::size_t index = 0; index < size; ++index) {
            hash ^= bytes[index];
            hash *= 1099511628211ull;
        }
        return hash;
    };
    std::uint64_t signature = 1469598103934665603ull;
    const std::uint64_t fontValue = ui.font.value();
    signature = hashBytes(signature, &fontValue, sizeof(fontValue));
    const auto hashFloat = [&](float value) { return hashBytes(signature, &value, sizeof(value)); };
    signature = hashFloat(static_cast<float>(atlasDesc->mode));
    signature = hashFloat(atlasDesc->emSize);
    signature = hashFloat(atlasDesc->distanceRange);
    signature = hashFloat(atlasDesc->lineHeight);
    signature = hashFloat(atlasDesc->ascender);
    for (const UiTextRun& run : ui.textRuns) {
        signature = hashBytes(signature, run.text.data(), run.text.size());
        signature = hashFloat(run.x);
        signature = hashFloat(run.y);
        signature = hashFloat(run.size);
        signature = hashFloat(run.outlineWidth);
        signature = hashFloat(run.color.r);
        signature = hashFloat(run.color.g);
        signature = hashFloat(run.color.b);
        signature = hashFloat(run.color.a);
        signature = hashFloat(run.outlineColor.r);
        signature = hashFloat(run.outlineColor.g);
        signature = hashFloat(run.outlineColor.b);
        signature = hashFloat(run.outlineColor.a);
    }

    float outlineWidth = 0.0f;
    graphics_internal::TextColor outlineColor{};
    if (!cachedUiTextValid || signature != cachedUiTextSignature) {
        cachedUiGlyphs.clear();
        for (const UiTextRun& run : ui.textRuns) {
            const graphics_internal::TextColor color{run.color.r, run.color.g, run.color.b, run.color.a};
            const graphics_internal::TextColor outline{
                run.outlineColor.r, run.outlineColor.g, run.outlineColor.b, run.outlineColor.a};
            const std::vector<graphics_internal::TextGlyph> runGlyphs = textRenderer.layoutText(
                *atlasDesc, run.text, run.x, run.y, run.size, color, run.outlineWidth, outline);
            cachedUiGlyphs.insert(cachedUiGlyphs.end(), runGlyphs.begin(), runGlyphs.end());
            if (run.outlineWidth > 0.0f) {
                outlineWidth = run.outlineWidth;
                outlineColor = outline;
            }
        }
        cachedUiTextSignature = signature;
        cachedUiTextValid = true;
        
    } else {
        for (const UiTextRun& run : ui.textRuns) {
            if (run.outlineWidth > 0.0f) {
                outlineWidth = run.outlineWidth;
                outlineColor = {run.outlineColor.r, run.outlineColor.g,
                                run.outlineColor.b, run.outlineColor.a};
            }
        }
    }

    
    textRenderer.renderText(buffer, currentFrameSlot,
                            static_cast<float>(width), static_cast<float>(height),
                            1.0f, outlineWidth, outlineColor, cachedUiGlyphs, signature);
    
}

#if defined(APORIA_EXPERIMENTAL)
void Graphics::Impl::prepareUiText(std::uint32_t width, std::uint32_t height, const Aporia::UiDrawList& ui) {
    if (!textRenderer.initialized() || ui.textRuns.empty()) {
        return;
    }

    const UiTextAtlasDesc* atlasDesc = &ui.textAtlas;
    TextureHandle atlasTextureHandle = ui.textAtlas.texture;
    if (ui.font.valid()) {
        const auto* fontIt = fontStore.find(ui.font);
        if (fontIt == nullptr) {
            return;
        }
        atlasDesc = &fontIt->atlas;
        atlasTextureHandle = fontIt->texture;
    }
    const auto textureIt = textureStore.resources().find(atlasTextureHandle.value());
    if (textureIt == textureStore.resources().end() || textureIt->second.view == VK_NULL_HANDLE) {
        return;
    }
    graphics_internal::TextRenderer::AtlasTexture atlasTexture;
    atlasTexture.view = textureIt->second.view;
    atlasTexture.sampler = textureIt->second.sampler;
    atlasTexture.width = static_cast<float>(textureIt->second.width);
    atlasTexture.height = static_cast<float>(textureIt->second.height);
    atlasTexture.distanceRange = atlasDesc->distanceRange;
    atlasTexture.fontSize = atlasDesc->emSize;
    textRenderer.setMode(atlasDesc->mode == UiTextAtlasMode::TtfBitmap
        ? graphics_internal::TextRenderer::Mode::Ttf
        : graphics_internal::TextRenderer::Mode::Msdf);
    textRenderer.setAtlasTexture(atlasTexture, currentFrameSlot);

    const auto hashBytes = [](std::uint64_t hash, const void* data, std::size_t size) noexcept {
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        for (std::size_t index = 0; index < size; ++index) {
            hash ^= bytes[index];
            hash *= 1099511628211ull;
        }
        return hash;
    };
    std::uint64_t signature = 1469598103934665603ull;
    const std::uint64_t fontValue = ui.font.value();
    signature = hashBytes(signature, &fontValue, sizeof(fontValue));
    const auto hashFloat = [&](float value) { return hashBytes(signature, &value, sizeof(value)); };
    signature = hashFloat(static_cast<float>(atlasDesc->mode));
    signature = hashFloat(atlasDesc->emSize);
    signature = hashFloat(atlasDesc->distanceRange);
    signature = hashFloat(atlasDesc->lineHeight);
    signature = hashFloat(atlasDesc->ascender);
    for (const UiTextRun& run : ui.textRuns) {
        signature = hashBytes(signature, run.text.data(), run.text.size());
        signature = hashFloat(run.x);
        signature = hashFloat(run.y);
        signature = hashFloat(run.size);
        signature = hashFloat(run.outlineWidth);
        signature = hashFloat(run.color.r);
        signature = hashFloat(run.color.g);
        signature = hashFloat(run.color.b);
        signature = hashFloat(run.color.a);
        signature = hashFloat(run.outlineColor.r);
        signature = hashFloat(run.outlineColor.g);
        signature = hashFloat(run.outlineColor.b);
        signature = hashFloat(run.outlineColor.a);
        signature = hashFloat(static_cast<float>(run.layer));
    }

    // Rebuild the batch only when the content changes, but always emit the layer
    // ranges the draw uses. Ranges are derived from the (stable) full batch.
    if (!cachedUiTextValid || signature != cachedUiTextSignature) {
        cachedUiGlyphs.clear();
        cachedUiOutlineWidth = 0.0f;
        cachedUiOutlineColor = {};
        for (std::uint32_t layerIndex = 0; layerIndex < Aporia::kUiLayerCount; ++layerIndex) {
            UiTextRange& range = uiTextRanges[layerIndex];
            range.first = static_cast<std::uint32_t>(cachedUiGlyphs.size());
            for (const UiTextRun& run : ui.textRuns) {
                if (static_cast<std::uint32_t>(run.layer) != layerIndex) {
                    continue;
                }
                const graphics_internal::TextColor color{run.color.r, run.color.g, run.color.b, run.color.a};
                const graphics_internal::TextColor outline{
                    run.outlineColor.r, run.outlineColor.g, run.outlineColor.b, run.outlineColor.a};
                const std::vector<graphics_internal::TextGlyph> runGlyphs = textRenderer.layoutText(
                    *atlasDesc, run.text, run.x, run.y, run.size, color, run.outlineWidth, outline);
                cachedUiGlyphs.insert(cachedUiGlyphs.end(), runGlyphs.begin(), runGlyphs.end());
                if (run.outlineWidth > 0.0f) {
                    cachedUiOutlineWidth = run.outlineWidth;
                    cachedUiOutlineColor = outline;
                }
            }
            range.count = static_cast<std::uint32_t>(cachedUiGlyphs.size()) - range.first;
        }
        cachedUiTextSignature = signature;
        cachedUiTextValid = true;
    }

    textRenderer.uploadText(currentFrameSlot,
                            static_cast<float>(width), static_cast<float>(height), 1.0f,
                            cachedUiOutlineWidth, cachedUiOutlineColor, cachedUiGlyphs, signature);
}

void Graphics::Impl::drawUiTextLayer(VkCommandBuffer buffer, Aporia::UiLayer layer) {
    const std::uint32_t index = static_cast<std::uint32_t>(layer);
    if (index >= Aporia::kUiLayerCount) {
        return;
    }
    const UiTextRange& range = uiTextRanges[index];
    if (range.count == 0) {
        return;
    }
    textRenderer.drawTextRange(buffer, currentFrameSlot, range.first, range.count);
}
#endif

} // namespace Aporia

