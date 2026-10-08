#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "QualityTargets.hpp"

#include "Device/Init/RenderPass.hpp"
#include "Device/SceneManager/Color/ColorTarget.hpp"
#include "Device/SceneManager/Depth/DepthTarget.hpp"
#include "Graphics/GlobalDrawer/Support/Support.hpp"
#include "Graphics/TextureManager/TextureManager.hpp"
#include "Graphics/TextureManager/TextureStore.hpp"

#include <algorithm>
#include <array>

namespace Aporia::graphics_internal {

VkFormat QualityTargets::chooseDepthFormat() const noexcept {
    return graphics_internal::chooseDepthFormat(gpu.physicalDevice);
}

bool QualityTargets::createColorTarget(
    VkFormat depthFormatToken,
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
    VkDeviceMemory& outDepthMemory,
    std::string& error) {
    if (!ColorTarget::create(
            gpu.device, gpu.memoryProperties, depthFormatToken, format, extraUsage, width, height,
            compatibleRenderPass, outView, outFramebuffer, outImage, outMemory,
            outDepthView, outDepthImage, outDepthMemory)) {
        error = "Quality target creation failed";
        return false;
    }
    return true;
}

bool QualityTargets::ensureDepthForExtent(VkFormat& depthFormat, std::uint32_t width, std::uint32_t height, std::string& error) {
    if (depthFormat == VK_FORMAT_UNDEFINED) {
        depthFormat = chooseDepthFormat();
        if (depthFormat == VK_FORMAT_UNDEFINED) {
            error = "No supported depth attachment format";
            return false;
        }
    }
    if (depthImage != VK_NULL_HANDLE && depthWidth == width && depthHeight == height) {
        return true;
    }
    if (depthImage != VK_NULL_HANDLE) {
        vkDestroyImageView(gpu.device, depthView, nullptr);
        vkDestroyImage(gpu.device, depthImage, nullptr);
        vkFreeMemory(gpu.device, depthMemory, nullptr);
        depthImage = VK_NULL_HANDLE;
        depthMemory = VK_NULL_HANDLE;
        depthView = VK_NULL_HANDLE;
    }
    if (!DepthTarget::create(gpu.device, gpu.memoryProperties, depthFormat, {width, height},
                             depthImage, depthMemory, depthView)) {
        error = "Depth image creation failed";
        return false;
    }
    depthWidth = width;
    depthHeight = height;
    return true;
}

void QualityTargets::destroyDepth(VkDevice device) noexcept {
    if (device != VK_NULL_HANDLE) {
        if (depthView != VK_NULL_HANDLE) {
            vkDestroyImageView(device, depthView, nullptr);
        }
        if (depthImage != VK_NULL_HANDLE) {
            vkDestroyImage(device, depthImage, nullptr);
        }
        if (depthMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, depthMemory, nullptr);
        }
    }
    depthImage = VK_NULL_HANDLE;
    depthMemory = VK_NULL_HANDLE;
    depthView = VK_NULL_HANDLE;
    depthWidth = 0;
    depthHeight = 0;
}

bool QualityTargets::createHdrRenderPass(VkFormat depthFormat, std::string& error) {
    if (hdrRenderPass != VK_NULL_HANDLE) {
        return true;
    }
    if (depthFormat == VK_FORMAT_UNDEFINED) {
        depthFormat = chooseDepthFormat();
        if (depthFormat == VK_FORMAT_UNDEFINED) {
            error = "No supported depth attachment format";
            return false;
        }
    }
    if (!RenderPassFactory::create(
            gpu.device, hdrFormat, depthFormat, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, hdrRenderPass)) {
        error = "vkCreateRenderPass(hdr) failed";
        return false;
    }
    return true;
}

bool QualityTargets::createReflectRenderPass(VkFormat colorFormat, VkFormat depthFormat, std::string& error) {
    if (reflectRenderPass != VK_NULL_HANDLE) {
        return true;
    }
    if (depthFormat == VK_FORMAT_UNDEFINED) {
        depthFormat = chooseDepthFormat();
        if (depthFormat == VK_FORMAT_UNDEFINED) {
            error = "No supported depth attachment format";
            return false;
        }
    }
    if (!RenderPassFactory::create(
            gpu.device, colorFormat, depthFormat, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, reflectRenderPass)) {
        error = "vkCreateRenderPass(reflect) failed";
        return false;
    }
    return true;
}

bool QualityTargets::createShadowResources(VkFormat depthFormat, std::uint32_t mapSize, std::string& error) {
    if (depthFormat == VK_FORMAT_UNDEFINED) {
        depthFormat = chooseDepthFormat();
        if (depthFormat == VK_FORMAT_UNDEFINED) {
            error = "No supported depth attachment format";
            return false;
        }
    }
    if (!ShadowRenderer::createResources(gpu.device, gpu.memoryProperties, depthFormat, mapSize, shadow)) {
        error = "Shadow map resource creation failed";
        return false;
    }

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.maxLod = 1.0f;
    if (vkCreateSampler(gpu.device, &samplerInfo, nullptr, &reflectSampler) != VK_SUCCESS) {
        error = "vkCreateSampler(reflection) failed";
        return false;
    }
    return true;
}

