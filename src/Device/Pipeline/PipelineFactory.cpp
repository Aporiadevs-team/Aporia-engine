#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "PipelineFactory.hpp"

#include "Graphics/GlobalDrawer/Support/Support.hpp"
#include "Graphics/GlobalDrawer/Meshes/MeshRenderer.hpp"
#include "Graphics/GlobalDrawer/Rectangulars/RectRenderer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace Aporia::graphics_internal {

namespace {

bool loadShaderModule(VkDevice device, const std::string& path, const char* label, VkShaderModule& out, std::string& error) {
    bool loaded = false;
    const std::vector<std::uint32_t> code = readSpirv(path, loaded);
    if (!loaded) {
        error = "Failed to load compiled ";
        error += label;
        error += " shader";
        return false;
    }
    VkShaderModuleCreateInfo moduleInfo{};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleInfo.codeSize = code.size() * sizeof(std::uint32_t);
    moduleInfo.pCode = code.data();
    if (vkCreateShaderModule(device, &moduleInfo, nullptr, &out) != VK_SUCCESS) {
        error = "vkCreateShaderModule(";
        error += label;
        error += ") failed";
        return false;
    }
    return true;
}

} // namespace

bool createPipelines(const PipelineContext& context, PipelineSet& out, std::string& error) {
    const VkDevice device = context.device;
    const std::string& shaderDirectory = context.shaderDirectory;

    if (!loadShaderModule(device, shaderDirectory + "/triangle.vert.spv", "vertex", out.vertexShader, error) ||
        !loadShaderModule(device, shaderDirectory + "/triangle.frag.spv", "fragment", out.fragmentShader, error)) {
        return false;
    }

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &context.triangleLayout;
    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &out.pipelineLayout) != VK_SUCCESS) {
        error = "vkCreatePipelineLayout failed";
        return false;
    }

    VkPipelineShaderStageCreateInfo shaderStages[2]{};
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = out.vertexShader;
    shaderStages[0].pName = "main";
    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = out.fragmentShader;
    shaderStages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

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

    VkPipelineColorBlendAttachmentState colorAttachment{};
    colorAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &colorAttachment;

    const VkDynamicState dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkPipelineDepthStencilStateCreateInfo triangleDepthStencil{};
    triangleDepthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    triangleDepthStencil.depthTestEnable = VK_TRUE;
    triangleDepthStencil.depthWriteEnable = VK_TRUE;
    triangleDepthStencil.depthCompareOp = VK_COMPARE_OP_ALWAYS;
    triangleDepthStencil.minDepthBounds = 0.0f;
    triangleDepthStencil.maxDepthBounds = 1.0f;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterization;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &triangleDepthStencil;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = out.pipelineLayout;
    pipelineInfo.renderPass = context.renderPass;
    pipelineInfo.subpass = 0;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &out.pipeline) != VK_SUCCESS) {
        error = "vkCreateGraphicsPipelines failed";
        return false;
    }

    // Mesh pipeline: vertex buffers + push-constant camera.
    if (!loadShaderModule(device, shaderDirectory + "/mesh.vert.spv", "mesh vertex", out.meshVertexShader, error) ||
        !loadShaderModule(device, shaderDirectory + "/mesh.frag.spv", "mesh fragment", out.meshFragmentShader, error)) {
        return false;
    }

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(MeshRenderer::PushConstants);

    VkPipelineLayoutCreateInfo meshLayoutInfo{};
    meshLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
#if defined(APORIA_EXPERIMENTAL)
    VkDescriptorSetLayout meshSetLayouts[2] = {context.sceneLayout, context.materialLayout};
    meshLayoutInfo.setLayoutCount = 2;
    meshLayoutInfo.pSetLayouts = meshSetLayouts;
#else
    meshLayoutInfo.setLayoutCount = 1;
    meshLayoutInfo.pSetLayouts = &context.sceneLayout;
