#include "PrecipitationRenderer.hpp"

#if defined(APORIA_SEASONS)

#include "Device/MemoryManager/MemoryManager.hpp"
#include "Graphics/GlobalDrawer/Support/Support.hpp"

#include <cmath>
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

float fallSpeed(PrecipitationKind kind) noexcept {
    switch (kind) {
    case PrecipitationKind::Snow:
        return 1.3f;
    case PrecipitationKind::Sleet:
        return 5.0f;
    case PrecipitationKind::Rain:
    case PrecipitationKind::None:
        break;
    }
    return 9.5f;
}

float kindValue(PrecipitationKind kind) noexcept {
    switch (kind) {
    case PrecipitationKind::Snow:
        return 1.0f;
    case PrecipitationKind::Sleet:
        return 2.0f;
    case PrecipitationKind::Rain:
    case PrecipitationKind::None:
        break;
    }
    return 0.0f;
}

} // namespace

bool PrecipitationRenderer::initialize(
    VkDevice device,
    const VkPhysicalDeviceMemoryProperties& memoryProperties,
    VkRenderPass renderPass,
    VkRenderPass hdrRenderPass,
    const std::string& shaderDirectory,
    std::string& error) {
    device_ = device;
    if (!loadModule(device, shaderDirectory + "/precipitation.vert.spv", vertexShader_) ||
        !loadModule(device, shaderDirectory + "/precipitation.frag.spv", fragmentShader_)) {
        error = "Failed to load compiled precipitation shaders";
        return false;
    }

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(PushConstants);
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &layout_) != VK_SUCCESS) {
        error = "vkCreatePipelineLayout(precipitation) failed";
        return false;
    }

    const VkDeviceSize bufferSize = sizeof(Instance) * kWeatherMaxParticles;
    for (std::uint32_t slot = 0; slot < kSlots; ++slot) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffers_[slot]) != VK_SUCCESS) {
            error = "vkCreateBuffer(precipitation) failed";
            return false;
        }
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device, buffers_[slot], &requirements);
        std::uint32_t memoryType = 0;
        if (!MemoryManager::findMemoryType(
                memoryProperties, requirements.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, memoryType)) {
            error = "No host-visible memory for the precipitation vertex buffer";
            return false;
        }
        VkMemoryAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocateInfo.allocationSize = requirements.size;
        allocateInfo.memoryTypeIndex = memoryType;
        if (vkAllocateMemory(device, &allocateInfo, nullptr, &memory_[slot]) != VK_SUCCESS ||
            vkBindBufferMemory(device, buffers_[slot], memory_[slot], 0) != VK_SUCCESS) {
            error = "vkAllocateMemory(precipitation) failed";
            return false;
        }
        if (vkMapMemory(device, memory_[slot], 0, VK_WHOLE_SIZE, 0, &mapped_[slot]) != VK_SUCCESS) {
            mapped_[slot] = nullptr;
            error = "vkMapMemory(precipitation) failed";
            return false;
        }
    }

    if (!createPipeline(device, renderPass, vertexShader_, fragmentShader_, pipeline_, error)) {
        return false;
    }
    if (hdrRenderPass != VK_NULL_HANDLE &&
        !createPipeline(device, hdrRenderPass, vertexShader_, fragmentShader_, hdrPipeline_, error)) {
        return false;
    }

    particles_.resize(kWeatherMaxParticles);
    return true;
}

bool PrecipitationRenderer::createPipeline(
    VkDevice device,
    VkRenderPass renderPass,
    VkShaderModule vertex,
    VkShaderModule fragment,
    VkPipeline& out,
    std::string& error) {
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertex;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragment;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(Instance);
    binding.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
    VkVertexInputAttributeDescription attributes[3]{};
    attributes[0] = {0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0};
    attributes[1] = {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 16};
    attributes[2] = {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 32};
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 3;
    vertexInput.pVertexAttributeDescriptions = attributes;

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

    // Particles are occluded by scene geometry (depth test) but never write it.
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    // Premultiplied alpha: the fragment shader outputs colour * alpha.
    VkPipelineColorBlendAttachmentState colorAttachment{};
    colorAttachment.blendEnable = VK_TRUE;
    colorAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    colorAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
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
        error = "vkCreateGraphicsPipelines(precipitation) failed";
        return false;
    }
    return true;
}

float PrecipitationRenderer::randomFloat(std::uint32_t& state) noexcept {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return static_cast<float>(state & 0x00ffffffu) / static_cast<float>(0x01000000u);
}