bool QualityTargets::rebuild(const RebuildContext& rebuildContext, std::string& error) {
    const RebuildContext& ctx = rebuildContext;
    const bool headless = ctx.headless;
    const std::uint32_t mainWidth = headless ? ctx.renderWidth : ctx.swapchainExtent.width;
    const std::uint32_t mainHeight = headless ? ctx.renderHeight : ctx.swapchainExtent.height;
    if (mainWidth == 0 || mainHeight == 0) {
        error = "Quality targets require a valid main target size";
        return false;
    }

    vkDeviceWaitIdle(gpu.device);
    destroyTargets(gpu.device, ctx.tonemapDescriptorPool, *ctx.taa, *ctx.scene);

    if (hdrFormat == VK_FORMAT_UNDEFINED) {
        hdrFormat = chooseHdrFormat(gpu.physicalDevice);
        if (hdrFormat == VK_FORMAT_UNDEFINED) {
            error = "No supported HDR color format";
            return false;
        }
    }

    if (ctx.hdrEnabled) {
        if (!createColorTarget(
                ctx.depthFormat,
                hdrFormat,
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                mainWidth,
                mainHeight,
                hdrRenderPass != VK_NULL_HANDLE ? hdrRenderPass : ctx.mainRenderPass,
                hdrView,
                hdrFramebuffer,
                hdrImage,
                hdrMemory,
                hdrDepthView,
                hdrDepthImage,
                hdrDepthMemory,
                error)) {
            return false;
        }
        hdrWidth = mainWidth;
        hdrHeight = mainHeight;

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.maxLod = 1.0f;
        if (vkCreateSampler(gpu.device, &samplerInfo, nullptr, &hdrSampler) != VK_SUCCESS) {
            error = "vkCreateSampler(hdr) failed";
            return false;
        }

        // Tonemap pass samples the HDR target.
        VkDescriptorSetAllocateInfo allocateSetInfo{};
        allocateSetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocateSetInfo.descriptorPool = ctx.tonemapDescriptorPool;
        allocateSetInfo.descriptorSetCount = 1;
        allocateSetInfo.pSetLayouts = &ctx.tonemapDescriptorLayout;
        if (vkAllocateDescriptorSets(gpu.device, &allocateSetInfo, &tonemapDescriptorSet) != VK_SUCCESS) {
            error = "vkAllocateDescriptorSets(tonemap) failed";
            return false;
        }
        VkDescriptorImageInfo hdrDescriptor{};
        hdrDescriptor.sampler = hdrSampler;
        hdrDescriptor.imageView = hdrView;
        hdrDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = tonemapDescriptorSet;
        write.dstBinding = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &hdrDescriptor;
        vkUpdateDescriptorSets(gpu.device, 1, &write, 0, nullptr);
    }

    if (ctx.hdrEnabled && static_cast<AntiAliasingMode>(ctx.antiAliasing) == AntiAliasingMode::TAA) {
        TaaRenderer::Context taaContext;
        taaContext.device = gpu.device;
        taaContext.memoryProperties = gpu.memoryProperties;
        taaContext.commandBuffer = gpu.commandBuffer;
        taaContext.queue = gpu.queue;
        taaContext.renderPass = hdrRenderPass;
        taaContext.hdrFormat = hdrFormat;
        taaContext.depthFormat = ctx.depthFormat;
        taaContext.vertexShader = ctx.tonemapVertexShader;
        taaContext.fragmentShader = ctx.taaFragmentShader;
        taaContext.colorView = hdrView;
        taaContext.depthView = hdrDepthView;
        taaContext.colorSampler = hdrSampler;
        if (!ctx.taa->initialize(taaContext, hdrWidth, hdrHeight)) {
            error = "TAA initialization failed";
            return false;
        }
        // Tonemap samples the resolved image while TAA is active.
        if (tonemapDescriptorSet != VK_NULL_HANDLE) {
            VkDescriptorImageInfo resolveInfo{};
            resolveInfo.sampler = hdrSampler;
            resolveInfo.imageView = ctx.taa->resolveView();
            resolveInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = tonemapDescriptorSet;
            write.dstBinding = 0;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.descriptorCount = 1;
            write.pImageInfo = &resolveInfo;
            vkUpdateDescriptorSets(gpu.device, 1, &write, 0, nullptr);
        }
    }

    if (ctx.reflectionsEnabled) {
        if (!createReflectRenderPass(ctx.colorFormat, ctx.depthFormat, error)) {
            return false;
        }
        const std::uint32_t reflectTargetWidth = std::max(1u, static_cast<std::uint32_t>(
            static_cast<float>(mainWidth) * ctx.reflectionScale));
        const std::uint32_t reflectTargetHeight = std::max(1u, static_cast<std::uint32_t>(
            static_cast<float>(mainHeight) * ctx.reflectionScale));
        if (!createColorTarget(
                ctx.depthFormat,
                ctx.colorFormat,
                0,
                reflectTargetWidth,
                reflectTargetHeight,
                reflectRenderPass != VK_NULL_HANDLE ? reflectRenderPass : ctx.mainRenderPass,
                reflectView,
                reflectFramebuffer,
                reflectImage,
                reflectMemory,
                reflectDepthView,
                reflectDepthImage,
                reflectDepthMemory,
                error)) {
            return false;
        }
        reflectWidth = reflectTargetWidth;
        reflectHeight = reflectTargetHeight;
    }

    if (!createShadowResources(ctx.depthFormat, ctx.shadowMapSize, error)) {
        return false;
    }
    const auto& storeTextures = ctx.textures->resources();
    const auto defaultIt = storeTextures.find(ctx.textures->defaultHandle().value());
    const auto textureIt = storeTextures.find(ctx.textures->boundHandle().value());
    const auto& albedoResource = textureIt != storeTextures.end() ? textureIt->second : defaultIt->second;
    if (!ctx.scene->allocate(gpu.device, gpu.memoryProperties, ctx.sceneUniformSize,
                             albedoResource.view, albedoResource.sampler,
                             shadow.view, shadow.sampler)) {
        return false;
    }
    if (ctx.reflectionsEnabled && reflectView != VK_NULL_HANDLE) {
        ctx.scene->bindReflection(reflectView, reflectSampler);
    }
    return true;
}

