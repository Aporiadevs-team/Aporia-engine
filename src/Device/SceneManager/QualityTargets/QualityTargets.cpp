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
#include "Device/MemoryManager/MemoryManager.hpp"
#include "Device/SceneManager/Color/ColorTarget.hpp"
#include "Device/SceneManager/Depth/DepthTarget.hpp"
#include "Graphics/GlobalDrawer/Support/Support.hpp"
#include "Graphics/TextureManager/TextureManager.hpp"
#include "Graphics/TextureManager/TextureStore.hpp"

#include <algorithm>
#include <array>
#include <cstring>

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
#if defined(APORIA_EXPERIMENTAL)
        // Froxel fog inputs: a nearest depth sampler, the integration UBO and the
        // grid view produced by VolumetricRenderer.
        if (tonemapDepthSampler == VK_NULL_HANDLE) {
            VkSamplerCreateInfo depthSamplerInfo{};
            depthSamplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            depthSamplerInfo.magFilter = VK_FILTER_NEAREST;
            depthSamplerInfo.minFilter = VK_FILTER_NEAREST;
            depthSamplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            depthSamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            depthSamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            depthSamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            depthSamplerInfo.maxLod = 0.0f;
            if (vkCreateSampler(gpu.device, &depthSamplerInfo, nullptr, &tonemapDepthSampler) != VK_SUCCESS) {
                error = "vkCreateSampler(tonemap depth) failed";
                return false;
            }
        }
        if (tonemapVolumetricBuffer == VK_NULL_HANDLE) {
            VkBufferCreateInfo bufferInfo{};
            bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferInfo.size = sizeof(VolumetricUniforms);
            bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
            bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            if (vkCreateBuffer(gpu.device, &bufferInfo, nullptr, &tonemapVolumetricBuffer) != VK_SUCCESS) {
                error = "vkCreateBuffer(tonemap volumetric) failed";
                return false;
            }
            VkMemoryRequirements requirements{};
            vkGetBufferMemoryRequirements(gpu.device, tonemapVolumetricBuffer, &requirements);
            std::uint32_t memoryType = 0;
            if (!MemoryManager::findMemoryType(gpu.memoryProperties, requirements.memoryTypeBits,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, memoryType)) {
                error = "No host-visible memory for the tonemap volumetric buffer";
                return false;
            }
            VkMemoryAllocateInfo allocateInfo{};
            allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocateInfo.allocationSize = requirements.size;
            allocateInfo.memoryTypeIndex = memoryType;
            if (vkAllocateMemory(gpu.device, &allocateInfo, nullptr, &tonemapVolumetricMemory) != VK_SUCCESS ||
                vkBindBufferMemory(gpu.device, tonemapVolumetricBuffer, tonemapVolumetricMemory, 0) != VK_SUCCESS) {
                error = "vkAllocateMemory(tonemap volumetric) failed";
                return false;
            }
        }
        VkDescriptorImageInfo depthDescriptor{};
        depthDescriptor.sampler = tonemapDepthSampler;
        depthDescriptor.imageView = hdrDepthView;
        // The scene depth is transitioned to SHADER_READ_ONLY before the tonemap
        // pass (by TAA, or by the explicit barrier in Frame when TAA is off).
        depthDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkDescriptorImageInfo froxelDescriptor{};
        froxelDescriptor.sampler = ctx.froxelSampler;
        froxelDescriptor.imageView = ctx.froxelView;
        froxelDescriptor.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        VkDescriptorBufferInfo volumetricBuffer{};
        volumetricBuffer.buffer = tonemapVolumetricBuffer;
        volumetricBuffer.offset = 0;
        volumetricBuffer.range = sizeof(VolumetricUniforms);
        std::array<VkWriteDescriptorSet, 4> writes{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = tonemapDescriptorSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].descriptorCount = 1;
        writes[0].pImageInfo = &hdrDescriptor;
        writes[1] = writes[0];
        writes[1].dstBinding = 1;
        writes[1].pImageInfo = &depthDescriptor;
        writes[2] = writes[0];
        writes[2].dstBinding = 2;
        writes[2].pImageInfo = &froxelDescriptor;
        writes[3] = writes[0];
        writes[3].dstBinding = 3;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[3].pImageInfo = nullptr;
        writes[3].pBufferInfo = &volumetricBuffer;
        vkUpdateDescriptorSets(gpu.device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
#else
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = tonemapDescriptorSet;
        write.dstBinding = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &hdrDescriptor;
        vkUpdateDescriptorSets(gpu.device, 1, &write, 0, nullptr);
#endif

        // Half-res separable blur chain for the UI glass (see QualityTargets.hpp).
        const std::uint32_t blurW = std::max(1u, mainWidth / 2);
        const std::uint32_t blurH = std::max(1u, mainHeight / 2);
        if (!createColorTarget(
                ctx.depthFormat,
                hdrFormat,
                0,
                blurW,
                blurH,
                hdrRenderPass,
                blurA.view,
                blurA.framebuffer,
                blurA.image,
                blurA.memory,
                blurA.depthView,
                blurA.depthImage,
                blurA.depthMemory,
                error) ||
            !createColorTarget(
                ctx.depthFormat,
                hdrFormat,
                0,
                blurW,
                blurH,
                hdrRenderPass,
                blurB.view,
                blurB.framebuffer,
                blurB.image,
                blurB.memory,
                blurB.depthView,
                blurB.depthImage,
                blurB.depthMemory,
                error)) {
            return false;
        }
        blurWidth = blurW;
        blurHeight = blurH;

        VkSamplerCreateInfo blurSamplerInfo{};
        blurSamplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        blurSamplerInfo.magFilter = VK_FILTER_LINEAR;
        blurSamplerInfo.minFilter = VK_FILTER_LINEAR;
        blurSamplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        blurSamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        blurSamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        blurSamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        blurSamplerInfo.maxLod = 1.0f;
        if (vkCreateSampler(gpu.device, &blurSamplerInfo, nullptr, &blurSampler) != VK_SUCCESS) {
            error = "vkCreateSampler(blur) failed";
            return false;
        }

        VkDescriptorSetAllocateInfo blurAllocate{};
        blurAllocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        blurAllocate.descriptorPool = ctx.tonemapDescriptorPool;
        blurAllocate.descriptorSetCount = 1;
        blurAllocate.pSetLayouts = &ctx.tonemapDescriptorLayout;
        if (vkAllocateDescriptorSets(gpu.device, &blurAllocate, &blurASet) != VK_SUCCESS ||
            vkAllocateDescriptorSets(gpu.device, &blurAllocate, &blurBSet) != VK_SUCCESS) {
            error = "vkAllocateDescriptorSets(blur) failed";
            return false;
        }
        VkDescriptorImageInfo blurADescriptor{};
        blurADescriptor.sampler = blurSampler;
        blurADescriptor.imageView = blurA.view;
        blurADescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkDescriptorImageInfo blurBDescriptor{};
        blurBDescriptor.sampler = blurSampler;
        blurBDescriptor.imageView = blurB.view;
        blurBDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet blurWrites[2]{};
        blurWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        blurWrites[0].dstSet = blurASet;
        blurWrites[0].dstBinding = 0;
        blurWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        blurWrites[0].descriptorCount = 1;
        blurWrites[0].pImageInfo = &blurADescriptor;
        blurWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        blurWrites[1].dstSet = blurBSet;
        blurWrites[1].dstBinding = 0;
        blurWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        blurWrites[1].descriptorCount = 1;
        blurWrites[1].pImageInfo = &blurBDescriptor;
        vkUpdateDescriptorSets(gpu.device, 2, blurWrites, 0, nullptr);

        // Backdrop composite target (full-res, same format as the final target).
        if (!createColorTarget(
                ctx.depthFormat, ctx.colorFormat, VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                mainWidth, mainHeight, ctx.mainRenderPass,
                uiTarget.view, uiTarget.framebuffer, uiTarget.image, uiTarget.memory,
                uiTarget.depthView, uiTarget.depthImage, uiTarget.depthMemory, error)) {
            return false;
        }
        if (vkAllocateDescriptorSets(gpu.device, &blurAllocate, &uiTargetSet) != VK_SUCCESS) {
            error = "vkAllocateDescriptorSets(uiTarget) failed";
            return false;
        }
        VkDescriptorImageInfo uiTargetDescriptor{};
        uiTargetDescriptor.sampler = blurSampler;
        uiTargetDescriptor.imageView = uiTarget.view;
        uiTargetDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet uiTargetWrite{};
        uiTargetWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        uiTargetWrite.dstSet = uiTargetSet;
        uiTargetWrite.dstBinding = 0;
        uiTargetWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        uiTargetWrite.descriptorCount = 1;
        uiTargetWrite.pImageInfo = &uiTargetDescriptor;
        vkUpdateDescriptorSets(gpu.device, 1, &uiTargetWrite, 0, nullptr);

        if (!RenderPassFactory::createLoad(gpu.device, ctx.colorFormat, ctx.depthFormat, uiAppendRenderPass)) {
            error = "vkCreateRenderPass(ui append) failed";
            return false;
        }
        VkImageView appendAttachments[2] = {uiTarget.view, uiTarget.depthView};
        VkFramebufferCreateInfo appendInfo{};
        appendInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        appendInfo.renderPass = uiAppendRenderPass;
        appendInfo.attachmentCount = 2;
        appendInfo.pAttachments = appendAttachments;
        appendInfo.width = mainWidth;
        appendInfo.height = mainHeight;
        appendInfo.layers = 1;
        if (vkCreateFramebuffer(gpu.device, &appendInfo, nullptr, &uiAppendFramebuffer) != VK_SUCCESS) {
            error = "vkCreateFramebuffer(ui append) failed";
            return false;
        }
    } else {
        // Non-HDR presets still draw UI with the uiPipeline, which shares the
        // tonemap descriptor layout. Allocate its set with a valid binding 0 (the
        // default texture) so the draws have a compatible set 0 bound.
        const auto& storeTextures = ctx.textures->resources();
        const auto defaultIt = storeTextures.find(ctx.textures->defaultHandle().value());
        const auto boundIt = storeTextures.find(ctx.textures->boundHandle().value());
        if (defaultIt == storeTextures.end() && boundIt == storeTextures.end()) {
            error = "No texture available for the UI descriptor set";
            return false;
        }
        const auto& uiTexture = boundIt != storeTextures.end() ? boundIt->second : defaultIt->second;
        VkDescriptorSetAllocateInfo allocateSetInfo{};
        allocateSetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocateSetInfo.descriptorPool = ctx.tonemapDescriptorPool;
        allocateSetInfo.descriptorSetCount = 1;
        allocateSetInfo.pSetLayouts = &ctx.tonemapDescriptorLayout;
        if (vkAllocateDescriptorSets(gpu.device, &allocateSetInfo, &tonemapDescriptorSet) != VK_SUCCESS) {
            error = "vkAllocateDescriptorSets(ui) failed";
            return false;
        }
        VkDescriptorImageInfo uiDescriptor{};
        uiDescriptor.sampler = uiTexture.sampler;
        uiDescriptor.imageView = uiTexture.view;
        uiDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet uiWrite{};
        uiWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        uiWrite.dstSet = tonemapDescriptorSet;
        uiWrite.dstBinding = 0;
        uiWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        uiWrite.descriptorCount = 1;
        uiWrite.pImageInfo = &uiDescriptor;
        vkUpdateDescriptorSets(gpu.device, 1, &uiWrite, 0, nullptr);
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
        const auto destroyBlurTarget = [device](BlurTarget& target) {
            if (target.framebuffer != VK_NULL_HANDLE) {
                vkDestroyFramebuffer(device, target.framebuffer, nullptr);
            }
            if (target.depthView != VK_NULL_HANDLE) {
                vkDestroyImageView(device, target.depthView, nullptr);
            }
            if (target.depthImage != VK_NULL_HANDLE) {
                vkDestroyImage(device, target.depthImage, nullptr);
            }
            if (target.depthMemory != VK_NULL_HANDLE) {
                vkFreeMemory(device, target.depthMemory, nullptr);
            }
            if (target.view != VK_NULL_HANDLE) {
                vkDestroyImageView(device, target.view, nullptr);
            }
            if (target.image != VK_NULL_HANDLE) {
                vkDestroyImage(device, target.image, nullptr);
            }
            if (target.memory != VK_NULL_HANDLE) {
                vkFreeMemory(device, target.memory, nullptr);
            }
            target = BlurTarget{};
        };
        destroyBlurTarget(blurA);
        destroyBlurTarget(blurB);
        if (uiAppendFramebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device, uiAppendFramebuffer, nullptr);
        }
        if (uiAppendRenderPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(device, uiAppendRenderPass, nullptr);
        }
        destroyBlurTarget(uiTarget);
        if (blurSampler != VK_NULL_HANDLE) {
            vkDestroySampler(device, blurSampler, nullptr);
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
#if defined(APORIA_EXPERIMENTAL)
        if (tonemapVolumetricBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, tonemapVolumetricBuffer, nullptr);
        }
        if (tonemapVolumetricMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, tonemapVolumetricMemory, nullptr);
        }
        if (tonemapDepthSampler != VK_NULL_HANDLE) {
            vkDestroySampler(device, tonemapDepthSampler, nullptr);
        }
