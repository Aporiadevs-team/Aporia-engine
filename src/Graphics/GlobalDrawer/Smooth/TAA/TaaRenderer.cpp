#include "TaaRenderer.hpp"

#include "Device/SceneManager/Color/ColorTarget.hpp"

#include <array>
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

void TaaRenderer::shutdown() noexcept {
    if (device_ != VK_NULL_HANDLE) {
        if (pipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(device_, pipeline_, nullptr);
        if (pipelineLayout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
        if (descriptorPool_ != VK_NULL_HANDLE) vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
        if (descriptorLayout_ != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device_, descriptorLayout_, nullptr);
        if (resolveFramebuffer_ != VK_NULL_HANDLE) vkDestroyFramebuffer(device_, resolveFramebuffer_, nullptr);
        if (resolveDepthView_ != VK_NULL_HANDLE) vkDestroyImageView(device_, resolveDepthView_, nullptr);
        if (resolveDepthImage_ != VK_NULL_HANDLE) vkDestroyImage(device_, resolveDepthImage_, nullptr);
        if (resolveDepthMemory_ != VK_NULL_HANDLE) vkFreeMemory(device_, resolveDepthMemory_, nullptr);
        if (resolveView_ != VK_NULL_HANDLE) vkDestroyImageView(device_, resolveView_, nullptr);
        if (resolveImage_ != VK_NULL_HANDLE) vkDestroyImage(device_, resolveImage_, nullptr);
        if (resolveMemory_ != VK_NULL_HANDLE) vkFreeMemory(device_, resolveMemory_, nullptr);
        if (historyView_ != VK_NULL_HANDLE) vkDestroyImageView(device_, historyView_, nullptr);
        if (historyImage_ != VK_NULL_HANDLE) vkDestroyImage(device_, historyImage_, nullptr);
        if (historyMemory_ != VK_NULL_HANDLE) vkFreeMemory(device_, historyMemory_, nullptr);
        if (historySampler_ != VK_NULL_HANDLE) vkDestroySampler(device_, historySampler_, nullptr);
        if (depthSampler_ != VK_NULL_HANDLE) vkDestroySampler(device_, depthSampler_, nullptr);
        for (std::uint32_t i = 0; i < kSlots; ++i) {
            if (uniformBuffers_[i] != VK_NULL_HANDLE) vkDestroyBuffer(device_, uniformBuffers_[i], nullptr);
            if (uniformMemories_[i] != VK_NULL_HANDLE) vkFreeMemory(device_, uniformMemories_[i], nullptr);
        }
    }
    pipeline_ = VK_NULL_HANDLE;
    pipelineLayout_ = VK_NULL_HANDLE;
    descriptorPool_ = VK_NULL_HANDLE;
    descriptorLayout_ = VK_NULL_HANDLE;
    for (std::uint32_t i = 0; i < kSlots; ++i) {
        descriptorSets_[i] = VK_NULL_HANDLE;
        uniformBuffers_[i] = VK_NULL_HANDLE;
        uniformMemories_[i] = VK_NULL_HANDLE;
    }
    resolveFramebuffer_ = VK_NULL_HANDLE;
    resolveDepthView_ = VK_NULL_HANDLE;
    resolveDepthImage_ = VK_NULL_HANDLE;
    resolveDepthMemory_ = VK_NULL_HANDLE;
    resolveView_ = VK_NULL_HANDLE;
    resolveImage_ = VK_NULL_HANDLE;
    resolveMemory_ = VK_NULL_HANDLE;
    historyView_ = VK_NULL_HANDLE;
    historyImage_ = VK_NULL_HANDLE;
    historyMemory_ = VK_NULL_HANDLE;
    historySampler_ = VK_NULL_HANDLE;
    depthSampler_ = VK_NULL_HANDLE;
    width_ = 0;
    height_ = 0;
    valid_ = false;
    historyValid_ = false;
}

bool TaaRenderer::initialize(const Context& context, std::uint32_t width, std::uint32_t height) {
    if (valid_ && width == width_ && height == height_) {
        return true;
    }
    shutdown();

    device_ = context.device;
    memoryProperties_ = context.memoryProperties;
    uploadCommandBuffer_ = context.commandBuffer;
    uploadQueue_ = context.queue;
    renderPass_ = context.renderPass;
    hdrFormat_ = context.hdrFormat;
    depthFormat_ = context.depthFormat;
    vertexShader_ = context.vertexShader;
    fragmentShader_ = context.fragmentShader;
    colorView_ = context.colorView;
    depthView_ = context.depthView;
    colorSampler_ = context.colorSampler;

    if (width == 0 || height == 0 || renderPass_ == VK_NULL_HANDLE || fragmentShader_ == VK_NULL_HANDLE ||
        vertexShader_ == VK_NULL_HANDLE) {
        return false;
    }

    if (!ColorTarget::create(device_, memoryProperties_, depthFormat_, hdrFormat_,
                             VK_IMAGE_USAGE_TRANSFER_SRC_BIT, width, height, renderPass_,
                             resolveView_, resolveFramebuffer_, resolveImage_, resolveMemory_,
                             resolveDepthView_, resolveDepthImage_, resolveDepthMemory_)) {
        return false;
    }
    VkFramebuffer ignoredFramebuffer = VK_NULL_HANDLE;
    VkImageView ignoredDepthView = VK_NULL_HANDLE;
    VkImage ignoredDepthImage = VK_NULL_HANDLE;
    VkDeviceMemory ignoredDepthMemory = VK_NULL_HANDLE;
    if (!ColorTarget::create(device_, memoryProperties_, depthFormat_, hdrFormat_,
                             VK_IMAGE_USAGE_TRANSFER_DST_BIT, width, height, VK_NULL_HANDLE,
                             historyView_, ignoredFramebuffer, historyImage_, historyMemory_,
                             ignoredDepthView, ignoredDepthImage, ignoredDepthMemory)) {
        return false;
    }

    VkSamplerCreateInfo colorSamplerInfo{};
    colorSamplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    colorSamplerInfo.magFilter = VK_FILTER_LINEAR;
    colorSamplerInfo.minFilter = VK_FILTER_LINEAR;
    colorSamplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    colorSamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    colorSamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    colorSamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    colorSamplerInfo.maxLod = 1.0f;
    if (vkCreateSampler(device_, &colorSamplerInfo, nullptr, &historySampler_) != VK_SUCCESS) {
        return false;
    }
    VkSamplerCreateInfo depthSamplerInfo{};
    depthSamplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    depthSamplerInfo.magFilter = VK_FILTER_NEAREST;
    depthSamplerInfo.minFilter = VK_FILTER_NEAREST;
    depthSamplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    depthSamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    depthSamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    depthSamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    depthSamplerInfo.maxLod = 0.0f;
    if (vkCreateSampler(device_, &depthSamplerInfo, nullptr, &depthSampler_) != VK_SUCCESS) {
        return false;
    }

    std::array<VkDescriptorSetLayoutBinding, 4> bindings{};
    for (std::uint32_t index = 0; index < 3; ++index) {
        bindings[index].binding = index;
        bindings[index].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[index].descriptorCount = 1;
        bindings[index].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<std::uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    if (vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &descriptorLayout_) != VK_SUCCESS) {
        return false;
    }
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorLayout_;
    if (vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr, &pipelineLayout_) != VK_SUCCESS) {
        return false;
    }

    VkDescriptorPoolSize poolSizes[2]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = 3 * kSlots;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[1].descriptorCount = kSlots;
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = kSlots;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    if (vkCreateDescriptorPool(device_, &poolInfo, nullptr, &descriptorPool_) != VK_SUCCESS) {
        return false;
    }
    VkDescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = descriptorPool_;
    allocateInfo.descriptorSetCount = kSlots;
    allocateInfo.pSetLayouts = &descriptorLayout_;
    if (vkAllocateDescriptorSets(device_, &allocateInfo, descriptorSets_) != VK_SUCCESS) {
        return false;
    }

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = 160; // 2 mat4 + 2 vec4
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device_, &bufferInfo, nullptr, &uniformBuffers_[0]) != VK_SUCCESS) {
        return false;
    }
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device_, uniformBuffers_[0], &requirements);
    std::uint32_t memoryType = 0;
    if (!findMemoryType(memoryProperties_, requirements.memoryTypeBits,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, memoryType)) {
        return false;
    }
    VkMemoryAllocateInfo uniformAllocate{};
    uniformAllocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    uniformAllocate.allocationSize = requirements.size;
    uniformAllocate.memoryTypeIndex = memoryType;
    if (vkAllocateMemory(device_, &uniformAllocate, nullptr, &uniformMemories_[0]) != VK_SUCCESS ||
        vkBindBufferMemory(device_, uniformBuffers_[0], uniformMemories_[0], 0) != VK_SUCCESS) {
        return false;
    }

    // Duplicate resources for the second slot.
    VkBufferCreateInfo dupBuf = bufferInfo;
    if (vkCreateBuffer(device_, &dupBuf, nullptr, &uniformBuffers_[1]) != VK_SUCCESS) {
        return false;
    }
    VkMemoryAllocateInfo dupAlloc = uniformAllocate;
    if (vkAllocateMemory(device_, &dupAlloc, nullptr, &uniformMemories_[1]) != VK_SUCCESS ||
        vkBindBufferMemory(device_, uniformBuffers_[1], uniformMemories_[1], 0) != VK_SUCCESS) {
        return false;
    }

    VkDescriptorImageInfo currentInfo{};
    currentInfo.sampler = colorSampler_;
    currentInfo.imageView = colorView_;
    currentInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkDescriptorImageInfo historyInfo{};
    historyInfo.sampler = historySampler_;
    historyInfo.imageView = historyView_;
    historyInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkDescriptorImageInfo depthInfo{};
    depthInfo.sampler = depthSampler_;
    depthInfo.imageView = depthView_;
    depthInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    std::array<VkWriteDescriptorSet, 4> writes{};
    for (std::uint32_t slot = 0; slot < kSlots; ++slot) {
        VkDescriptorBufferInfo uniformInfo{};
        uniformInfo.buffer = uniformBuffers_[slot];
        uniformInfo.offset = 0;
        uniformInfo.range = 160;
        for (std::uint32_t index = 0; index < 4; ++index) {
            writes[index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[index].dstSet = descriptorSets_[slot];
            writes[index].dstBinding = index;
            writes[index].descriptorCount = 1;
        }
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].pImageInfo = &currentInfo;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].pImageInfo = &historyInfo;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[2].pImageInfo = &depthInfo;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[3].pBufferInfo = &uniformInfo;
        vkUpdateDescriptorSets(device_, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertexShader_;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragmentShader_;
    stages[1].pName = "main";
    VkPipelineVertexInputStateCreateInfo emptyInput{};
    emptyInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rasterization{};
    rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization.polygonMode = VK_POLYGON_MODE_FILL;
    rasterization.cullMode = VK_CULL_MODE_NONE;
    rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blendAttachment;
    const VkDynamicState dynamicStates[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &emptyInput;
    pipelineInfo.pInputAssemblyState = &assembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterization;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout_;
    pipelineInfo.renderPass = renderPass_;
    pipelineInfo.subpass = 0;
    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline_) != VK_SUCCESS) {
        return false;
    }

    width_ = width;
    height_ = height;
    valid_ = true;
    historyValid_ = false;
    return true;
}

