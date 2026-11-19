#pragma once

#if defined(_WIN32) && !defined(VK_USE_PLATFORM_WIN32_KHR)
#define VK_USE_PLATFORM_WIN32_KHR
#endif

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>

namespace Aporia::graphics_internal {

// Owns the scene descriptor set layout, the per-frame uniform buffers and the
// descriptor sets the mesh/shadow/reflection passes bind. Binding layout:
//   0 = albedo sampler, 1 = shadow map, 2 = reflection map, 3 = scene UBO.
class SceneBindings {
public:
    static constexpr std::uint32_t kSlots = 2;

    bool createLayout(VkDevice device);
    // Creates the pool, uniform buffers, descriptor sets and the initial
    // descriptors. `uniformSize` must match the shader's scene UBO block.
    bool allocate(
        VkDevice device,
        const VkPhysicalDeviceMemoryProperties& memoryProperties,
        VkDeviceSize uniformSize,
        VkImageView albedoView,
        VkSampler albedoSampler,
        VkImageView shadowView,
        VkSampler shadowSampler);
    void releaseResources(VkDevice device) noexcept;
    void destroyLayout(VkDevice device) noexcept;

    [[nodiscard]] VkDescriptorSetLayout layout() const noexcept { return layout_; }
    [[nodiscard]] VkDescriptorSet set(std::uint32_t slot) const noexcept {
        return sets_[slot < kSlots ? slot : 0];
    }
    [[nodiscard]] bool resourcesReady() const noexcept { return pool_ != VK_NULL_HANDLE; }

    bool writeUniforms(std::uint32_t slot, const void* data, std::size_t size) noexcept;
    void bindAlbedo(std::uint32_t frameSlot, VkImageView view, VkSampler sampler) noexcept;
    void bindReflection(std::uint32_t frameSlot, VkImageView view, VkSampler sampler) noexcept;

private:
    void writeImage(std::uint32_t frameSlot, std::uint32_t binding, VkImageView view, VkSampler sampler, VkImageLayout layout) noexcept;

    VkDevice device_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout layout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkBuffer buffers_[kSlots] = {};
    VkDeviceMemory memory_[kSlots] = {};
    VkDescriptorSet sets_[kSlots] = {};
    VkDeviceSize uniformSize_ = 0;
};

} // namespace Aporia::graphics_internal