#endif
    meshLayoutInfo.pushConstantRangeCount = 1;
    meshLayoutInfo.pPushConstantRanges = &pushRange;
    if (vkCreatePipelineLayout(device, &meshLayoutInfo, nullptr, &out.meshPipelineLayout) != VK_SUCCESS) {
        error = "vkCreatePipelineLayout(mesh) failed";
        return false;
    }

    VkPipelineShaderStageCreateInfo meshShaderStages[2]{};
    meshShaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    meshShaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    meshShaderStages[0].module = out.meshVertexShader;
    meshShaderStages[0].pName = "main";
    meshShaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    meshShaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    meshShaderStages[1].module = out.meshFragmentShader;
    meshShaderStages[1].pName = "main";

    VkVertexInputBindingDescription vertexBinding{};
    vertexBinding.binding = 0;
    vertexBinding.stride = static_cast<std::uint32_t>(sizeof(MeshVertex));
    vertexBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 3> vertexAttributes{};
    vertexAttributes[0].binding = 0;
    vertexAttributes[0].location = 0;
    vertexAttributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    vertexAttributes[0].offset = offsetof(MeshVertex, position);
    vertexAttributes[1].binding = 0;
    vertexAttributes[1].location = 1;
    vertexAttributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    vertexAttributes[1].offset = offsetof(MeshVertex, normal);
    vertexAttributes[2].binding = 0;
    vertexAttributes[2].location = 2;
    vertexAttributes[2].format = VK_FORMAT_R32G32_SFLOAT;
    vertexAttributes[2].offset = offsetof(MeshVertex, uv);

    VkPipelineVertexInputStateCreateInfo meshVertexInput{};
    meshVertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    meshVertexInput.vertexBindingDescriptionCount = 1;
    meshVertexInput.pVertexBindingDescriptions = &vertexBinding;
    meshVertexInput.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(vertexAttributes.size());
    meshVertexInput.pVertexAttributeDescriptions = vertexAttributes.data();

    // The depth-only shader consumes only position; sharing the full mesh
    // declaration triggered validation warnings for unused normal/UV inputs.
    VkVertexInputAttributeDescription shadowVertexAttribute{};
    shadowVertexAttribute.binding = 0;
    shadowVertexAttribute.location = 0;
    shadowVertexAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
    shadowVertexAttribute.offset = offsetof(MeshVertex, position);
    VkPipelineVertexInputStateCreateInfo shadowVertexInput{};
    shadowVertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    shadowVertexInput.vertexBindingDescriptionCount = 1;
    shadowVertexInput.pVertexBindingDescriptions = &vertexBinding;
    shadowVertexInput.vertexAttributeDescriptionCount = 1;
    shadowVertexInput.pVertexAttributeDescriptions = &shadowVertexAttribute;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.minDepthBounds = 0.0f;
    depthStencil.maxDepthBounds = 1.0f;

    VkGraphicsPipelineCreateInfo meshPipelineInfo{};
    meshPipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    meshPipelineInfo.stageCount = 2;
    meshPipelineInfo.pStages = meshShaderStages;
    meshPipelineInfo.pVertexInputState = &meshVertexInput;
    meshPipelineInfo.pInputAssemblyState = &inputAssembly;
    meshPipelineInfo.pViewportState = &viewportState;
    meshPipelineInfo.pRasterizationState = &rasterization;
    meshPipelineInfo.pMultisampleState = &multisample;
    meshPipelineInfo.pDepthStencilState = &depthStencil;
    meshPipelineInfo.pColorBlendState = &colorBlend;
    meshPipelineInfo.pDynamicState = &dynamicState;
    meshPipelineInfo.layout = out.meshPipelineLayout;
    meshPipelineInfo.renderPass = context.renderPass;
    meshPipelineInfo.subpass = 0;
    // Quality specialization: PCF tap count and reflection toggle compile per
    // preset, so Low pays neither cost.
    VkSpecializationMapEntry specEntries[3]{};
    specEntries[0].constantID = 0;
    specEntries[0].offset = 0;
    specEntries[0].size = sizeof(std::int32_t);
    specEntries[1].constantID = 1;
    specEntries[1].offset = sizeof(std::int32_t);
    specEntries[1].size = sizeof(float);
    specEntries[2].constantID = 2;
    specEntries[2].offset = 2 * sizeof(std::int32_t);
    specEntries[2].size = sizeof(std::int32_t);
    float specReflection = context.reflectionsEnabled ? 1.0f : 0.0f;
    std::int32_t specTaps = context.shadowTaps;
    std::int32_t specData[3] = {specTaps, 0, context.maxLights};
    std::memcpy(specData + 1, &specReflection, sizeof(float));
    VkSpecializationInfo meshSpecInfo{};
    meshSpecInfo.mapEntryCount = 3;
    meshSpecInfo.pMapEntries = specEntries;
    meshSpecInfo.dataSize = sizeof(specData);
    meshSpecInfo.pData = specData;
    meshShaderStages[1].pSpecializationInfo = &meshSpecInfo;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &meshPipelineInfo, nullptr, &out.meshPipeline) != VK_SUCCESS) {
        error = "vkCreateGraphicsPipelines(mesh) failed";
        return false;
    }
    if (context.hdrRenderPass != VK_NULL_HANDLE) {
        meshPipelineInfo.renderPass = context.hdrRenderPass;
        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &meshPipelineInfo, nullptr, &out.hdrMeshPipeline) != VK_SUCCESS) {
            error = "vkCreateGraphicsPipelines(hdr mesh) failed";
            return false;
        }
    }

    // Transparent mesh variants: same shaders/layout, but alpha-blended with
    // depth writes off so glass sorts over the body without punching holes.
    VkPipelineColorBlendAttachmentState transparentAttachment = colorAttachment;
    transparentAttachment.blendEnable = VK_TRUE;
    transparentAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    transparentAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    transparentAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    transparentAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    transparentAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    transparentAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    VkPipelineColorBlendStateCreateInfo transparentColorBlend = colorBlend;
    transparentColorBlend.pAttachments = &transparentAttachment;

    VkPipelineDepthStencilStateCreateInfo transparentDepthStencil = depthStencil;
    transparentDepthStencil.depthWriteEnable = VK_FALSE;

    meshPipelineInfo.pColorBlendState = &transparentColorBlend;
    meshPipelineInfo.pDepthStencilState = &transparentDepthStencil;
    meshPipelineInfo.renderPass = context.renderPass;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &meshPipelineInfo, nullptr, &out.meshTransparentPipeline) != VK_SUCCESS) {
        error = "vkCreateGraphicsPipelines(transparent mesh) failed";
        return false;
    }
    if (context.hdrRenderPass != VK_NULL_HANDLE) {
        meshPipelineInfo.renderPass = context.hdrRenderPass;
        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &meshPipelineInfo, nullptr, &out.hdrMeshTransparentPipeline) != VK_SUCCESS) {
            error = "vkCreateGraphicsPipelines(hdr transparent mesh) failed";
            return false;
        }
    }

    // Quality pipelines: shadow depth pass and HDR tonemap pass.
    if (!loadShaderModule(device, shaderDirectory + "/shadow.vert.spv", "shadow vertex", out.shadowVertexShader, error) ||
        !loadShaderModule(device, shaderDirectory + "/shadow.frag.spv", "shadow fragment", out.shadowFragmentShader, error)) {
        return false;
    }

    VkPushConstantRange shadowPushRange{};
    shadowPushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    shadowPushRange.offset = 0;
    shadowPushRange.size = sizeof(MeshRenderer::PushConstants);
    VkPipelineLayoutCreateInfo shadowLayoutInfo{};
    shadowLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    shadowLayoutInfo.setLayoutCount = 1;
    shadowLayoutInfo.pSetLayouts = &context.sceneLayout;
    shadowLayoutInfo.pushConstantRangeCount = 1;
    shadowLayoutInfo.pPushConstantRanges = &shadowPushRange;
    if (vkCreatePipelineLayout(device, &shadowLayoutInfo, nullptr, &out.shadowPipelineLayout) != VK_SUCCESS) {
        error = "vkCreatePipelineLayout(shadow) failed";
        return false;
    }

    VkPipelineShaderStageCreateInfo shadowStages[2]{};
    shadowStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shadowStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shadowStages[0].module = out.shadowVertexShader;
    shadowStages[0].pName = "main";
    shadowStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shadowStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shadowStages[1].module = out.shadowFragmentShader;
    shadowStages[1].pName = "main";

    VkPipelineDepthStencilStateCreateInfo shadowDepthStencil{};
    shadowDepthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    shadowDepthStencil.depthTestEnable = VK_TRUE;
    shadowDepthStencil.depthWriteEnable = VK_TRUE;
    shadowDepthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    shadowDepthStencil.minDepthBounds = 0.0f;
    shadowDepthStencil.maxDepthBounds = 1.0f;

    VkGraphicsPipelineCreateInfo shadowPipelineInfo{};
    shadowPipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    shadowPipelineInfo.stageCount = 2;
    shadowPipelineInfo.pStages = shadowStages;
    shadowPipelineInfo.pVertexInputState = &shadowVertexInput;
    shadowPipelineInfo.pInputAssemblyState = &inputAssembly;
    shadowPipelineInfo.pViewportState = &viewportState;
    shadowPipelineInfo.pRasterizationState = &rasterization;
    shadowPipelineInfo.pMultisampleState = &multisample;
    shadowPipelineInfo.pDepthStencilState = &shadowDepthStencil;
    shadowPipelineInfo.pDynamicState = &dynamicState;
    shadowPipelineInfo.layout = out.shadowPipelineLayout;
    shadowPipelineInfo.renderPass = context.shadowRenderPass;
    shadowPipelineInfo.subpass = 0;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &shadowPipelineInfo, nullptr, &out.shadowPipeline) != VK_SUCCESS) {
        error = "vkCreateGraphicsPipelines(shadow) failed";
        return false;
    }

    if (!loadShaderModule(device, shaderDirectory + "/tonemap.vert.spv", "tonemap vertex", out.tonemapVertexShader, error) ||
        !loadShaderModule(device, shaderDirectory + "/tonemap.frag.spv", "tonemap fragment", out.tonemapFragmentShader, error) ||
        !loadShaderModule(device, shaderDirectory + "/taa.frag.spv", "taa fragment", out.taaFragmentShader, error) ||
        !loadShaderModule(device, shaderDirectory + "/ui.frag.spv", "ui fragment", out.uiFragmentShader, error)) {
        return false;
    }

    VkDescriptorSetLayoutBinding hdrSamplerBinding{};
    hdrSamplerBinding.binding = 0;
    hdrSamplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    hdrSamplerBinding.descriptorCount = 1;
    hdrSamplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo tonemapLayoutInfo{};
    tonemapLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    tonemapLayoutInfo.bindingCount = 1;
    tonemapLayoutInfo.pBindings = &hdrSamplerBinding;
    if (vkCreateDescriptorSetLayout(device, &tonemapLayoutInfo, nullptr, &out.tonemapDescriptorLayout) != VK_SUCCESS) {
        error = "vkCreateDescriptorSetLayout(tonemap) failed";
        return false;
    }
    VkPipelineLayoutCreateInfo tonemapPipelineLayoutInfo{};
    tonemapPipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    tonemapPipelineLayoutInfo.setLayoutCount = 1;
    tonemapPipelineLayoutInfo.pSetLayouts = &out.tonemapDescriptorLayout;
    VkPushConstantRange tonemapPushRange{};
    tonemapPushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    tonemapPushRange.offset = 0;
    tonemapPushRange.size = sizeof(RectRenderer::PushConstants) + 16;
    tonemapPipelineLayoutInfo.pushConstantRangeCount = 1;
    tonemapPipelineLayoutInfo.pPushConstantRanges = &tonemapPushRange;
    if (vkCreatePipelineLayout(device, &tonemapPipelineLayoutInfo, nullptr, &out.tonemapPipelineLayout) != VK_SUCCESS) {
        error = "vkCreatePipelineLayout(tonemap) failed";
        return false;
    }
    VkDescriptorPoolSize tonemapPoolSize{};
    tonemapPoolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    tonemapPoolSize.descriptorCount = 1;
    VkDescriptorPoolCreateInfo tonemapPoolInfo{};
    tonemapPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    tonemapPoolInfo.maxSets = 1;
    tonemapPoolInfo.poolSizeCount = 1;
    tonemapPoolInfo.pPoolSizes = &tonemapPoolSize;
    if (vkCreateDescriptorPool(device, &tonemapPoolInfo, nullptr, &out.tonemapDescriptorPool) != VK_SUCCESS) {
        error = "vkCreateDescriptorPool(tonemap) failed";
        return false;
    }

    VkPipelineShaderStageCreateInfo tonemapStages[2]{};
    tonemapStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    tonemapStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    tonemapStages[0].module = out.tonemapVertexShader;
    tonemapStages[0].pName = "main";
    tonemapStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    tonemapStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    tonemapStages[1].module = out.tonemapFragmentShader;
    tonemapStages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo emptyVertexInput{};
    emptyVertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo stripAssembly{};
    stripAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    stripAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    stripAssembly.primitiveRestartEnable = VK_FALSE;

    VkGraphicsPipelineCreateInfo tonemapPipelineInfo{};
    tonemapPipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    tonemapPipelineInfo.stageCount = 2;
    tonemapPipelineInfo.pStages = tonemapStages;
    tonemapPipelineInfo.pVertexInputState = &emptyVertexInput;
    tonemapPipelineInfo.pInputAssemblyState = &stripAssembly;
    tonemapPipelineInfo.pViewportState = &viewportState;
    tonemapPipelineInfo.pRasterizationState = &rasterization;
    tonemapPipelineInfo.pMultisampleState = &multisample;
    tonemapPipelineInfo.pDepthStencilState = &triangleDepthStencil;
    tonemapPipelineInfo.pColorBlendState = &colorBlend;
    tonemapPipelineInfo.pDynamicState = &dynamicState;
    tonemapPipelineInfo.layout = out.tonemapPipelineLayout;
    tonemapPipelineInfo.renderPass = context.renderPass;
    tonemapPipelineInfo.subpass = 0;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &tonemapPipelineInfo, nullptr, &out.tonemapPipeline) != VK_SUCCESS) {
        error = "vkCreateGraphicsPipelines(tonemap) failed";
        return false;
    }

    // UI pipeline: same layout/render pass and vertex shader as tonemap, but the
    // SDF/blur fragment shader composited with straight alpha over the frame.
    VkPipelineShaderStageCreateInfo uiStages[2]{};
    uiStages[0] = tonemapStages[0];
    uiStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    uiStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    uiStages[1].module = out.uiFragmentShader;
    uiStages[1].pName = "main";
    VkPipelineColorBlendAttachmentState uiBlendAttachment{};
    uiBlendAttachment.blendEnable = VK_TRUE;
    uiBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    uiBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    uiBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    uiBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    uiBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    uiBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    uiBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                       VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo uiColorBlend{};
    uiColorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    uiColorBlend.attachmentCount = 1;
    uiColorBlend.pAttachments = &uiBlendAttachment;
    VkGraphicsPipelineCreateInfo uiPipelineInfo = tonemapPipelineInfo;
    uiPipelineInfo.pStages = uiStages;
    uiPipelineInfo.pColorBlendState = &uiColorBlend;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &uiPipelineInfo, nullptr, &out.uiPipeline) != VK_SUCCESS) {
        error = "vkCreateGraphicsPipelines(ui) failed";
        return false;
    }

    return true;
}