void PrecipitationRenderer::respawn(std::uint32_t first, std::uint32_t last, const double camera[3]) {
    const float widthBase = kind_ == PrecipitationKind::Snow ? 0.050f
                          : kind_ == PrecipitationKind::Sleet ? 0.028f : 0.020f;
    const float lengthBase = kind_ == PrecipitationKind::Snow ? 0.050f
                           : kind_ == PrecipitationKind::Sleet ? 0.130f
                           : 0.060f + fallSpeed(kind_) * 0.018f;
    for (std::uint32_t index = first; index < last && index < particles_.size(); ++index) {
        Particle& particle = particles_[index];
        particle.position[0] = static_cast<float>(camera[0]) + (randomFloat(randomState_) * 2.0f - 1.0f) * kBoxRadius;
        particle.position[1] = static_cast<float>(camera[1]) + (randomFloat(randomState_) * 2.0f - 1.0f) * kBoxRadius;
        particle.position[2] = static_cast<float>(camera[2]) - kBoxBelow +
                               randomFloat(randomState_) * (kBoxAbove + kBoxBelow);
        particle.velocity[0] = 0.0f;
        particle.velocity[1] = 0.0f;
        particle.velocity[2] = -fallSpeed(kind_);
        particle.width = widthBase * (0.7f + 0.6f * randomFloat(randomState_));
        particle.length = lengthBase * (0.7f + 0.6f * randomFloat(randomState_));
        particle.alpha = 0.55f + 0.45f * randomFloat(randomState_);
        particle.phase = randomFloat(randomState_);
    }
}

void PrecipitationRenderer::update(
    std::uint32_t slot,
    const WeatherState& weather,
    const double cameraPosition[3],
    float deltaSeconds) noexcept {
    if (slot >= kSlots || particles_.empty() || mapped_[slot] == nullptr) {
        return;
    }
    const float dt = deltaSeconds > 0.1f ? 0.1f : (deltaSeconds > 0.0f ? deltaSeconds : 0.0f);
    elapsed_ += dt;

    const PrecipitationKind kind = weather.precipitation;
    const float intensity = weather.intensity > 1.0f ? 1.0f : (weather.intensity > 0.0f ? weather.intensity : 0.0f);
    const std::uint32_t budget = weather.particleCount == 0
        ? 2500u
        : (weather.particleCount > kWeatherMaxParticles ? kWeatherMaxParticles : weather.particleCount);
    std::uint32_t target = 0;
    if (kind != PrecipitationKind::None && intensity > 0.0f) {
        target = static_cast<std::uint32_t>(static_cast<float>(budget) * intensity);
        if (target == 0) {
            target = 1;
        }
    }

    const double dx = cameraPosition[0] - lastCamera_[0];
    const double dy = cameraPosition[1] - lastCamera_[1];
    const double dz = cameraPosition[2] - lastCamera_[2];
    const bool jumped = cameraKnown_ && (dx * dx + dy * dy + dz * dz) > double(kBoxRadius * kBoxRadius);
    const bool kindChanged = kind_ != kind;
    kind_ = kind;

    if (kindChanged || jumped || !cameraKnown_) {
        respawn(0, target, cameraPosition);
    } else if (target > activeCount_) {
        respawn(activeCount_, target, cameraPosition);
    }
    activeCount_ = target;
    cameraKnown_ = true;
    std::memcpy(lastCamera_, cameraPosition, sizeof(lastCamera_));
    if (activeCount_ == 0) {
        return;
    }

    const float fall = weather.fallSpeed > 0.0f ? weather.fallSpeed : fallSpeed(kind);
    const float turbulence = weather.turbulence > 1.0f ? 1.0f : (weather.turbulence > 0.0f ? weather.turbulence : 0.0f);
    const float drift = turbulence * (kind == PrecipitationKind::Snow ? 3.0f : 1.6f);
    const float sizeScale = 0.8f + 0.4f * intensity;
    const float alphaScale = 0.55f + 0.45f * intensity;
    const float alphaBase = kind == PrecipitationKind::Snow ? 0.90f
                           : kind == PrecipitationKind::Sleet ? 0.65f : 0.42f;
    const float kindId = kindValue(kind);
    Instance* instances = static_cast<Instance*>(mapped_[slot]);

    for (std::uint32_t index = 0; index < activeCount_; ++index) {
        Particle& particle = particles_[index];
        const float wave = particle.phase * 6.2831853f;
        particle.velocity[0] = weather.wind[0] + std::sin(elapsed_ * 1.7f + wave) * drift;
        particle.velocity[1] = weather.wind[1] + std::cos(elapsed_ * 1.3f + wave * 0.81f) * drift;
        particle.velocity[2] = -fall;
        particle.position[0] += particle.velocity[0] * dt;
        particle.position[1] += particle.velocity[1] * dt;
        particle.position[2] += particle.velocity[2] * dt;

        float offset = particle.position[0] - static_cast<float>(cameraPosition[0]);
        if (offset > kBoxRadius) {
            particle.position[0] -= 2.0f * kBoxRadius;
        } else if (offset < -kBoxRadius) {
            particle.position[0] += 2.0f * kBoxRadius;
        }
        offset = particle.position[1] - static_cast<float>(cameraPosition[1]);
        if (offset > kBoxRadius) {
            particle.position[1] -= 2.0f * kBoxRadius;
        } else if (offset < -kBoxRadius) {
            particle.position[1] += 2.0f * kBoxRadius;
        }
        offset = particle.position[2] - static_cast<float>(cameraPosition[2]);
        if (offset < -kBoxBelow) {
            particle.position[2] += kBoxAbove + kBoxBelow;
        } else if (offset > kBoxAbove) {
            particle.position[2] -= kBoxAbove + kBoxBelow;
        }

        Instance& instance = instances[index];
        instance.position[0] = particle.position[0];
        instance.position[1] = particle.position[1];
        instance.position[2] = particle.position[2];
        instance.position[3] = kindId;
        instance.velocity[0] = particle.velocity[0];
        instance.velocity[1] = particle.velocity[1];
        instance.velocity[2] = particle.velocity[2];
        instance.velocity[3] = particle.width * sizeScale;
        instance.size[0] = particle.length * sizeScale;
        instance.size[1] = alphaBase * particle.alpha * alphaScale;
        instance.size[2] = particle.phase;
        instance.size[3] = 0.0f;
    }
}

