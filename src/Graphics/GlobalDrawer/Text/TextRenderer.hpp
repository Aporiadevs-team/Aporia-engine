#pragma once

#include "Aporia/Graphics/Graphics.hpp"

#include <cstdint>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

namespace Aporia::graphics_internal {

// std140 block matching shaders/text.vert. Glyph instances follow the header.
struct FontUniforms {
    float screenData[4]{};    // xy = screen size, z = guiScale, w = outlineWidth
    float outlineColor[4]{};  // rgba
    float atlasData[4]{};     // xy = atlas size, z = distanceRange, w = fontSize
    std::int32_t charCount[4]{}; // x = glyph count
};

// One glyph instance: 4 x vec4 appended after FontUniforms.
struct TextGlyph {
    float position[2]{};
    float size[2]{};
    float uv[4]{};      // u0, v0, u1, v1
    float color[4]{};
    float rotation = 0.0f;
    float pivot[2]{};
    float glyphScale = 1.0f;
};

struct TextColor {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};

// Draws TTF (coverage atlas) and MSDF text with the same uniform-driven quad
// generation used by the reference renderer: no vertex buffer, each glyph is
// six vertices synthesized from gl_VertexIndex.
class TextRenderer {
public:
    enum class Mode : std::uint8_t {
        Msdf = 0,
        Ttf = 1,
    };

    static constexpr std::uint32_t MaxGlyphs = 256;
    static constexpr std::uint32_t SlotCount = 2;

    struct Context {
        VkDevice device = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkPhysicalDeviceMemoryProperties memoryProperties{};
        VkRenderPass renderPass = VK_NULL_HANDLE;
        std::string shaderDirectory;
    };

    struct AtlasTexture {
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
        float width = 0.0f;
        float height = 0.0f;
        float distanceRange = 4.0f;
        float fontSize = 32.0f;
    };

    bool initialize(const Context& context) noexcept;
    void destroy() noexcept;

    void setMode(Mode mode) noexcept { mode_ = mode; }
    [[nodiscard]] Mode mode() const noexcept { return mode_; }
    void setAtlasTexture(const AtlasTexture& texture) noexcept;

    // Lays out text into glyph instances. Handles '\n'; size is in pixels.
    [[nodiscard]] std::vector<TextGlyph> layoutText(
        const UiTextAtlasDesc& atlas,
        const std::string& text,
        float x,
        float y,
        float size,
        const TextColor& color,
        float outlineWidth = 0.0f,
        const TextColor& outlineColor = {}) const;

    [[nodiscard]] float measureText(
        const UiTextAtlasDesc& atlas,
        const std::string& text,
        float size) const;

    [[nodiscard]] float lineHeight(const UiTextAtlasDesc& atlas, float size) const noexcept;

    // Uploads glyphs into the frame slot and issues the draw (needs a bound
    // pipeline-ready render pass; viewport/scissor are set by the caller).
    void renderText(
        VkCommandBuffer commandBuffer,
        std::uint32_t frameSlot,
        float screenWidth,
        float screenHeight,
        float guiScale,
        float outlineWidth,
        const TextColor& outlineColor,
        const std::vector<TextGlyph>& glyphs) const noexcept;

    [[nodiscard]] bool initialized() const noexcept { return device_ != VK_NULL_HANDLE; }

private:
    struct Slot {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        void* mapped = nullptr;
    };

    void updateDescriptors() noexcept;

    VkDevice device_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline msdfPipeline_ = VK_NULL_HANDLE;
    VkPipeline ttfPipeline_ = VK_NULL_HANDLE;
    Slot slots_[SlotCount]{};
    Mode mode_ = Mode::Msdf;
    AtlasTexture atlas_{};
};

} // namespace Aporia::graphics_internal