void destroyPipelines(VkDevice device, PipelineSet& p) noexcept {
    if (device == VK_NULL_HANDLE) {
        return;
    }
    auto destroyPipeline = [device](VkPipeline& handle) {
        if (handle != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, handle, nullptr);
            handle = VK_NULL_HANDLE;
        }
    };
    auto destroyLayout = [device](VkPipelineLayout& handle) {
        if (handle != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, handle, nullptr);
            handle = VK_NULL_HANDLE;
        }
    };
    auto destroyModule = [device](VkShaderModule& handle) {
        if (handle != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device, handle, nullptr);
            handle = VK_NULL_HANDLE;
        }
    };

    destroyPipeline(p.pipeline);
    destroyLayout(p.pipelineLayout);
    destroyPipeline(p.meshPipeline);
    destroyPipeline(p.hdrMeshPipeline);
    destroyPipeline(p.meshTransparentPipeline);
    destroyPipeline(p.hdrMeshTransparentPipeline);
    destroyLayout(p.meshPipelineLayout);
    destroyModule(p.meshVertexShader);
    destroyModule(p.meshFragmentShader);
    destroyPipeline(p.shadowPipeline);
    destroyLayout(p.shadowPipelineLayout);
    destroyModule(p.shadowVertexShader);
    destroyModule(p.shadowFragmentShader);
    destroyPipeline(p.tonemapPipeline);
    destroyLayout(p.tonemapPipelineLayout);
    if (p.tonemapDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, p.tonemapDescriptorPool, nullptr);
        p.tonemapDescriptorPool = VK_NULL_HANDLE;
    }
    if (p.tonemapDescriptorLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, p.tonemapDescriptorLayout, nullptr);
        p.tonemapDescriptorLayout = VK_NULL_HANDLE;
    }
    destroyModule(p.tonemapVertexShader);
    destroyModule(p.tonemapFragmentShader);
    destroyModule(p.taaFragmentShader);
    destroyPipeline(p.uiPipeline);
    destroyModule(p.uiFragmentShader);
    destroyModule(p.vertexShader);
    destroyModule(p.fragmentShader);
}

} // namespace Aporia::graphics_internal
