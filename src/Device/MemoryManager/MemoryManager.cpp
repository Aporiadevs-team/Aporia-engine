#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "MemoryManager.hpp"

#include <cstdio>
#include <cstring>
#include <limits>

namespace Aporia::graphics_internal {

bool MemoryManager::findMemoryType(
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

bool MemoryManager::uploadBuffer(
        const Context& context,
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
        stagingBuffer = VK_NULL_HANDLE;
        stagingMemory = VK_NULL_HANDLE;
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

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    const VkResult beginResult = vkBeginCommandBuffer(context.commandBuffer, &beginInfo);
    if (beginResult != VK_SUCCESS) {

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
        ((usage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT) != 0 ? VK_ACCESS_INDEX_READ_BIT : 0) |
        ((usage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT) != 0 ? VK_ACCESS_UNIFORM_READ_BIT : 0);
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

    // Use a fence so the caller can wait asynchronously — no vkQueueWaitIdle
    // needed. This avoids a full GPU pipeline stall on every buffer upload.
    VkFence uploadFence = VK_NULL_HANDLE;
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    const VkResult fenceResult = vkCreateFence(context.device, &fenceInfo, nullptr, &uploadFence);
    const VkResult submitResult = fenceResult == VK_SUCCESS
        ? vkQueueSubmit(context.queue, 1, &submitInfo, uploadFence)
        : vkQueueSubmit(context.queue, 1, &submitInfo, VK_NULL_HANDLE);

    // Wait on the fence if we created one; fall back to queue wait idle as a
    // last resort when fence creation failed.
    if (fenceResult == VK_SUCCESS) {
        vkWaitForFences(context.device, 1, &uploadFence, VK_TRUE,
                        std::numeric_limits<std::uint64_t>::max());
        vkDestroyFence(context.device, uploadFence, nullptr);
    } else if (submitResult == VK_SUCCESS) {
        vkQueueWaitIdle(context.queue);
    }

    destroyStaging();
    return submitResult == VK_SUCCESS;
}

void MemoryManager::destroyBuffer(VkDevice device, VkBuffer& buffer, VkDeviceMemory& memory) noexcept {
    if (device != VK_NULL_HANDLE) {
        if (buffer != VK_NULL_HANDLE) vkDestroyBuffer(device, buffer, nullptr);
        if (memory != VK_NULL_HANDLE) vkFreeMemory(device, memory, nullptr);
    }
    buffer = VK_NULL_HANDLE;
    memory = VK_NULL_HANDLE;
}

} // namespace Aporia::graphics_internal
