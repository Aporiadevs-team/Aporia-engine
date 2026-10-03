#include "HdrRenderer.hpp"
#include "../Rectangulars/RectRenderer.hpp"

#include <array>

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

bool HdrRenderer::createDepthImage(
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
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
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

bool HdrRenderer::createColorTarget(
        VkDevice device,
        const VkPhysicalDeviceMemoryProperties& memoryProperties,
        VkFormat depthFormat,
        VkFormat format,
        VkImageUsageFlags extraUsage,
        std::uint32_t width,
        std::uint32_t height,
        VkRenderPass compatibleRenderPass,
        VkImageView& outView,
        VkFramebuffer& outFramebuffer,
        VkImage& outImage,
        VkDeviceMemory& outMemory,
        VkImageView& outDepthView,
        VkImage& outDepthImage,
        VkDeviceMemory& outDepthMemory) noexcept {
    outImage = VK_NULL_HANDLE;
    outMemory = VK_NULL_HANDLE;
    outView = VK_NULL_HANDLE;
    outFramebuffer = VK_NULL_HANDLE;
    outDepthImage = VK_NULL_HANDLE;
    outDepthMemory = VK_NULL_HANDLE;
    outDepthView = VK_NULL_HANDLE;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent = {width, height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | extraUsage;
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
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device, &viewInfo, nullptr, &outView) != VK_SUCCESS) {
        return false;
    }

    if (compatibleRenderPass == VK_NULL_HANDLE) {
        return true;
    }
    if (!createDepthImage(device, memoryProperties, depthFormat, {width, height},
                          outDepthImage, outDepthMemory, outDepthView)) {
        return false;
    }
    std::array<VkImageView, 2> attachments{outView, outDepthView};
    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = compatibleRenderPass;
    framebufferInfo.attachmentCount = static_cast<std::uint32_t>(attachments.size());
    framebufferInfo.pAttachments = attachments.data();
    framebufferInfo.width = width;
    framebufferInfo.height = height;
    framebufferInfo.layers = 1;
    return vkCreateFramebuffer(device, &framebufferInfo, nullptr, &outFramebuffer) == VK_SUCCESS;
}

bool HdrRenderer::createRenderPass(
        VkDevice device,
        VkFormat colorFormat,
        VkFormat depthFormat,
        VkImageLayout colorFinalLayout,
        VkRenderPass& outRenderPass) noexcept {
    if (outRenderPass != VK_NULL_HANDLE) {
        return true;
    }

    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = colorFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = colorFinalLayout;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = depthFormat;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    std::array<VkAttachmentDescription, 2> attachments{colorAttachment, depthAttachment};

    VkAttachmentReference colorReference{};
    colorReference.attachment = 0;
    colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthReference{};
    depthReference.attachment = 1;
    depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorReference;
    subpass.pDepthStencilAttachment = &depthReference;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<std::uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    return vkCreateRenderPass(device, &renderPassInfo, nullptr, &outRenderPass) == VK_SUCCESS;
}

void HdrRenderer::renderTonemap(
        const TonemapContext& context,
        VkFramebuffer target,
        std::uint32_t width,
        std::uint32_t height,
        const std::function<void(VkCommandBuffer, VkPipelineLayout)>& overlay) noexcept {
    if (context.commandBuffer == VK_NULL_HANDLE || context.renderPass == VK_NULL_HANDLE ||
        target == VK_NULL_HANDLE || width == 0 || height == 0) {
        return;
    }

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    begin.renderPass = context.renderPass;
    begin.framebuffer = target;
    begin.renderArea.extent = {width, height};
    begin.clearValueCount = static_cast<std::uint32_t>(clearValues.size());
    begin.pClearValues = clearValues.data();
    vkCmdBeginRenderPass(context.commandBuffer, &begin, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{};
    viewport.width = static_cast<float>(width);
    viewport.height = static_cast<float>(height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(context.commandBuffer, 0, 1, &viewport);
    VkRect2D scissor{};
    scissor.extent = {width, height};
    vkCmdSetScissor(context.commandBuffer, 0, 1, &scissor);

    vkCmdBindPipeline(context.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, context.pipeline);
    vkCmdBindDescriptorSets(context.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            context.pipelineLayout, 0, 1, &context.descriptorSet, 0, nullptr);
    // Base resolve: neutral push constants draw the tonemapped frame across
    // the whole target. Overlay draws then blend UI on top.
    RectRenderer::PushConstants neutral{};
    vkCmdPushConstants(context.commandBuffer, context.pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(neutral), &neutral);
    vkCmdDraw(context.commandBuffer, 4, 1, 0, 0);
    if (overlay) {
        overlay(context.commandBuffer, context.pipelineLayout);
    }
    vkCmdEndRenderPass(context.commandBuffer);
}

} // namespace Aporia::graphics_internal