bool TaaRenderer::uploadUniforms(std::uint32_t frameSlot, const float prevViewProj[16], const float invViewProj[16], float blend) {
    if (frameSlot >= kSlots || uniformMemories_[frameSlot] == VK_NULL_HANDLE) {
        return false;
    }
    void* mapped = nullptr;
    if (vkMapMemory(device_, uniformMemories_[frameSlot], 0, 160, 0, &mapped) != VK_SUCCESS || mapped == nullptr) {
        return false;
    }
    struct TaaUniforms {
        float prevViewProj[16];
        float invViewProj[16];
        float params[4];
        float resolution[4];
    } uniforms{};
    std::memcpy(uniforms.prevViewProj, prevViewProj, sizeof(uniforms.prevViewProj));
    std::memcpy(uniforms.invViewProj, invViewProj, sizeof(uniforms.invViewProj));
    uniforms.params[0] = blend;
    uniforms.params[1] = historyValid_ ? 1.0f : 0.0f;
    uniforms.params[2] = width_ != 0 ? 1.0f / static_cast<float>(width_) : 0.0f;
    uniforms.params[3] = height_ != 0 ? 1.0f / static_cast<float>(height_) : 0.0f;
    uniforms.resolution[0] = static_cast<float>(width_);
    uniforms.resolution[1] = static_cast<float>(height_);
    std::memcpy(mapped, &uniforms, sizeof(uniforms));
    vkUnmapMemory(device_, uniformMemories_[frameSlot]);
    return true;
}