void PrecipitationRenderer::record(
    VkCommandBuffer cmd,
    bool hdr,
    std::uint32_t slot,
    const float viewMatrix[16],
    const float viewProj[16],
    const double cameraPosition[3],
    const float fogColor[3],
    float fogDensity) const noexcept {
    if (cmd == VK_NULL_HANDLE || slot >= kSlots || activeCount_ == 0 || buffers_[slot] == VK_NULL_HANDLE) {
        return;
    }
    const VkPipeline pipeline = hdr ? hdrPipeline_ : pipeline_;
    if (pipeline == VK_NULL_HANDLE) {
        return;
    }

    PushConstants push{};
    std::memcpy(push.viewProj, viewProj, sizeof(push.viewProj));
    // Column-major view: right = column 0, up = column 1.
    push.cameraRight[0] = viewMatrix[0];
    push.cameraRight[1] = viewMatrix[4];
    push.cameraRight[2] = viewMatrix[8];
    push.cameraUp[0] = viewMatrix[1];
    push.cameraUp[1] = viewMatrix[5];
    push.cameraUp[2] = viewMatrix[9];
    push.cameraPosition[0] = static_cast<float>(cameraPosition[0]);
    push.cameraPosition[1] = static_cast<float>(cameraPosition[1]);
    push.cameraPosition[2] = static_cast<float>(cameraPosition[2]);
    push.cameraPosition[3] = kBoxRadius;
    push.fog[0] = fogColor[0];
    push.fog[1] = fogColor[1];
    push.fog[2] = fogColor[2];
    push.fog[3] = fogDensity;

    const VkDeviceSize offset = 0;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdPushConstants(cmd, layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
    vkCmdBindVertexBuffers(cmd, 0, 1, &buffers_[slot], &offset);
    vkCmdDraw(cmd, 4, activeCount_, 0, 0);
}

void PrecipitationRenderer::shutdown(VkDevice device) noexcept {
    if (device == VK_NULL_HANDLE) {
        return;
    }
    for (std::uint32_t slot = 0; slot < kSlots; ++slot) {
        if (mapped_[slot] != nullptr && memory_[slot] != VK_NULL_HANDLE) {
            vkUnmapMemory(device, memory_[slot]);
            mapped_[slot] = nullptr;
        }
        if (buffers_[slot] != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, buffers_[slot], nullptr);
            buffers_[slot] = VK_NULL_HANDLE;
        }
        if (memory_[slot] != VK_NULL_HANDLE) {
            vkFreeMemory(device, memory_[slot], nullptr);
            memory_[slot] = VK_NULL_HANDLE;
        }
    }
    if (pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
    if (hdrPipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, hdrPipeline_, nullptr);
        hdrPipeline_ = VK_NULL_HANDLE;
    }
    if (layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, layout_, nullptr);
        layout_ = VK_NULL_HANDLE;
    }
    if (vertexShader_ != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, vertexShader_, nullptr);
        vertexShader_ = VK_NULL_HANDLE;
    }
    if (fragmentShader_ != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, fragmentShader_, nullptr);
        fragmentShader_ = VK_NULL_HANDLE;
    }
    particles_.clear();
    activeCount_ = 0;
    cameraKnown_ = false;
    device_ = VK_NULL_HANDLE;
}

} // namespace Aporia::graphics_internal

#endif
