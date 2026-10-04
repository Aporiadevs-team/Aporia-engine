#include "MeshRenderer.hpp"

#include <cstring>

namespace Aporia::graphics_internal {

void MeshRenderer::destroyResource(VkDevice device, Resource& resource) noexcept {
    if (device != VK_NULL_HANDLE) {
        if (resource.indexBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, resource.indexBuffer, nullptr);
        }
        if (resource.indexMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, resource.indexMemory, nullptr);
        }
        if (resource.vertexBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, resource.vertexBuffer, nullptr);
        }
        if (resource.vertexMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, resource.vertexMemory, nullptr);
        }
    }
    resource = {};
}

void MeshRenderer::renderMeshes(
        VkCommandBuffer commandBuffer,
        VkPipeline pipeline,
        VkPipelineLayout pipelineLayout,
        VkDescriptorSet descriptorSet,
        const float viewProj[16],
        const std::vector<RenderItem>& items,
        const std::unordered_map<std::uint64_t, Resource>& resources,
        VkPipeline pipelineOverride) noexcept {
    PushConstants push{};
    std::memcpy(push.viewProj, viewProj, sizeof(push.viewProj));
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      pipelineOverride != VK_NULL_HANDLE ? pipelineOverride : pipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
    for (const RenderItem& item : items) {
        const auto it = resources.find(item.meshId);
        if (it == resources.end() || it->second.vertexBuffer == VK_NULL_HANDLE) continue;
        std::memcpy(push.model, item.modelMatrix, sizeof(push.model));
        std::memcpy(push.color, item.color, sizeof(push.color));
        vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                           0, sizeof(push), &push);
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &it->second.vertexBuffer, &offset);
        if (it->second.indexBuffer != VK_NULL_HANDLE) {
            vkCmdBindIndexBuffer(commandBuffer, it->second.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(commandBuffer, it->second.indexCount, 1, 0, 0, 0);
        } else {
            vkCmdDraw(commandBuffer, it->second.vertexCount, 1, 0, 0);
        }
    }
}

} // namespace Aporia::graphics_internal
