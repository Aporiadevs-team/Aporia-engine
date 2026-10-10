#include "VolumetricRenderer.hpp"

#if defined(APORIA_EXPERIMENTAL)

#include "Device/MemoryManager/MemoryManager.hpp"
#include "Graphics/GlobalDrawer/Support/Support.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

namespace Aporia::graphics_internal {

namespace {

bool readSpirv(const std::string& path, std::vector<char>& out) noexcept {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    const std::streamsize size = file.tellg();
    if (size <= 0) {
        return false;
    }
    out.resize(static_cast<std::size_t>(size));
    file.seekg(0);
    file.read(out.data(), size);
    return static_cast<std::streamsize>(out.size()) == size;
}

void transitionImage(
    VkCommandBuffer commandBuffer,
    VkImage image,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    VkPipelineStageFlags srcStage,
    VkPipelineStageFlags dstStage,
    VkAccessFlags srcAccess,
    VkAccessFlags dstAccess) noexcept {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;
    vkCmdPipelineBarrier(commandBuffer, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

} // namespace

bool VolumetricRenderer::createImage(std::string& error) noexcept {
    VkFormatProperties storageProps{};
    vkGetPhysicalDeviceFormatProperties(physicalDevice_, VK_FORMAT_R16G16B16A16_SFLOAT, &storageProps);
    storageSupported_ =
        (storageProps.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) != 0;

    const bool full = storageSupported_;
    const VkExtent3D extent = full ? VkExtent3D{kGridX, kGridY, kGridZ} : VkExtent3D{1, 1, 1};

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_3D;
    imageInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    imageInfo.extent = extent;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | (full ? VK_IMAGE_USAGE_STORAGE_BIT : 0u);
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device_, &imageInfo, nullptr, &image_) != VK_SUCCESS) {
        error = "vkCreateImage(froxel) failed";
        return false;
    }

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device_, image_, &requirements);
    std::uint32_t memoryType = 0;
    if (!MemoryManager::findMemoryType(memoryProperties_, requirements.memoryTypeBits,
                                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memoryType)) {
        error = "No device-local memory for the froxel grid";
        return false;
    }
    VkMemoryAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = requirements.size;
    allocateInfo.memoryTypeIndex = memoryType;
    if (vkAllocateMemory(device_, &allocateInfo, nullptr, &imageMemory_) != VK_SUCCESS ||
        vkBindImageMemory(device_, image_, imageMemory_, 0) != VK_SUCCESS) {
        error = "vkAllocateMemory(froxel) failed";
        return false;
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_3D;
    viewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device_, &viewInfo, nullptr, &view_) != VK_SUCCESS) {
        error = "vkCreateImageView(froxel) failed";
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
    samplerInfo.maxLod = 1.0f;
    if (vkCreateSampler(device_, &samplerInfo, nullptr, &sampler_) != VK_SUCCESS) {
        error = "vkCreateSampler(froxel) failed";
        return false;
    }

    // One-shot transition to the layout the passes expect: GENERAL so compute
    // can write and fragment can sample it; a plain sampled layout when compute
    // is unavailable.
    if (utilityCommandBuffer_ == VK_NULL_HANDLE || queue_ == VK_NULL_HANDLE) {
        return true; // headless bootstrap without a utility buffer; skip barrier
    }
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(utilityCommandBuffer_, &beginInfo) == VK_SUCCESS) {
        transitionImage(utilityCommandBuffer_, image_, VK_IMAGE_LAYOUT_UNDEFINED,
                        full ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        0, VK_ACCESS_SHADER_WRITE_BIT);
        vkEndCommandBuffer(utilityCommandBuffer_);
        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &utilityCommandBuffer_;
        vkQueueSubmit(queue_, 1, &submit, VK_NULL_HANDLE);
        vkQueueWaitIdle(queue_);
    }
    return true;
}

bool VolumetricRenderer::createParams() noexcept {
    const VkDeviceSize size = sizeof(InjectParams);
    for (std::uint32_t slot = 0; slot < kSlots; ++slot) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device_, &bufferInfo, nullptr, &paramBuffers_[slot]) != VK_SUCCESS) {
            return false;
        }
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device_, paramBuffers_[slot], &requirements);
        std::uint32_t memoryType = 0;
        if (!MemoryManager::findMemoryType(memoryProperties_, requirements.memoryTypeBits,
                                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                           memoryType)) {
            return false;
        }
        VkMemoryAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocateInfo.allocationSize = requirements.size;
        allocateInfo.memoryTypeIndex = memoryType;
        if (vkAllocateMemory(device_, &allocateInfo, nullptr, &paramMemories_[slot]) != VK_SUCCESS ||
            vkBindBufferMemory(device_, paramBuffers_[slot], paramMemories_[slot], 0) != VK_SUCCESS) {
            return false;
        }
        if (vkMapMemory(device_, paramMemories_[slot], 0, size, 0, &paramMapped_[slot]) != VK_SUCCESS) {
            paramMapped_[slot] = nullptr;
            return false;
        }
    }
    return true;
}