#endif
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
    blurA = BlurTarget{};
    blurB = BlurTarget{};
    blurSampler = VK_NULL_HANDLE;
    blurASet = VK_NULL_HANDLE;
    blurBSet = VK_NULL_HANDLE;
    blurWidth = 0;
    blurHeight = 0;
    uiTarget = BlurTarget{};
    uiTargetSet = VK_NULL_HANDLE;
    uiAppendRenderPass = VK_NULL_HANDLE;
    uiAppendFramebuffer = VK_NULL_HANDLE;

#if defined(APORIA_EXPERIMENTAL)
    tonemapDepthSampler = VK_NULL_HANDLE;
    tonemapVolumetricBuffer = VK_NULL_HANDLE;
    tonemapVolumetricMemory = VK_NULL_HANDLE;
#endif
}

#if defined(APORIA_EXPERIMENTAL)
bool QualityTargets::writeTonemapVolumetric(const VolumetricUniforms& uniforms) noexcept {
    if (tonemapVolumetricMemory == VK_NULL_HANDLE || gpu.device == VK_NULL_HANDLE) {
        return false;
    }
    void* mapped = nullptr;
    if (vkMapMemory(gpu.device, tonemapVolumetricMemory, 0, sizeof(VolumetricUniforms), 0, &mapped) != VK_SUCCESS ||
        mapped == nullptr) {
        return false;
    }
    std::memcpy(mapped, &uniforms, sizeof(uniforms));
    vkUnmapMemory(gpu.device, tonemapVolumetricMemory);
    return true;
}
#endif

} // namespace Aporia::graphics_internal
