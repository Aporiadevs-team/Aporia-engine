#include "ShadowRenderer.hpp"
#include "Aporia/Math/Matrix4.hpp"

#include <algorithm>
#include <cmath>
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

bool ShadowRenderer::createRenderPass(
        VkDevice device,
        VkFormat depthFormat,
        Resources& resources) noexcept {
    if (resources.renderPass != VK_NULL_HANDLE) {
        return true;
    }

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = depthFormat;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    VkAttachmentReference depthReference{};
    depthReference.attachment = 0;
    depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.pDepthStencilAttachment = &depthReference;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &depthAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    return vkCreateRenderPass(device, &renderPassInfo, nullptr, &resources.renderPass) == VK_SUCCESS;
}

bool ShadowRenderer::createTargets(
        VkDevice device,
        const VkPhysicalDeviceMemoryProperties& memoryProperties,
        VkFormat depthFormat,
        std::uint32_t mapSize,
        Resources& resources) noexcept {
    if (device == VK_NULL_HANDLE || depthFormat == VK_FORMAT_UNDEFINED || mapSize == 0) {
        return false;
    }
    destroyTargets(device, resources);

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = depthFormat;
    imageInfo.extent = {mapSize, mapSize, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device, &imageInfo, nullptr, &resources.image) != VK_SUCCESS) {
        destroyTargets(device, resources);
        return false;
    }

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device, resources.image, &requirements);
    std::uint32_t memoryType = 0;
    if (!findMemoryType(memoryProperties, requirements.memoryTypeBits,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memoryType)) {
        destroyTargets(device, resources);
        return false;
    }
    VkMemoryAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = requirements.size;
    allocateInfo.memoryTypeIndex = memoryType;
    if (vkAllocateMemory(device, &allocateInfo, nullptr, &resources.memory) != VK_SUCCESS ||
        vkBindImageMemory(device, resources.image, resources.memory, 0) != VK_SUCCESS) {
        destroyTargets(device, resources);
        return false;
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = resources.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = depthFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device, &viewInfo, nullptr, &resources.view) != VK_SUCCESS) {
        destroyTargets(device, resources);
        return false;
    }

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    // Depth must be sampled point-wise: linear filtering interpolates between
    // two surfaces at a silhouette and makes the shadow edge crawl and swim.
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.maxLod = 0.0f;
    if (vkCreateSampler(device, &samplerInfo, nullptr, &resources.sampler) != VK_SUCCESS) {
        destroyTargets(device, resources);
        return false;
    }

    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = resources.renderPass;
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments = &resources.view;
    framebufferInfo.width = mapSize;
    framebufferInfo.height = mapSize;
    framebufferInfo.layers = 1;
    if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &resources.framebuffer) != VK_SUCCESS) {
        destroyTargets(device, resources);
        return false;
    }

    resources.mapSize = mapSize;
    return true;
}

bool ShadowRenderer::createResources(
        VkDevice device,
        const VkPhysicalDeviceMemoryProperties& memoryProperties,
        VkFormat depthFormat,
        std::uint32_t mapSize,
        Resources& resources) noexcept {
    if (!createRenderPass(device, depthFormat, resources)) {
        return false;
    }
    return createTargets(device, memoryProperties, depthFormat, mapSize, resources);
}

void ShadowRenderer::destroyTargets(VkDevice device, Resources& resources) noexcept {
    if (device != VK_NULL_HANDLE) {
        if (resources.framebuffer != VK_NULL_HANDLE) vkDestroyFramebuffer(device, resources.framebuffer, nullptr);
        if (resources.sampler != VK_NULL_HANDLE) vkDestroySampler(device, resources.sampler, nullptr);
        if (resources.view != VK_NULL_HANDLE) vkDestroyImageView(device, resources.view, nullptr);
        if (resources.image != VK_NULL_HANDLE) vkDestroyImage(device, resources.image, nullptr);
        if (resources.memory != VK_NULL_HANDLE) vkFreeMemory(device, resources.memory, nullptr);
    }
    resources.framebuffer = VK_NULL_HANDLE;
    resources.sampler = VK_NULL_HANDLE;
    resources.view = VK_NULL_HANDLE;
    resources.image = VK_NULL_HANDLE;
    resources.memory = VK_NULL_HANDLE;
    resources.mapSize = 0;
}

void ShadowRenderer::destroyResources(VkDevice device, Resources& resources) noexcept {
    destroyTargets(device, resources);
    if (device != VK_NULL_HANDLE && resources.renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, resources.renderPass, nullptr);
    }
    resources = {};
}