bool VolumetricRenderer::writeDescriptors(VkImageView shadowView, VkSampler shadowSampler) noexcept {
    if (device_ == VK_NULL_HANDLE || view_ == VK_NULL_HANDLE || !storageSupported_) {
        return true;
    }
    for (std::uint32_t slot = 0; slot < kSlots; ++slot) {
        VkDescriptorImageInfo froxelInfo{};
        froxelInfo.sampler = sampler_;
        froxelInfo.imageView = view_;
        froxelInfo.imageLayout = storageSupported_ ? VK_IMAGE_LAYOUT_GENERAL
                                                   : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkDescriptorImageInfo shadowInfo{};
        shadowInfo.sampler = shadowSampler;
        shadowInfo.imageView = shadowView;
        shadowInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = paramBuffers_[slot];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(InjectParams);

        std::array<VkWriteDescriptorSet, 3> writes{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = sets_[slot];
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[0].descriptorCount = 1;
        writes[0].pImageInfo = &froxelInfo;
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = sets_[slot];
        writes[1].dstBinding = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &shadowInfo;
        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = sets_[slot];
        writes[2].dstBinding = 2;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[2].descriptorCount = 1;
        writes[2].pBufferInfo = &bufferInfo;
        vkUpdateDescriptorSets(device_, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
    return true;
}

bool VolumetricRenderer::createPipeline(const std::string& shaderDirectory, std::string& error) noexcept {
    if (!storageSupported_) {
        return true; // no compute path; grid stays empty
    }

    VkDescriptorSetLayoutBinding bindings[3]{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 3;
    layoutInfo.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &setLayout_) != VK_SUCCESS) {
        error = "vkCreateDescriptorSetLayout(volumetric) failed";
        return false;
    }
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &setLayout_;
    if (vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr, &pipelineLayout_) != VK_SUCCESS) {
        error = "vkCreatePipelineLayout(volumetric) failed";
        return false;
    }

    std::vector<char> code;
    if (!readSpirv(shaderDirectory + "/volumetric_inject.comp.spv", code)) {
        error = "volumetric_inject.comp.spv not found";
        return false;
    }
    VkShaderModuleCreateInfo moduleInfo{};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleInfo.codeSize = code.size();
    moduleInfo.pCode = reinterpret_cast<const std::uint32_t*>(code.data());
    if (vkCreateShaderModule(device_, &moduleInfo, nullptr, &shaderModule_) != VK_SUCCESS) {
        error = "vkCreateShaderModule(volumetric_inject) failed";
        return false;
    }

    std::array<VkDescriptorPoolSize, 3> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[0].descriptorCount = kSlots;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = kSlots;
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[2].descriptorCount = kSlots;
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = kSlots;
    poolInfo.poolSizeCount = static_cast<std::uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    if (vkCreateDescriptorPool(device_, &poolInfo, nullptr, &descriptorPool_) != VK_SUCCESS) {
        error = "vkCreateDescriptorPool(volumetric) failed";
        return false;
    }
    const VkDescriptorSetLayout layouts[kSlots] = {setLayout_, setLayout_};
    VkDescriptorSetAllocateInfo allocateSets{};
    allocateSets.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateSets.descriptorPool = descriptorPool_;
    allocateSets.descriptorSetCount = kSlots;
    allocateSets.pSetLayouts = layouts;
    if (vkAllocateDescriptorSets(device_, &allocateSets, sets_) != VK_SUCCESS) {
        error = "vkAllocateDescriptorSets(volumetric) failed";
        return false;
    }

    if (!createParams()) {
        error = "Volumetric parameter buffers failed";
        return false;
    }

    VkComputePipelineCreateInfo computeInfo{};
    computeInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    computeInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    computeInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    computeInfo.stage.module = shaderModule_;
    computeInfo.stage.pName = "main";
    computeInfo.layout = pipelineLayout_;
    if (vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &computeInfo, nullptr, &pipeline_) != VK_SUCCESS) {
        error = "vkCreateComputePipelines(volumetric) failed";
        return false;
    }
    return true;
}

