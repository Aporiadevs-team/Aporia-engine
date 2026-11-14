#pragma once

#include "Aporia/Graphics/Graphics.hpp"

#include <unordered_map>
#include <vulkan/vulkan.h>

namespace Aporia::graphics_internal {

// Ownership boundary for GPU texture resources. Graphics remains the public
// facade; this manager owns lookup/bind/update policy for texture handles.
class TextureManager {
public:
    struct Resource {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
        VkFormat format = VK_FORMAT_UNDEFINED;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t mipLevels = 1;
        std::size_t dataSize = 0;
    };

    static void destroyResource(VkDevice device, Resource& texture) noexcept;

    // Explicit Vulkan context so the manager does not depend on Graphics::Impl.
    struct UploadContext {
        VkDevice device = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkPhysicalDeviceMemoryProperties memoryProperties{};
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkQueue queue = VK_NULL_HANDLE;
    };

    // Uploads pixel data into an existing texture image and regenerates its mip
    // chain (blit downsampling). Transitions through TRANSFER_DST and back to
    // SHADER_READ_ONLY, so callers pass the current layout as oldLayout.
    static bool uploadTexture(
        const UploadContext& context,
        Resource& texture,
        const void* pixels,
        std::size_t dataSize,
        VkImageLayout oldLayout) noexcept;

    // Creates the device image/view/sampler and uploads the pixels in one call.
    // scalePercent drives the quality auto-compression (100 = native).
    struct CreateContext {
        VkDevice device = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkPhysicalDeviceMemoryProperties memoryProperties{};
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkQueue queue = VK_NULL_HANDLE;
    };
    static bool createResource(
        const CreateContext& context,
        const TextureDesc& description,
        VkFormat vulkanFormat,
        std::uint32_t bytesPerPixel,
        std::uint32_t scalePercent,
        Resource& texture) noexcept;

    // Convenience factory for a 1x1 solid-color texture (defaults, fallbacks).
    static bool createSolid(
        const CreateContext& context,
        std::uint8_t r,
        std::uint8_t g,
        std::uint8_t b,
        std::uint8_t a,
        Resource& texture) noexcept;
    struct Entry {
        TextureHandle handle{};
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        TextureFormat format = TextureFormat::Rgba8Unorm;
    };

    void clear() noexcept { entries_.clear(); }
    void track(const Entry& entry) { entries_[entry.handle.value()] = entry; }
    void untrack(TextureHandle handle) noexcept { entries_.erase(handle.value()); }
    [[nodiscard]] const Entry* find(TextureHandle handle) const noexcept {
        const auto it = entries_.find(handle.value());
        return it == entries_.end() ? nullptr : &it->second;
    }
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

private:
    std::unordered_map<std::uint64_t, Entry> entries_;
};

}
