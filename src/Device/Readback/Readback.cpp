#include "Readback.hpp"

#include "Device/MemoryManager/MemoryManager.hpp"
#include "Graphics/GlobalDrawer/GraphicsImpl.hpp"

#include <cstring>

namespace Aporia::graphics_internal {

bool readbackColorImage(const ReadbackContext& context, std::vector<std::uint8_t>& pixels) {
    if (context.device == VK_NULL_HANDLE || context.commandBuffer == VK_NULL_HANDLE ||
        context.queue == VK_NULL_HANDLE || context.colorImage == VK_NULL_HANDLE ||
        context.width == 0 || context.height == 0) {
        return false;
    }

    const VkDeviceSize byteCount =
        static_cast<VkDeviceSize>(context.width) * static_cast<VkDeviceSize>(context.height) * 4;

    pixels.assign(static_cast<std::size_t>(byteCount), 0);

    VkBuffer dstBuffer = VK_NULL_HANDLE;
    VkDeviceMemory dstMemory = VK_NULL_HANDLE;
    auto destroyReadback = [&]() noexcept {
        if (dstBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(context.device, dstBuffer, nullptr);
        }
        if (dstMemory != VK_NULL_HANDLE) {
            vkFreeMemory(context.device, dstMemory, nullptr);
        }
    };

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = byteCount;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(context.device, &bufferInfo, nullptr, &dstBuffer) != VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(context.device, dstBuffer, &requirements);
    std::uint32_t memoryType = 0;
    if (!MemoryManager::findMemoryType(
            context.memoryProperties,
            requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            memoryType)) {
        destroyReadback();
        return false;
    }

    VkMemoryAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = requirements.size;
    allocateInfo.memoryTypeIndex = memoryType;
    if (vkAllocateMemory(context.device, &allocateInfo, nullptr, &dstMemory) != VK_SUCCESS) {
        destroyReadback();
        return false;
    }
    if (vkBindBufferMemory(context.device, dstBuffer, dstMemory, 0) != VK_SUCCESS) {
        destroyReadback();
        return false;
    }

    if (vkResetCommandBuffer(context.commandBuffer, 0) != VK_SUCCESS) {
        destroyReadback();
        return false;
    }
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(context.commandBuffer, &beginInfo) != VK_SUCCESS) {
        destroyReadback();
        return false;
    }

    VkImageMemoryBarrier toTransfer{};
    toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toTransfer.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.image = context.colorImage;
    toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toTransfer.subresourceRange.levelCount = 1;
    toTransfer.subresourceRange.layerCount = 1;
    toTransfer.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(
        context.commandBuffer,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &toTransfer);

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {context.width, context.height, 1};

    vkCmdCopyImageToBuffer(
        context.commandBuffer, context.colorImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dstBuffer, 1, &region);

    VkImageMemoryBarrier backToAttachment{};
    backToAttachment.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    backToAttachment.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    backToAttachment.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    backToAttachment.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    backToAttachment.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    backToAttachment.image = context.colorImage;
    backToAttachment.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    backToAttachment.subresourceRange.levelCount = 1;
    backToAttachment.subresourceRange.layerCount = 1;
    backToAttachment.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    backToAttachment.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    vkCmdPipelineBarrier(
        context.commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &backToAttachment);

    if (vkEndCommandBuffer(context.commandBuffer) != VK_SUCCESS) {
        destroyReadback();
        return false;
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &context.commandBuffer;
    if (vkQueueSubmit(context.queue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
        destroyReadback();
        return false;
    }
    if (vkQueueWaitIdle(context.queue) != VK_SUCCESS) {
        destroyReadback();
        return false;
    }

    void* mapped = nullptr;
    if (vkMapMemory(context.device, dstMemory, 0, byteCount, 0, &mapped) != VK_SUCCESS || mapped == nullptr) {
        destroyReadback();
        return false;
    }
    std::memcpy(pixels.data(), mapped, static_cast<std::size_t>(byteCount));
    vkUnmapMemory(context.device, dstMemory);
    destroyReadback();
    return true;
}

} // namespace Aporia::graphics_internal

namespace Aporia {

bool Graphics::readbackPixels(std::vector<std::uint8_t>& pixels) const {
    if (!impl_ || !initialized()) {
        return false;
    }
    if (!impl_->headless || impl_->colorImage == VK_NULL_HANDLE) {
        return false;
    }
    graphics_internal::ReadbackContext context;
    context.device = impl_->device;
    context.memoryProperties = impl_->memoryProperties;
    context.commandBuffer = impl_->commandBuffer;
    context.queue = impl_->queue;
    context.colorImage = impl_->colorImage;
    context.width = impl_->renderWidth;
    context.height = impl_->renderHeight;
    return graphics_internal::readbackColorImage(context, pixels);
}

} // namespace Aporia
