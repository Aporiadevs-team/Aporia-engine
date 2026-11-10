#include "TextureManager.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <vector>

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

bool TextureManager::uploadTexture(
        const UploadContext& context,
        Resource& texture,
        const void* pixels,
        std::size_t dataSize,
        VkImageLayout oldLayout) noexcept {
    if (context.device == VK_NULL_HANDLE || context.commandBuffer == VK_NULL_HANDLE ||
        pixels == nullptr || dataSize == 0 || dataSize != texture.dataSize) {
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

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = dataSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(context.device, &bufferInfo, nullptr, &stagingBuffer) != VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements bufferRequirements{};
    vkGetBufferMemoryRequirements(context.device, stagingBuffer, &bufferRequirements);
    std::uint32_t stagingMemoryType = 0;
    if (!findMemoryType(context.memoryProperties, bufferRequirements.memoryTypeBits,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                        stagingMemoryType)) {
        destroyStaging();
        return false;
    }
    VkMemoryAllocateInfo stagingMemoryInfo{};
    stagingMemoryInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    stagingMemoryInfo.allocationSize = bufferRequirements.size;
    stagingMemoryInfo.memoryTypeIndex = stagingMemoryType;
    if (vkAllocateMemory(context.device, &stagingMemoryInfo, nullptr, &stagingMemory) != VK_SUCCESS ||
        vkBindBufferMemory(context.device, stagingBuffer, stagingMemory, 0) != VK_SUCCESS) {
        destroyStaging();
        return false;
    }

    void* mapped = nullptr;
    if (vkMapMemory(context.device, stagingMemory, 0, dataSize, 0, &mapped) != VK_SUCCESS || mapped == nullptr) {
        destroyStaging();
        return false;
    }
    std::memcpy(mapped, pixels, dataSize);
    vkUnmapMemory(context.device, stagingMemory);

    // Async upload: no vkDeviceWaitIdle / vkQueueWaitIdle — the caller may
    // still be recording commands for other frames, so a full device wait would
    // serialize the entire pipeline. Instead we use a fence for per-upload sync.
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(context.commandBuffer, &beginInfo) != VK_SUCCESS) {
        destroyStaging();
        return false;
    }

    VkImageMemoryBarrier toTransfer{};
    toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toTransfer.oldLayout = oldLayout;
    toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.image = texture.image;
    toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toTransfer.subresourceRange.levelCount = 1;
    toTransfer.subresourceRange.layerCount = 1;
    toTransfer.srcAccessMask = oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        ? VK_ACCESS_SHADER_READ_BIT : 0;
    toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(
        context.commandBuffer,
        oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toTransfer);

    VkBufferImageCopy copyRegion{};
    copyRegion.bufferOffset = 0;
    copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.imageSubresource.layerCount = 1;
    copyRegion.imageExtent = {texture.width, texture.height, 1};
    vkCmdCopyBufferToImage(context.commandBuffer, stagingBuffer, texture.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

    VkImageMemoryBarrier toRead{};
    toRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toRead.image = texture.image;
    toRead.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toRead.subresourceRange.levelCount = 1;
    toRead.subresourceRange.layerCount = 1;
    toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(
        context.commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &toRead);

    if (vkEndCommandBuffer(context.commandBuffer) != VK_SUCCESS) {
        destroyStaging();
        return false;
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &context.commandBuffer;

    // Use a fence for per-upload sync instead of vkQueueWaitIdle — avoids
    // blocking the entire GPU pipeline when other frames are still in-flight.
    VkFence uploadFence = VK_NULL_HANDLE;
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    const VkResult fenceResult = vkCreateFence(
        context.device,
        &fenceInfo,
        nullptr, &uploadFence);
    const VkResult submitResult = fenceResult == VK_SUCCESS
        ? vkQueueSubmit(context.queue, 1, &submitInfo, uploadFence)
        : vkQueueSubmit(context.queue, 1, &submitInfo, VK_NULL_HANDLE);

    if (fenceResult == VK_SUCCESS) {
        vkWaitForFences(context.device, 1, &uploadFence, VK_TRUE,
                        std::numeric_limits<uint64_t>::max());
        vkDestroyFence(context.device, uploadFence, nullptr);
    } else if (submitResult == VK_SUCCESS) {
        vkQueueWaitIdle(context.queue);
    }
    destroyStaging();
    return submitResult == VK_SUCCESS;
}

bool TextureManager::createResource(
        const CreateContext& context,
        const TextureDesc& description,
        VkFormat vulkanFormat,
        std::uint32_t bytesPerPixel,
        std::uint32_t scalePercent,
        Resource& texture) noexcept {
    if (context.device == VK_NULL_HANDLE || context.commandBuffer == VK_NULL_HANDLE ||
        description.width == 0 || description.height == 0 || description.pixels == nullptr ||
        description.dataSize == 0 || bytesPerPixel == 0) {
        return false;
    }

    TextureDesc effective = description;
    std::vector<std::uint8_t> downscaled;
    const std::uint32_t scale = std::min(100u, scalePercent);
    if (scale < 100 && (description.width > 4 || description.height > 4)) {
        const std::uint32_t scaledWidth = std::max<std::uint32_t>(1, description.width * scale / 100);
        const std::uint32_t scaledHeight = std::max<std::uint32_t>(1, description.height * scale / 100);
        std::vector<std::uint8_t> boxFiltered(
            static_cast<std::size_t>(scaledWidth) * scaledHeight * bytesPerPixel);
        const auto* source = static_cast<const std::uint8_t*>(description.pixels);
        for (std::uint32_t y = 0; y < scaledHeight; ++y) {
            const std::uint32_t sourceY0 = static_cast<std::uint32_t>(
                static_cast<std::uint64_t>(y) * description.height / scaledHeight);
            const std::uint32_t sourceY1 = std::min(description.height - 1, static_cast<std::uint32_t>(
                static_cast<std::uint64_t>(y + 1) * description.height / scaledHeight));
            for (std::uint32_t x = 0; x < scaledWidth; ++x) {
                const std::uint32_t sourceX0 = static_cast<std::uint32_t>(
                    static_cast<std::uint64_t>(x) * description.width / scaledWidth);
                const std::uint32_t sourceX1 = std::min(description.width - 1, static_cast<std::uint32_t>(
                    static_cast<std::uint64_t>(x + 1) * description.width / scaledWidth));
                for (std::uint32_t component = 0; component < bytesPerPixel; ++component) {
                    std::uint32_t sum = 0;
                    std::uint32_t count = 0;
                    for (std::uint32_t sy = sourceY0; sy <= sourceY1; ++sy) {
                        for (std::uint32_t sx = sourceX0; sx <= sourceX1; ++sx) {
                            sum += source[(static_cast<std::size_t>(sy) * description.width + sx) * bytesPerPixel + component];
                            ++count;
                        }
                    }
                    boxFiltered[(static_cast<std::size_t>(y) * scaledWidth + x) * bytesPerPixel + component] =
                        static_cast<std::uint8_t>(sum / count);
                }
            }
        }
        downscaled = std::move(boxFiltered);
        effective.width = scaledWidth;
        effective.height = scaledHeight;
        effective.pixels = downscaled.data();
        effective.dataSize = downscaled.size();
    }

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = vulkanFormat;
    imageInfo.extent = {effective.width, effective.height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(context.device, &imageInfo, nullptr, &texture.image) != VK_SUCCESS) {
        destroyResource(context.device, texture);
        return false;
    }

    VkMemoryRequirements imageRequirements{};
    vkGetImageMemoryRequirements(context.device, texture.image, &imageRequirements);
    std::uint32_t imageMemoryType = 0;
    if (!findMemoryType(context.memoryProperties, imageRequirements.memoryTypeBits,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, imageMemoryType)) {
        destroyResource(context.device, texture);
        return false;
    }
    VkMemoryAllocateInfo imageMemoryInfo{};
    imageMemoryInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    imageMemoryInfo.allocationSize = imageRequirements.size;
    imageMemoryInfo.memoryTypeIndex = imageMemoryType;
    if (vkAllocateMemory(context.device, &imageMemoryInfo, nullptr, &texture.memory) != VK_SUCCESS ||
        vkBindImageMemory(context.device, texture.image, texture.memory, 0) != VK_SUCCESS) {
        destroyResource(context.device, texture);
        return false;
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = texture.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = vulkanFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    if (vkCreateImageView(context.device, &viewInfo, nullptr, &texture.view) != VK_SUCCESS) {
        destroyResource(context.device, texture);
        return false;
    }

    VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    switch (description.wrap) {
    case SamplerMode::Clamp: addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; break;
    case SamplerMode::MirroredRepeat: addressMode = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT; break;
    case SamplerMode::Repeat:
    default: addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT; break;
    }

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = addressMode;
    samplerInfo.addressModeV = addressMode;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxLod = 1.0f;
    if (vkCreateSampler(context.device, &samplerInfo, nullptr, &texture.sampler) != VK_SUCCESS) {
        destroyResource(context.device, texture);
        return false;
    }

    texture.format = vulkanFormat;
    texture.width = effective.width;
    texture.height = effective.height;
    texture.dataSize = effective.dataSize;

    UploadContext upload;
    upload.device = context.device;
    upload.memoryProperties = context.memoryProperties;
    upload.commandBuffer = context.commandBuffer;
    upload.queue = context.queue;
    if (!uploadTexture(upload, texture, effective.pixels, effective.dataSize, VK_IMAGE_LAYOUT_UNDEFINED)) {
        destroyResource(context.device, texture);
        return false;
    }
    return true;
}

void TextureManager::destroyResource(VkDevice device, Resource& texture) noexcept {
    if (device != VK_NULL_HANDLE) {
        if (texture.sampler != VK_NULL_HANDLE) vkDestroySampler(device, texture.sampler, nullptr);
        if (texture.view != VK_NULL_HANDLE) vkDestroyImageView(device, texture.view, nullptr);
        if (texture.image != VK_NULL_HANDLE) vkDestroyImage(device, texture.image, nullptr);
        if (texture.memory != VK_NULL_HANDLE) vkFreeMemory(device, texture.memory, nullptr);
    }
    texture = {};
}

bool TextureManager::createSolid(
        const CreateContext& context,
        std::uint8_t r,
        std::uint8_t g,
        std::uint8_t b,
        std::uint8_t a,
        Resource& texture) noexcept {
    const std::uint8_t pixels[4] = {r, g, b, a};
    TextureDesc description;
    description.width = 1;
    description.height = 1;
    description.format = TextureFormat::Rgba8Unorm;
    description.pixels = pixels;
    description.dataSize = sizeof(pixels);
    return createResource(context, description, VK_FORMAT_R8G8B8A8_UNORM, 4, 100, texture);
}

} // namespace Aporia::graphics_internal