bool VolumetricRenderer::initialize(
    const Device& device,
    const std::string& shaderDirectory,
    VkImageView shadowView,
    VkSampler shadowSampler,
    std::string& error) noexcept {
    destroy();
    device_ = device.device;
    physicalDevice_ = device.physicalDevice;
    memoryProperties_ = device.memoryProperties;
    utilityCommandBuffer_ = device.commandBuffer;
    queue_ = device.queue;
    if (device_ == VK_NULL_HANDLE) {
        error = "Volumetric renderer needs a device";
        return false;
    }
    if (!createImage(error)) {
        destroy();
        return false;
    }
    if (!createPipeline(shaderDirectory, error)) {
        destroy();
        return false;
    }
    if (storageSupported_ && !writeDescriptors(shadowView, shadowSampler)) {
        destroy();
        return false;
    }
    valid_ = storageSupported_;
    if (!storageSupported_) {
        engineLog("[warn] volumetric: RGBA16F storage images unsupported; fog disabled");
    }
    return true;
}

void VolumetricRenderer::bindShadow(VkImageView shadowView, VkSampler shadowSampler) noexcept {
    if (device_ == VK_NULL_HANDLE) {
        return;
    }
    writeDescriptors(shadowView, shadowSampler);
}

bool VolumetricRenderer::writeParams(std::uint32_t slot, const InjectParams& params) noexcept {
    if (!valid_ || slot >= kSlots || paramMapped_[slot] == nullptr) {
        return false;
    }
    std::memcpy(paramMapped_[slot], &params, sizeof(params));
    return true;
}

void VolumetricRenderer::record(VkCommandBuffer commandBuffer, std::uint32_t slot) noexcept {
    if (!valid_ || pipeline_ == VK_NULL_HANDLE || commandBuffer == VK_NULL_HANDLE || slot >= kSlots) {
        return;
    }

    // Prior frame's fragment sampling / compute write -> this dispatch's write.
    transitionImage(commandBuffer, image_, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                    VK_ACCESS_SHADER_WRITE_BIT);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout_,
                            0, 1, &sets_[slot], 0, nullptr);
    const std::uint32_t groupsX = (kGridX + 7) / 8;
    const std::uint32_t groupsY = (kGridY + 7) / 8;
    const std::uint32_t groupsZ = kGridZ;
    vkCmdDispatch(commandBuffer, groupsX, groupsY, groupsZ);

    // Compute write -> tonemap fragment sampling.
    transitionImage(commandBuffer, image_, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
}

void VolumetricRenderer::destroy() noexcept {
    if (device_ != VK_NULL_HANDLE) {
        for (std::uint32_t slot = 0; slot < kSlots; ++slot) {
            if (paramMapped_[slot] != nullptr) {
                vkUnmapMemory(device_, paramMemories_[slot]);
                paramMapped_[slot] = nullptr;
            }
            if (paramBuffers_[slot] != VK_NULL_HANDLE) {
                vkDestroyBuffer(device_, paramBuffers_[slot], nullptr);
            }
            if (paramMemories_[slot] != VK_NULL_HANDLE) {
                vkFreeMemory(device_, paramMemories_[slot], nullptr);
            }
            paramBuffers_[slot] = VK_NULL_HANDLE;
            paramMemories_[slot] = VK_NULL_HANDLE;
            sets_[slot] = VK_NULL_HANDLE;
        }
        if (descriptorPool_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
        }
        if (pipeline_ != VK_NULL_HANDLE) {
            vkDestroyPipeline(device_, pipeline_, nullptr);
        }
        if (shaderModule_ != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device_, shaderModule_, nullptr);
        }
        if (pipelineLayout_ != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
        }
        if (setLayout_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device_, setLayout_, nullptr);
        }
        if (sampler_ != VK_NULL_HANDLE) {
            vkDestroySampler(device_, sampler_, nullptr);
        }
        if (view_ != VK_NULL_HANDLE) {
            vkDestroyImageView(device_, view_, nullptr);
        }
        if (image_ != VK_NULL_HANDLE) {
            vkDestroyImage(device_, image_, nullptr);
        }
        if (imageMemory_ != VK_NULL_HANDLE) {
            vkFreeMemory(device_, imageMemory_, nullptr);
        }
    }
    device_ = VK_NULL_HANDLE;
    physicalDevice_ = VK_NULL_HANDLE;
    utilityCommandBuffer_ = VK_NULL_HANDLE;
    queue_ = VK_NULL_HANDLE;
    image_ = VK_NULL_HANDLE;
    imageMemory_ = VK_NULL_HANDLE;
    view_ = VK_NULL_HANDLE;
    sampler_ = VK_NULL_HANDLE;
    setLayout_ = VK_NULL_HANDLE;
    pipelineLayout_ = VK_NULL_HANDLE;
    pipeline_ = VK_NULL_HANDLE;
    shaderModule_ = VK_NULL_HANDLE;
    descriptorPool_ = VK_NULL_HANDLE;
    storageSupported_ = true;
    valid_ = false;
}

} // namespace Aporia::graphics_internal

#endif // APORIA_EXPERIMENTAL