void TaaRenderer::record(
        VkCommandBuffer commandBuffer,
        VkImage depthImage,
        std::uint32_t frameSlot,
        const float prevViewProj[16],
        const float invViewProj[16],
        float blend) noexcept {
    if (!valid_ || commandBuffer == VK_NULL_HANDLE) {
        return;
    }
    if (!uploadUniforms(frameSlot, prevViewProj, invViewProj, blend)) {
        return;
    }

    const VkImageLayout historyOldLayout = historyValid_
        ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
    std::array<VkImageMemoryBarrier, 2> toShaderRead{};
    VkImageMemoryBarrier& depthBarrier = toShaderRead[0];
    depthBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    depthBarrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    depthBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    depthBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    depthBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    depthBarrier.image = depthImage;
    depthBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    depthBarrier.subresourceRange.levelCount = 1;
    depthBarrier.subresourceRange.layerCount = 1;
    depthBarrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    depthBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    VkImageMemoryBarrier& historyBarrier = toShaderRead[1];
    historyBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    historyBarrier.oldLayout = historyOldLayout;
    historyBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    historyBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    historyBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    historyBarrier.image = historyImage_;
    historyBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    historyBarrier.subresourceRange.levelCount = 1;
    historyBarrier.subresourceRange.layerCount = 1;
    historyBarrier.srcAccessMask = historyValid_ ? VK_ACCESS_SHADER_READ_BIT : 0;
    historyBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(commandBuffer,
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr,
        static_cast<std::uint32_t>(toShaderRead.size()), toShaderRead.data());

    std::array<VkClearValue, 2> taaClear{};
    taaClear[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    taaClear[1].depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo taaBegin{};
    taaBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    taaBegin.renderPass = renderPass_;
    taaBegin.framebuffer = resolveFramebuffer_;
    taaBegin.renderArea.extent = {width_, height_};
    taaBegin.clearValueCount = static_cast<std::uint32_t>(taaClear.size());
    taaBegin.pClearValues = taaClear.data();
    vkCmdBeginRenderPass(commandBuffer, &taaBegin, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport taaViewport{};
    taaViewport.width = static_cast<float>(width_);
    taaViewport.height = static_cast<float>(height_);
    taaViewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &taaViewport);
    VkRect2D taaScissor{};
    taaScissor.extent = {width_, height_};
    vkCmdSetScissor(commandBuffer, 0, 1, &taaScissor);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipelineLayout_, 0, 1, &descriptorSets_[frameSlot], 0, nullptr);
    vkCmdDraw(commandBuffer, 4, 1, 0, 0);
    vkCmdEndRenderPass(commandBuffer);

    std::array<VkImageMemoryBarrier, 2> toTransfer{};
    VkImageMemoryBarrier& resolveSource = toTransfer[0];
    resolveSource.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    resolveSource.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    resolveSource.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    resolveSource.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    resolveSource.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    resolveSource.image = resolveImage_;
    resolveSource.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    resolveSource.subresourceRange.levelCount = 1;
    resolveSource.subresourceRange.layerCount = 1;
    resolveSource.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    resolveSource.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    VkImageMemoryBarrier& historyDestination = toTransfer[1];
    historyDestination.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    historyDestination.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    historyDestination.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    historyDestination.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    historyDestination.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    historyDestination.image = historyImage_;
    historyDestination.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    historyDestination.subresourceRange.levelCount = 1;
    historyDestination.subresourceRange.layerCount = 1;
    historyDestination.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    historyDestination.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
        static_cast<std::uint32_t>(toTransfer.size()), toTransfer.data());
    VkImageCopy copyRegion{};
    copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.srcSubresource.mipLevel = 0;
    copyRegion.srcSubresource.baseArrayLayer = 0;
    copyRegion.srcSubresource.layerCount = 1;
    copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.dstSubresource.mipLevel = 0;
    copyRegion.dstSubresource.baseArrayLayer = 0;
    copyRegion.dstSubresource.layerCount = 1;
    copyRegion.extent = {width_, height_, 1};
    vkCmdCopyImage(commandBuffer, resolveImage_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   historyImage_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
    std::array<VkImageMemoryBarrier, 2> toRead{};
    VkImageMemoryBarrier& resolveBack = toRead[0];
    resolveBack.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    resolveBack.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    resolveBack.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    resolveBack.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    resolveBack.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    resolveBack.image = resolveImage_;
    resolveBack.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    resolveBack.subresourceRange.levelCount = 1;
    resolveBack.subresourceRange.layerCount = 1;
    resolveBack.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    resolveBack.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    VkImageMemoryBarrier& historyBack = toRead[1];
    historyBack.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    historyBack.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    historyBack.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    historyBack.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    historyBack.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    historyBack.image = historyImage_;
    historyBack.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    historyBack.subresourceRange.levelCount = 1;
    historyBack.subresourceRange.layerCount = 1;
    historyBack.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    historyBack.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr,
        static_cast<std::uint32_t>(toRead.size()), toRead.data());
    historyValid_ = true;
}

} // namespace Aporia::graphics_internal
