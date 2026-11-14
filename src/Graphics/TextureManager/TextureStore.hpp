#pragma once

#include "Aporia/Graphics/Graphics.hpp"
#include "Device/SceneManager/SceneBindings/SceneBindings.hpp"
#include "Graphics/TextureManager/TextureManager.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vulkan/vulkan.h>

namespace Aporia::graphics_internal {

// Owns texture handles, the albedo descriptor set (set 0) and upload policy.
// The facade and the material store look textures up here instead of touching
// the handle map directly.
class TextureStore {
public:
    using Resource = TextureManager::Resource;

    struct Device {
        VkDevice device = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkPhysicalDeviceMemoryProperties memoryProperties{};
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkQueue queue = VK_NULL_HANDLE;
    };

    void setDevice(const Device& device) noexcept { device_ = device; }
    void setTextureScalePercent(std::uint32_t percent) noexcept { textureScalePercent_ = percent; }

    // Creates the white default texture and the descriptor set that samples it.
    bool initialize(SceneBindings& scene, std::string& error);

    // Uploads a new texture and makes it the bound albedo. `error` is set on failure.
    [[nodiscard]] TextureHandle create(const TextureDesc& description, SceneBindings& scene, std::string& error);
    bool update(TextureHandle handle, const void* pixels, std::size_t dataSize, std::string& error);
    bool bind(TextureHandle handle, SceneBindings& scene, std::uint32_t frameSlot, std::string& error);
    void destroy(TextureHandle handle, SceneBindings& scene, std::string& error);

    // Frees the device image/sampler of every texture and the descriptor set.
    void release(VkDevice device) noexcept;

    // Fixed descriptor set that samples `handle`, allocated on first use from
    // the shared pool. Returns VK_NULL_HANDLE when the handle is invalid or the
    // pool is exhausted; callers then fall back to descriptorSet().
    [[nodiscard]] VkDescriptorSet setFor(TextureHandle handle);

    [[nodiscard]] VkDescriptorSetLayout descriptorSetLayout() const noexcept { return descriptorSetLayout_; }
    // Set that currently samples boundHandle(); the frame-wide default.
    [[nodiscard]] VkDescriptorSet descriptorSet() const noexcept { return descriptorSet_; }
    [[nodiscard]] const std::unordered_map<std::uint64_t, VkDescriptorSet>& descriptorSets() const noexcept {
        return descriptorSets_;
    }
    [[nodiscard]] TextureHandle defaultHandle() const noexcept { return defaultTexture_; }
    [[nodiscard]] TextureHandle boundHandle() const noexcept { return boundTexture_; }
    [[nodiscard]] const std::unordered_map<std::uint64_t, Resource>& resources() const noexcept { return textures_; }
    [[nodiscard]] std::size_t count() const noexcept { return textures_.size(); }
    [[nodiscard]] std::uint64_t uploadCount() const noexcept { return textureUploadCount_; }
    [[nodiscard]] std::uint32_t textureScalePercent() const noexcept { return textureScalePercent_; }

private:
    bool uploadData(Resource& texture, const void* pixels, std::size_t dataSize, VkImageLayout oldLayout, std::string& error);
    bool createResource(const TextureDesc& description, Resource& texture, std::string& error);
    bool createDescriptorResources(std::string& error);
    void destroyDescriptorResources(VkDevice device) noexcept;

    // Shared pool budget: one set for the frame-wide default plus one per
    // texture that is referenced as a per-item albedo.
    static constexpr std::uint32_t kMaxDescriptorSets = 512;

    Device device_{};
    std::uint32_t textureScalePercent_ = 100;
    std::unordered_map<std::uint64_t, Resource> textures_;
    std::unordered_map<std::uint64_t, VkDescriptorSet> descriptorSets_;
    TextureHandle defaultTexture_;
    TextureHandle boundTexture_;
    std::uint64_t nextTextureId_ = 1;
    std::uint64_t textureUploadCount_ = 0;
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet_ = VK_NULL_HANDLE;
};

} // namespace Aporia::graphics_internal
