#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "SkyRenderer.hpp"

#include "Device/MemoryManager/MemoryManager.hpp"
#include "Graphics/GlobalDrawer/Support/Support.hpp"

#include <cstring>

namespace Aporia::graphics_internal {

namespace {

bool loadModule(VkDevice device, const std::string& path, VkShaderModule& out) {
    bool loaded = false;
    const std::vector<std::uint32_t> code = readSpirv(path, loaded);
    if (!loaded) {
        return false;
    }
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = code.size() * sizeof(std::uint32_t);
    info.pCode = code.data();
    return vkCreateShaderModule(device, &info, nullptr, &out) == VK_SUCCESS;
}

} // namespace

bool SkyRenderer::initialize(
    VkDevice device,
    const VkPhysicalDeviceMemoryProperties& memoryProperties,
    VkRenderPass renderPass,
    VkRenderPass hdrRenderPass,
    const std::string& shaderDirectory,
    std::string& error) {
    device_ = device;
    if (!loadModule(device, shaderDirectory + "/sky.vert.spv", vertexShader_) ||
        !loadModule(device, shaderDirectory + "/sky.frag.spv", fragmentShader_)) {
        error = "Failed to load compiled sky shaders";
        return false;
    }

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo setInfo{};
    setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    setInfo.bindingCount = 1;
    setInfo.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(device, &setInfo, nullptr, &setLayout_) != VK_SUCCESS) {
        error = "vkCreateDescriptorSetLayout(sky) failed";
        return false;
    }

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &setLayout_;
    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &layout_) != VK_SUCCESS) {
        error = "vkCreatePipelineLayout(sky) failed";
        return false;
    }

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = kSlots;
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = kSlots;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool_) != VK_SUCCESS) {
        error = "vkCreateDescriptorPool(sky) failed";
        return false;
    }

    for (std::uint32_t slot = 0; slot < kSlots; ++slot) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = sizeof(Uniforms);
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffers_[slot]) != VK_SUCCESS) {
            error = "vkCreateBuffer(sky) failed";
            return false;
        }
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device, buffers_[slot], &requirements);
        std::uint32_t memoryType = 0;
        if (!MemoryManager::findMemoryType(
                memoryProperties, requirements.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, memoryType)) {
            error = "No host-visible memory for the sky uniform buffer";
            return false;
        }
        VkMemoryAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocateInfo.allocationSize = requirements.size;
        allocateInfo.memoryTypeIndex = memoryType;
        if (vkAllocateMemory(device, &allocateInfo, nullptr, &memory_[slot]) != VK_SUCCESS ||
            vkBindBufferMemory(device, buffers_[slot], memory_[slot], 0) != VK_SUCCESS) {
            error = "vkAllocateMemory(sky) failed";
            return false;
        }

        VkDescriptorSetAllocateInfo setAllocate{};
        setAllocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        setAllocate.descriptorPool = pool_;
        setAllocate.descriptorSetCount = 1;
        setAllocate.pSetLayouts = &setLayout_;
        if (vkAllocateDescriptorSets(device, &setAllocate, &sets_[slot]) != VK_SUCCESS) {
            error = "vkAllocateDescriptorSets(sky) failed";
            return false;
        }
        VkDescriptorBufferInfo bufferDescriptor{};
        bufferDescriptor.buffer = buffers_[slot];
        bufferDescriptor.offset = 0;
        bufferDescriptor.range = sizeof(Uniforms);
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = sets_[slot];
        write.dstBinding = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.descriptorCount = 1;
        write.pBufferInfo = &bufferDescriptor;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }

    if (!createPipeline(device, renderPass, vertexShader_, fragmentShader_, pipeline_, error)) {
        return false;
    }
    if (hdrRenderPass != VK_NULL_HANDLE &&
        !createPipeline(device, hdrRenderPass, vertexShader_, fragmentShader_, hdrPipeline_, error)) {
        return false;
    }
    return true;
}

bool SkyRenderer::createPipeline(VkDevice device, VkRenderPass renderPass, VkShaderModule vertex,
                                 VkShaderModule fragment, VkPipeline& out, std::string& error) {
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertex;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragment;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // The sky is the background: no depth test, no depth write, drawn first.
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorAttachment{};
    colorAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &colorAttachment;

    const VkDynamicState dynamicStates[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &assembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &raster;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = layout_;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &out) != VK_SUCCESS) {
        error = "vkCreateGraphicsPipelines(sky) failed";
        return false;
    }
    return true;
}

bool SkyRenderer::writeUniforms(VkDevice device, std::uint32_t slot, const Uniforms& uniforms) noexcept {
    if (slot >= kSlots || memory_[slot] == VK_NULL_HANDLE) {
        return false;
    }
    void* mapped = nullptr;
    if (vkMapMemory(device, memory_[slot], 0, sizeof(Uniforms), 0, &mapped) != VK_SUCCESS || mapped == nullptr) {
        return false;
    }
    std::memcpy(mapped, &uniforms, sizeof(uniforms));
    vkUnmapMemory(device, memory_[slot]);
    return true;
}

void SkyRenderer::record(VkCommandBuffer cmd, bool hdr, std::uint32_t slot) const noexcept {
    if (cmd == VK_NULL_HANDLE || slot >= kSlots || sets_[slot] == VK_NULL_HANDLE) {
        return;
    }
    const VkPipeline pipeline = hdr ? hdrPipeline_ : pipeline_;
    if (pipeline == VK_NULL_HANDLE) {
        return;
    }
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &sets_[slot], 0, nullptr);
    vkCmdDraw(cmd, 4, 1, 0, 0);
}

void SkyRenderer::shutdown(VkDevice device) noexcept {
    if (device == VK_NULL_HANDLE) {
        return;
    }
    if (pipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(device, pipeline_, nullptr);
    if (hdrPipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(device, hdrPipeline_, nullptr);
    if (pool_ != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, pool_, nullptr);
    if (layout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, layout_, nullptr);
    if (setLayout_ != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, setLayout_, nullptr);
    if (vertexShader_ != VK_NULL_HANDLE) vkDestroyShaderModule(device, vertexShader_, nullptr);
    if (fragmentShader_ != VK_NULL_HANDLE) vkDestroyShaderModule(device, fragmentShader_, nullptr);
    for (std::uint32_t slot = 0; slot < kSlots; ++slot) {
        if (buffers_[slot] != VK_NULL_HANDLE) vkDestroyBuffer(device, buffers_[slot], nullptr);
        if (memory_[slot] != VK_NULL_HANDLE) vkFreeMemory(device, memory_[slot], nullptr);
    }
    device_ = VK_NULL_HANDLE;
    pipeline_ = VK_NULL_HANDLE;
    hdrPipeline_ = VK_NULL_HANDLE;
    pool_ = VK_NULL_HANDLE;
    layout_ = VK_NULL_HANDLE;
    setLayout_ = VK_NULL_HANDLE;
    vertexShader_ = VK_NULL_HANDLE;
    fragmentShader_ = VK_NULL_HANDLE;
    for (std::uint32_t slot = 0; slot < kSlots; ++slot) {
        buffers_[slot] = VK_NULL_HANDLE;
        memory_[slot] = VK_NULL_HANDLE;
        sets_[slot] = VK_NULL_HANDLE;
    }
}

} // namespace Aporia::graphics_internal
