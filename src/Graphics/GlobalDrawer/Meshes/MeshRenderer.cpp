#include "MeshRenderer.hpp"

#include <cstring>

namespace Aporia::graphics_internal {

namespace {

bool findMemoryType(
        const VkPhysicalDeviceMemoryProperties& properties,
        std::uint32_t typeFilter,
        VkMemoryPropertyFlags required,
        std::uint32_t& outType) noexcept {
    for (std::uint32_t index = 0; index < properties.memoryTypeCount; ++index) {
        if ((typeFilter & (1u << index)) == 0) {
            continue;
        }
        if ((properties.memoryTypes[index].propertyFlags & required) == required) {
            outType = index;
            return true;
        }
    }
    return false;
}

} // namespace

bool MeshRenderer::uploadBuffer(
        const UploadContext& context,
        VkDeviceSize size,
        const void* data,
        VkBufferUsageFlags usage,
        VkBuffer& buffer,
        VkDeviceMemory& memory) noexcept {
    buffer = VK_NULL_HANDLE;
    memory = VK_NULL_HANDLE;
    if (size == 0 || data == nullptr || context.device == VK_NULL_HANDLE ||
        context.commandBuffer == VK_NULL_HANDLE) {
        return false;
    }

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    const auto destroyStaging = [&]() noexcept {
        if (stagingBuffer != VK_NULL_HANDLE) vkDestroyBuffer(context.device, stagingBuffer, nullptr);
        if (stagingMemory != VK_NULL_HANDLE) vkFreeMemory(context.device, stagingMemory, nullptr);
    };

    VkBufferCreateInfo stagingInfo{};
    stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingInfo.size = size;
    stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(context.device, &stagingInfo, nullptr, &stagingBuffer) != VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements stagingRequirements{};
    vkGetBufferMemoryRequirements(context.device, stagingBuffer, &stagingRequirements);
    std::uint32_t stagingType = 0;
    if (!findMemoryType(context.memoryProperties, stagingRequirements.memoryTypeBits,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                        stagingType)) {
        destroyStaging();
        return false;
    }
    VkMemoryAllocateInfo stagingAllocate{};
    stagingAllocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    stagingAllocate.allocationSize = stagingRequirements.size;
    stagingAllocate.memoryTypeIndex = stagingType;
    if (vkAllocateMemory(context.device, &stagingAllocate, nullptr, &stagingMemory) != VK_SUCCESS ||
        vkBindBufferMemory(context.device, stagingBuffer, stagingMemory, 0) != VK_SUCCESS) {
        destroyStaging();
        return false;
    }

    void* mapped = nullptr;
    if (vkMapMemory(context.device, stagingMemory, 0, size, 0, &mapped) != VK_SUCCESS || mapped == nullptr) {
        destroyStaging();
        return false;
    }
    std::memcpy(mapped, data, static_cast<std::size_t>(size));
    vkUnmapMemory(context.device, stagingMemory);

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(context.device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        destroyStaging();
        return false;
    }

    VkMemoryRequirements bufferRequirements{};
    vkGetBufferMemoryRequirements(context.device, buffer, &bufferRequirements);
    std::uint32_t bufferType = 0;
    if (!findMemoryType(context.memoryProperties, bufferRequirements.memoryTypeBits,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, bufferType)) {
        destroyStaging();
        return false;
    }
    VkMemoryAllocateInfo bufferAllocate{};
    bufferAllocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    bufferAllocate.allocationSize = bufferRequirements.size;
    bufferAllocate.memoryTypeIndex = bufferType;
    if (vkAllocateMemory(context.device, &bufferAllocate, nullptr, &memory) != VK_SUCCESS ||
        vkBindBufferMemory(context.device, buffer, memory, 0) != VK_SUCCESS) {
        destroyStaging();
        return false;
    }

    if (vkQueueWaitIdle(context.queue) != VK_SUCCESS ||
        vkResetCommandBuffer(context.commandBuffer, 0) != VK_SUCCESS) {
        destroyStaging();
        return false;
    }
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(context.commandBuffer, &beginInfo) != VK_SUCCESS) {
        destroyStaging();
        return false;
    }

    VkBufferCopy copyRegion{};
    copyRegion.size = size;
    vkCmdCopyBuffer(context.commandBuffer, stagingBuffer, buffer, 1, &copyRegion);

    VkBufferMemoryBarrier uploadBarrier{};
    uploadBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    uploadBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    uploadBarrier.dstAccessMask =
        ((usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) != 0 ? VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT : 0) |
        ((usage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT) != 0 ? VK_ACCESS_INDEX_READ_BIT : 0);
    uploadBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    uploadBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    uploadBarrier.buffer = buffer;
    uploadBarrier.offset = 0;
    uploadBarrier.size = size;
    vkCmdPipelineBarrier(context.commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 0, nullptr, 1, &uploadBarrier, 0, nullptr);

    if (vkEndCommandBuffer(context.commandBuffer) != VK_SUCCESS) {
        destroyStaging();
        return false;
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &context.commandBuffer;
    const VkResult submitResult = vkQueueSubmit(context.queue, 1, &submitInfo, VK_NULL_HANDLE);
    const VkResult waitResult = submitResult == VK_SUCCESS ? vkQueueWaitIdle(context.queue) : submitResult;
    destroyStaging();
    return submitResult == VK_SUCCESS && waitResult == VK_SUCCESS;
}

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

}
