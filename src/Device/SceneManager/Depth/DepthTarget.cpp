#include "DepthTarget.hpp"

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

bool DepthTarget::create(
        VkDevice device,
        const VkPhysicalDeviceMemoryProperties& memoryProperties,
        VkFormat depthFormat,
        VkExtent2D extent,
        VkImage& outImage,
        VkDeviceMemory& outMemory,
        VkImageView& outView) noexcept {
    outImage = VK_NULL_HANDLE;
    outMemory = VK_NULL_HANDLE;
    outView = VK_NULL_HANDLE;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = depthFormat;
    imageInfo.extent = {extent.width, extent.height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device, &imageInfo, nullptr, &outImage) != VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device, outImage, &requirements);
    std::uint32_t memoryType = 0;
    if (!findMemoryType(memoryProperties, requirements.memoryTypeBits,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memoryType)) {
        return false;
    }
    VkMemoryAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = requirements.size;
    allocateInfo.memoryTypeIndex = memoryType;
    if (vkAllocateMemory(device, &allocateInfo, nullptr, &outMemory) != VK_SUCCESS ||
        vkBindImageMemory(device, outImage, outMemory, 0) != VK_SUCCESS) {
        return false;
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = outImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = depthFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    return vkCreateImageView(device, &viewInfo, nullptr, &outView) == VK_SUCCESS;
}

} // namespace Aporia::graphics_internal