void QualityTargets::destroyTargets(VkDevice device, VkDescriptorPool tonemapPool, TaaRenderer& taa, SceneBindings& scene) noexcept {
    taa.shutdown();
    if (device != VK_NULL_HANDLE) {
        if (hdrFramebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device, hdrFramebuffer, nullptr);
        }
        if (hdrSampler != VK_NULL_HANDLE) {
            vkDestroySampler(device, hdrSampler, nullptr);
        }
        if (hdrDepthView != VK_NULL_HANDLE) {
            vkDestroyImageView(device, hdrDepthView, nullptr);
        }
        if (hdrDepthImage != VK_NULL_HANDLE) {
            vkDestroyImage(device, hdrDepthImage, nullptr);
        }
        if (hdrDepthMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, hdrDepthMemory, nullptr);
        }
        if (hdrView != VK_NULL_HANDLE) {
            vkDestroyImageView(device, hdrView, nullptr);
        }
        if (hdrImage != VK_NULL_HANDLE) {
            vkDestroyImage(device, hdrImage, nullptr);
        }
        if (hdrMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, hdrMemory, nullptr);
        }
        if (reflectFramebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device, reflectFramebuffer, nullptr);
        }
        if (reflectDepthView != VK_NULL_HANDLE) {
            vkDestroyImageView(device, reflectDepthView, nullptr);
        }
        if (reflectDepthImage != VK_NULL_HANDLE) {
            vkDestroyImage(device, reflectDepthImage, nullptr);
        }
        if (reflectDepthMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, reflectDepthMemory, nullptr);
        }
        if (reflectView != VK_NULL_HANDLE) {
            vkDestroyImageView(device, reflectView, nullptr);
        }
        if (reflectImage != VK_NULL_HANDLE) {
            vkDestroyImage(device, reflectImage, nullptr);
        }
        if (reflectMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, reflectMemory, nullptr);
        }
        ShadowRenderer::destroyResources(device, shadow);
        if (reflectRenderPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(device, reflectRenderPass, nullptr);
        }
        if (reflectSampler != VK_NULL_HANDLE) {
            vkDestroySampler(device, reflectSampler, nullptr);
        }
        scene.releaseResources(device);
        if (tonemapPool != VK_NULL_HANDLE) {
            // Pool is owned by initialize/destroy; rebuild only reclaims
            // the tonemap descriptor set allocated from it.
            vkResetDescriptorPool(device, tonemapPool, 0);
        }
    }
    hdrImage = VK_NULL_HANDLE;
    hdrMemory = VK_NULL_HANDLE;
    hdrView = VK_NULL_HANDLE;
    hdrSampler = VK_NULL_HANDLE;
    hdrFramebuffer = VK_NULL_HANDLE;
    hdrDepthImage = VK_NULL_HANDLE;
    hdrDepthMemory = VK_NULL_HANDLE;
    hdrDepthView = VK_NULL_HANDLE;
    hdrWidth = 0;
    hdrHeight = 0;
    reflectImage = VK_NULL_HANDLE;
    reflectMemory = VK_NULL_HANDLE;
    reflectView = VK_NULL_HANDLE;
    reflectFramebuffer = VK_NULL_HANDLE;
    reflectDepthImage = VK_NULL_HANDLE;
    reflectDepthMemory = VK_NULL_HANDLE;
    reflectDepthView = VK_NULL_HANDLE;
    reflectWidth = 0;
    reflectHeight = 0;
    shadow = {};
    reflectSampler = VK_NULL_HANDLE;
    reflectRenderPass = VK_NULL_HANDLE;
    tonemapDescriptorSet = VK_NULL_HANDLE;
}

} // namespace Aporia::graphics_internal
