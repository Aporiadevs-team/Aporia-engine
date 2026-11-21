#pragma once

#if defined(APORIA_EXPERIMENTAL)

#include "Aporia/Graphics/Graphics.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vulkan/vulkan.h>

namespace Aporia::graphics_internal {

class TextureStore;

// EXPERIMENTAL per-item multi-layer materials (descriptor set 1): layout, pool,
// per-material UV transform buffers and the descriptor sets the mesh pass binds.
class MaterialStore {
public:
    struct Device {
        VkDevice device = VK_NULL_HANDLE;
        VkPhysicalDeviceMemoryProperties memoryProperties{};
    };

    void setDevice(const Device& device) noexcept { device_ = device; }

    bool createDefault(const TextureStore& textures, std::string& error);
    [[nodiscard]] MaterialHandle create(const MaterialDesc& description, const TextureStore& textures, std::string& error);
    bool update(MaterialHandle handle, const MaterialDesc& description, const TextureStore& textures);
    void destroy(MaterialHandle handle) noexcept;
    [[nodiscard]] bool valid(MaterialHandle handle) const noexcept;
    void release(VkDevice device) noexcept;

    [[nodiscard]] const std::unordered_map<std::uint64_t, VkDescriptorSet>* sets() const noexcept { return &materialSets_; }
    [[nodiscard]] VkDescriptorSet defaultSet() const noexcept { return defaultMaterialSet_; }
    [[nodiscard]] VkDescriptorSetLayout descriptorSetLayout() const noexcept { return materialDescriptorLayout_; }

private:
    bool ensureLayoutAndPool(std::string& error);
    VkDescriptorSet buildSet(
        const MaterialDesc& description,
        const TextureStore& textures,
        VkBuffer& outBuffer,
        VkDeviceMemory& outMemory);

    Device device_{};
    VkDescriptorSetLayout materialDescriptorLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool materialDescriptorPool_ = VK_NULL_HANDLE;
    std::unordered_map<std::uint64_t, VkDescriptorSet> materialSets_;
    std::unordered_map<std::uint64_t, VkBuffer> materialBuffers_;
    std::unordered_map<std::uint64_t, VkDeviceMemory> materialMemory_;
    VkDescriptorSet defaultMaterialSet_ = VK_NULL_HANDLE;
    VkBuffer defaultMaterialBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory defaultMaterialMemory_ = VK_NULL_HANDLE;
    std::uint64_t nextMaterialId_ = 1;
};

} // namespace Aporia::graphics_internal

#endif // APORIA_EXPERIMENTAL
