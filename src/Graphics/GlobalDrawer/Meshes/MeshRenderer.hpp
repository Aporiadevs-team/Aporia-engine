#pragma once

#include "Aporia/Graphics/Graphics.hpp"

#include <cstdint>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.h>

namespace Aporia::graphics_internal {

class MeshRenderer {
public:
    struct Resource {
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
        VkBuffer indexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory indexMemory = VK_NULL_HANDLE;
        std::uint32_t vertexCount = 0;
        std::uint32_t indexCount = 0;
    };
    struct PushConstants {
        float viewProj[16];
        float model[16];
        float color[4];
        float emissive;
    };

    static void renderMeshes(
        VkCommandBuffer commandBuffer,
        VkPipeline pipeline,
        VkPipelineLayout pipelineLayout,
        VkDescriptorSet descriptorSet,
        const float viewProj[16],
        const std::vector<RenderItem>& items,
        const std::unordered_map<std::uint64_t, Resource>& resources,
        VkPipeline pipelineOverride = VK_NULL_HANDLE,
        const std::unordered_map<std::uint64_t, VkDescriptorSet>* materialSets = nullptr,
        VkDescriptorSet defaultMaterialSet = VK_NULL_HANDLE) noexcept;

    static void destroyResource(VkDevice device, Resource& resource) noexcept;

    struct Stats {
        std::uint64_t meshCount = 0;
        std::uint64_t vertexCount = 0;
    };

    void clear() noexcept { meshes_.clear(); }
    void registerMesh(MeshHandle handle, std::uint64_t vertexCount) {
        meshes_[handle.value()] = vertexCount;
    }
    void unregisterMesh(MeshHandle handle) noexcept { meshes_.erase(handle.value()); }
    [[nodiscard]] Stats stats() const noexcept {
        Stats result;
        result.meshCount = meshes_.size();
        for (const auto& [id, count] : meshes_) {
            (void)id;
            result.vertexCount += count;
        }
        return result;
    }

private:
    std::unordered_map<std::uint64_t, std::uint64_t> meshes_;
};

}
