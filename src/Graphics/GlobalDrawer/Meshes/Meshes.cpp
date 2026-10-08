#include "Graphics/GlobalDrawer/GraphicsImpl.hpp"

#include <array>
#include <cstring>
#include <string>

namespace Aporia {

// Rebuilds the mesh pipelines with the current shadow-tap / reflection
// specialization. Quality presets change these, so the pipelines must be
// recompiled on setQuality (otherwise High never actually kicks in).
bool Graphics::Impl::rebuildMeshPipelines() {
    if (device == VK_NULL_HANDLE || pipelines.meshPipelineLayout == VK_NULL_HANDLE ||
        pipelines.meshVertexShader == VK_NULL_HANDLE || pipelines.meshFragmentShader == VK_NULL_HANDLE) {
        return false;
    }
    vkDeviceWaitIdle(device);
    if (pipelines.meshPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, pipelines.meshPipeline, nullptr);
        pipelines.meshPipeline = VK_NULL_HANDLE;
    }
    if (pipelines.hdrMeshPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, pipelines.hdrMeshPipeline, nullptr);
        pipelines.hdrMeshPipeline = VK_NULL_HANDLE;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = pipelines.meshVertexShader;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = pipelines.meshFragmentShader;
    stages[1].pName = "main";

    VkVertexInputBindingDescription vertexBinding{};
    vertexBinding.binding = 0;
    vertexBinding.stride = static_cast<std::uint32_t>(sizeof(MeshVertex));
    vertexBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    std::array<VkVertexInputAttributeDescription, 3> attributes{};
    attributes[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MeshVertex, position)};
    attributes[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MeshVertex, normal)};
    attributes[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(MeshVertex, uv)};
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &vertexBinding;
    vertexInput.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
    vertexInput.pVertexAttributeDescriptions = attributes.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
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
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.minDepthBounds = 0.0f;
    depthStencil.maxDepthBounds = 1.0f;
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

    VkSpecializationMapEntry specEntries[2]{};
    specEntries[0].constantID = 0;
    specEntries[0].offset = 0;
    specEntries[0].size = sizeof(std::int32_t);
    specEntries[1].constantID = 1;
    specEntries[1].offset = sizeof(std::int32_t);
    specEntries[1].size = sizeof(float);
    float specReflection = reflectionsEnabled ? 1.0f : 0.0f;
    std::int32_t specTaps = shadowTaps;
    std::int32_t specData[2] = {specTaps, 0};
    std::memcpy(specData + 1, &specReflection, sizeof(float));
    VkSpecializationInfo specInfo{};
    specInfo.mapEntryCount = 2;
    specInfo.pMapEntries = specEntries;
    specInfo.dataSize = sizeof(specData);
    specInfo.pData = specData;
    stages[1].pSpecializationInfo = &specInfo;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterization;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelines.meshPipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipelines.meshPipeline) != VK_SUCCESS) {
        setError("vkCreateGraphicsPipelines(mesh, preset) failed");
        return false;
    }
    if (hdrRenderPass != VK_NULL_HANDLE) {
        pipelineInfo.renderPass = hdrRenderPass;
        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipelines.hdrMeshPipeline) != VK_SUCCESS) {
            setError("vkCreateGraphicsPipelines(hdr mesh, preset) failed");
            return false;
        }
    }
    return true;
}

MeshHandle Graphics::createMesh(const MeshData& data) {
    if (!impl_) {
        return invalidMeshHandle;
    }
    if (!initialized()) {
        impl_->setError("Graphics is not initialized");
        return invalidMeshHandle;
    }
    std::string error;
    const MeshHandle handle = impl_->meshStore.create(data, error);
    if (!handle.valid()) {
        impl_->setError(error);
    }
    return handle;
}

void Graphics::destroyMesh(MeshHandle handle) noexcept {
    if (!impl_ || !handle.valid()) {
        return;
    }
    if (!initialized()) {
        impl_->lastError = "Graphics is not initialized";
        return;
    }
    impl_->meshStore.destroy(handle);
}

std::uint64_t Graphics::meshVertexCount(MeshHandle handle) const noexcept {
    if (!impl_) {
        return 0;
    }
    return impl_->meshStore.vertexCount(handle);
}

} // namespace Aporia
