#include "Readback.hpp"

#include "Device/MemoryManager/MemoryManager.hpp"
#include "Graphics/GlobalDrawer/GraphicsImpl.hpp"

#include <cstring>

namespace Aporia::graphics_internal {

// Converts R16G16B16A16_SFLOAT (HDR) to RGBA8 (LDR) with ACES tonemap + sRGB gamma.
static std::uint8_t toSrgb(float v) noexcept {
    v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    return static_cast<std::uint8_t>(std::round(v * 255.0f));
}

static bool convertHdrToLdr(const std::uint8_t* src, std::uint8_t* dst, std::size_t width, std::size_t height) {
    const float* pixels = reinterpret_cast<const float*>(src);
    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            const std::size_t idx = y * width + x;
            const float r = pixels[idx * 4 + 0];
            const float g = pixels[idx * 4 + 1];
            const float b = pixels[idx * 4 + 2];
            // ACES filmic tone mapping
            const float a = 2.51f, b_val = 0.03f, c = 2.43f, d = 0.59f, e = 0.14f;
            const float rt = (r * (a * r + b_val)) / (r * (c * r + d) + e);
            const float gt = (g * (a * g + b_val)) / (g * (c * g + d) + e);
            const float bt = (b * (a * b + b_val)) / (b * (c * b + d) + e);
            // sRGB gamma
            dst[idx * 4 + 0] = toSrgb(static_cast<float>(std::pow(rt, 1.0 / 2.2)));
            dst[idx * 4 + 1] = toSrgb(static_cast<float>(std::pow(gt, 1.0 / 2.2)));
            dst[idx * 4 + 2] = toSrgb(static_cast<float>(std::pow(bt, 1.0 / 2.2)));
            dst[idx * 4 + 3] = 255;
        }
    }
    return true;
}

bool readbackColorImage(const ReadbackContext& context, std::vector<std::uint8_t>& pixels) {
    if (context.device == VK_NULL_HANDLE || context.commandBuffer == VK_NULL_HANDLE ||
        context.queue == VK_NULL_HANDLE || context.colorImage == VK_NULL_HANDLE ||
        context.width == 0 || context.height == 0) {
        return false;
    }

    // Frames may still be in flight writing the color image; drain first.
    if (vkQueueWaitIdle(context.queue) != VK_SUCCESS) {
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
    // If the source is HDR (R16G16B16A16_SFLOAT) convert to LDR (RGBA8) so
    // the caller gets display-space pixels that can be compared.
    if (context.isHdrFormat) {
        std::vector<std::uint8_t> hdrPixels(static_cast<std::size_t>(byteCount));
        std::memcpy(hdrPixels.data(), mapped, static_cast<std::size_t>(byteCount));
        const VkDeviceSize dstByteCount = static_cast<VkDeviceSize>(context.width) *
            static_cast<VkDeviceSize>(context.height) * 4;
        pixels.assign(static_cast<std::size_t>(dstByteCount), 0);
        convertHdrToLdr(hdrPixels.data(), pixels.data(), context.width, context.height);
    } else {
        std::memcpy(pixels.data(), mapped, static_cast<std::size_t>(byteCount));
    }
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
    // In HDR mode the tonemap pass (which includes SMAA/TAA) writes into
    // hdrImage (R16G16B16A16_SFLOAT). Only fall back to colorImage when HDR is
    // disabled. Mark HDR format so readback can convert to LDR.
    context.colorImage = impl_->hdrImage != VK_NULL_HANDLE && impl_->hdrEnabled ?
                         impl_->hdrImage : impl_->colorImage;
    context.width = impl_->hdrEnabled && impl_->hdrWidth > 0 ?
                    impl_->hdrWidth : impl_->renderWidth;
    context.height = impl_->hdrEnabled && impl_->hdrHeight > 0 ?
                     impl_->hdrHeight : impl_->renderHeight;
    context.isHdrFormat = impl_->hdrEnabled && impl_->hdrImage != VK_NULL_HANDLE;
    return graphics_internal::readbackColorImage(context, pixels);
}

} // namespace Aporia
