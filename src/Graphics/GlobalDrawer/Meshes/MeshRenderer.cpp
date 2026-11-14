#include "MeshRenderer.hpp"

#include <algorithm>
#include <cstring>

namespace Aporia::graphics_internal {

namespace {

// Conservative frustum test in clip space: the box is culled only when all 8
// corners fall outside one plane (Vulkan NDC: -w<=x<=w, -w<=y<=w, 0<=z<=w,
// w>0). Boxes straddling the eye plane (mixed w) are always kept, so a cull
// never drops visible geometry.
bool aabbOutsideViewFrustum(const float* viewProj, const float* model,
                            const float mn[3], const float mx[3]) noexcept {
    float vpm[16];
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int index = 0; index < 4; ++index) {
                sum += viewProj[index * 4 + row] * model[column * 4 + index];
            }
            vpm[column * 4 + row] = sum;
        }
    }
    float xpwMax = -1e30f, xmwMin = 1e30f;
    float ypwMax = -1e30f, ymwMin = 1e30f;
    float zMax = -1e30f, zmwMin = 1e30f, wMax = -1e30f;
    for (int corner = 0; corner < 8; ++corner) {
        const float p[3] = {
            (corner & 1) != 0 ? mx[0] : mn[0],
            (corner & 2) != 0 ? mx[1] : mn[1],
            (corner & 4) != 0 ? mx[2] : mn[2],
        };
        const float x = vpm[0] * p[0] + vpm[4] * p[1] + vpm[8] * p[2] + vpm[12];
        const float y = vpm[1] * p[0] + vpm[5] * p[1] + vpm[9] * p[2] + vpm[13];
        const float z = vpm[2] * p[0] + vpm[6] * p[1] + vpm[10] * p[2] + vpm[14];
        const float w = vpm[3] * p[0] + vpm[7] * p[1] + vpm[11] * p[2] + vpm[15];
        xpwMax = std::max(xpwMax, x + w);
        xmwMin = std::min(xmwMin, x - w);
        ypwMax = std::max(ypwMax, y + w);
        ymwMin = std::min(ymwMin, y - w);
        zMax = std::max(zMax, z);
        zmwMin = std::min(zmwMin, z - w);
        wMax = std::max(wMax, w);
    }
    return xpwMax < 0.0f || xmwMin > 0.0f ||
           ypwMax < 0.0f || ymwMin > 0.0f ||
           zMax < 0.0f || zmwMin > 0.0f || wMax <= 0.0f;
}

} // namespace

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
        VkPipeline pipelineOverride,
        const std::unordered_map<std::uint64_t, VkDescriptorSet>* materialSets,
        VkDescriptorSet defaultMaterialSet,
        const std::unordered_map<std::uint64_t, VkDescriptorSet>* textureSets,
        VkDescriptorSet defaultTextureSet) noexcept {
    PushConstants push{};
#if defined(APORIA_EXPERIMENTAL)
    constexpr std::uint32_t kItemTextureSet = 2;
#else
    constexpr std::uint32_t kItemTextureSet = 1;
#endif
    // viewProj drives the per-item frustum cull below; it matches the
    // SceneUniforms binding 3 (main pass) or the sun matrix (shadow pass).
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      pipelineOverride != VK_NULL_HANDLE ? pipelineOverride : pipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
    VkDescriptorSet boundTextureSet = VK_NULL_HANDLE;
    for (const RenderItem& item : items) {
        const auto it = resources.find(item.meshId);
        if (it == resources.end() || it->second.vertexBuffer == VK_NULL_HANDLE) continue;
        if (item.aabbMin[0] <= item.aabbMax[0] &&
            aabbOutsideViewFrustum(viewProj, item.modelMatrix, item.aabbMin, item.aabbMax)) {
            continue;
        }
#if defined(APORIA_EXPERIMENTAL)
        if (materialSets != nullptr) {
            VkDescriptorSet materialSet = defaultMaterialSet;
            if (item.material.valid()) {
                const auto materialIt = materialSets->find(item.material.value());
                if (materialIt != materialSets->end()) {
                    materialSet = materialIt->second;
                }
            }
            if (materialSet != VK_NULL_HANDLE) {
                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        pipelineLayout, 1, 1, &materialSet, 0, nullptr);
            }
        }
#endif
        if (textureSets != nullptr) {
            VkDescriptorSet textureSet = defaultTextureSet;
            if (item.texture.valid()) {
                const auto textureIt = textureSets->find(item.texture.value());
                if (textureIt != textureSets->end()) {
                    textureSet = textureIt->second;
                }
            }
            if (textureSet != VK_NULL_HANDLE && textureSet != boundTextureSet) {
                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        pipelineLayout, kItemTextureSet, 1, &textureSet, 0, nullptr);
                boundTextureSet = textureSet;
            }
        }
        std::memcpy(push.model, item.modelMatrix, sizeof(push.model));
        std::memcpy(push.color, item.color, sizeof(push.color));
        push.emissive = item.emissive;
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