void ShadowRenderer::renderShadowMap(
        const RenderContext& context,
        const float sunMatrix[16],
        const std::vector<RenderItem>& items,
        const std::unordered_map<std::uint64_t, MeshRenderer::Resource>& meshes) noexcept {
    if (context.commandBuffer == VK_NULL_HANDLE || context.renderPass == VK_NULL_HANDLE ||
        context.framebuffer == VK_NULL_HANDLE || context.mapSize == 0) {
        return;
    }

    VkClearValue clearValue{};
    clearValue.depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    begin.renderPass = context.renderPass;
    begin.framebuffer = context.framebuffer;
    begin.renderArea.extent = {context.mapSize, context.mapSize};
    begin.clearValueCount = 1;
    begin.pClearValues = &clearValue;
    vkCmdBeginRenderPass(context.commandBuffer, &begin, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{};
    viewport.width = static_cast<float>(context.mapSize);
    viewport.height = static_cast<float>(context.mapSize);
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(context.commandBuffer, 0, 1, &viewport);
    VkRect2D scissor{};
    scissor.extent = {context.mapSize, context.mapSize};
    vkCmdSetScissor(context.commandBuffer, 0, 1, &scissor);

    MeshRenderer::renderMeshes(context.commandBuffer, VK_NULL_HANDLE, context.pipelineLayout,
                               context.descriptorSet, sunMatrix, items, meshes, context.pipeline);
    vkCmdEndRenderPass(context.commandBuffer);
}

void ShadowRenderer::buildSunMatrix(
        const float sunDirectionIn[3],
        const double cameraPositionIn[3],
        float shadowExtent,
        std::uint32_t mapSize,
        float outMatrix[16]) noexcept {
    float directionX = sunDirectionIn[0];
    float directionY = sunDirectionIn[1];
    float directionZ = sunDirectionIn[2];
    const float length = std::sqrt(directionX * directionX + directionY * directionY + directionZ * directionZ);
    if (length <= 1.0e-6f) {
        directionX = 0.35f;
        directionY = -0.5f;
        directionZ = 0.8f;
    } else {
        directionX /= length;
        directionY /= length;
        directionZ /= length;
    }
    if (std::abs(directionZ) > 0.999f) {
        directionZ = directionZ > 0.0f ? 0.999f : -0.999f;
    }

    const Aporia::Vec3 sunDirection(directionX, directionY, directionZ);
    const Aporia::Vec3 cameraPosition(cameraPositionIn[0], cameraPositionIn[1], cameraPositionIn[2]);
    Aporia::Vec3 center = cameraPosition;
    const Aporia::Vec3 up = std::abs(directionZ) < 0.9f ? Aporia::Vec3(0.0, 0.0, 1.0) : Aporia::Vec3(1.0, 0.0, 0.0);
    const double extent = std::max(1.0, static_cast<double>(shadowExtent));
    // The sun eye sits far enough that the whole ortho box (plus room for tall
    // casters) fits between near and far, whatever extent the host picked.
    const double eyeDistance = std::max(60.0, extent + 40.0);
    const double farPlane = std::max(200.0, eyeDistance + extent + 80.0);
    const Aporia::Vec3 lightEye = center + sunDirection * eyeDistance;
    const Aporia::Mat4 unsnappedView = Aporia::Mat4::lookAt(lightEye, center, up);
    const Aporia::Vec3 right{unsnappedView.m[0][0], unsnappedView.m[1][0], unsnappedView.m[2][0]};
    const Aporia::Vec3 correctedUp{unsnappedView.m[0][1], unsnappedView.m[1][1], unsnappedView.m[2][1]};
    // The light-space position of the camera centre. offset itself is parallel
    // to the sun direction, so projecting IT onto right/up always gives zero --
    // the snap has to work on the absolute light-space centre, otherwise the
    // texel grid slides with the camera and every shadow edge swims.
    const double lightX = dot(right, center);
    const double lightY = dot(correctedUp, center);
    const double texelWorldSize = (extent * 2.0) / static_cast<double>(std::max(1u, mapSize));
    if (texelWorldSize > 0.0) {
        const double snappedX = std::round(lightX / texelWorldSize) * texelWorldSize;
        const double snappedY = std::round(lightY / texelWorldSize) * texelWorldSize;
        center += right * (snappedX - lightX) + correctedUp * (snappedY - lightY);
    }
    const Aporia::Vec3 eye = center + sunDirection * eyeDistance;
    const Aporia::Mat4 view = Aporia::Mat4::lookAt(eye, center, up);
    const Aporia::Mat4 projection =
        Aporia::Mat4::orthographicVulkan(extent * 2.0, extent * 2.0, 0.1, farPlane);
    const Aporia::Mat4 sunViewProj = projection * view;
    std::memcpy(outMatrix, &sunViewProj, sizeof(sunViewProj));
}

} // namespace Aporia::graphics_internal
