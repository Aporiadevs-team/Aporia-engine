#include "TextRenderer.hpp"
#include "../Support/Support.hpp"

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

std::uint32_t decodeUtf8(std::string_view text, std::size_t& index) noexcept {
    const auto byte = [&](std::size_t at) { return static_cast<unsigned char>(text[at]); };
    const unsigned char lead = byte(index);
    if (lead < 0x80) {
        ++index;
        return lead;
    }
    if ((lead & 0xE0) == 0xC0 && index + 1 < text.size()) {
        const std::uint32_t cp = ((lead & 0x1Fu) << 6) | (byte(index + 1) & 0x3Fu);
        index += 2;
        return cp;
    }
    if ((lead & 0xF0) == 0xE0 && index + 2 < text.size()) {
        const std::uint32_t cp = ((lead & 0x0Fu) << 12) | ((byte(index + 1) & 0x3Fu) << 6) | (byte(index + 2) & 0x3Fu);
        index += 3;
        return cp;
    }
    if ((lead & 0xF8) == 0xF0 && index + 3 < text.size()) {
        const std::uint32_t cp = ((lead & 0x07u) << 18) | ((byte(index + 1) & 0x3Fu) << 12) |
                                 ((byte(index + 2) & 0x3Fu) << 6) | (byte(index + 3) & 0x3Fu);
        index += 4;
        return cp;
    }
    ++index;
    return lead;
}

const UiGlyph* findGlyph(const UiTextAtlasDesc& atlas, std::uint32_t codePoint) noexcept {
    for (const UiGlyph& glyph : atlas.glyphs) {
        if (glyph.codePoint == codePoint) {
            return &glyph;
        }
    }
    return nullptr;
}

} // namespace

bool TextRenderer::initialize(const Context& context) noexcept {
    if (context.device == VK_NULL_HANDLE || context.renderPass == VK_NULL_HANDLE) {
        return false;
    }
    device_ = context.device;

    VkDescriptorSetLayoutBinding bindings[2]{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 2;
    layoutInfo.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &descriptorLayout_) != VK_SUCCESS) {
        destroy();
        return false;
    }

    VkDescriptorPoolSize poolSizes[2]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = SlotCount;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = SlotCount;
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = SlotCount;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    if (vkCreateDescriptorPool(device_, &poolInfo, nullptr, &descriptorPool_) != VK_SUCCESS) {
        destroy();
        return false;
    }

    const VkDeviceSize bufferSize = sizeof(FontUniforms) + static_cast<VkDeviceSize>(MaxGlyphs) * sizeof(TextGlyph);
    for (Slot& slot : slots_) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device_, &bufferInfo, nullptr, &slot.buffer) != VK_SUCCESS) {
            destroy();
            return false;
        }
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device_, slot.buffer, &requirements);
        std::uint32_t memoryType = 0;
        if (!findMemoryType(context.memoryProperties, requirements.memoryTypeBits,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, memoryType)) {
            destroy();
            return false;
        }
        VkMemoryAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocateInfo.allocationSize = requirements.size;
        allocateInfo.memoryTypeIndex = memoryType;
        if (vkAllocateMemory(device_, &allocateInfo, nullptr, &slot.memory) != VK_SUCCESS ||
            vkBindBufferMemory(device_, slot.buffer, slot.memory, 0) != VK_SUCCESS ||
            vkMapMemory(device_, slot.memory, 0, bufferSize, 0, &slot.mapped) != VK_SUCCESS) {
            destroy();
            return false;
        }

        VkDescriptorSetAllocateInfo setInfo{};
        setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        setInfo.descriptorPool = descriptorPool_;
        setInfo.descriptorSetCount = 1;
        setInfo.pSetLayouts = &descriptorLayout_;
        if (vkAllocateDescriptorSets(device_, &setInfo, &slot.descriptorSet) != VK_SUCCESS) {
            destroy();
            return false;
        }
        VkDescriptorBufferInfo bufferDescriptor{};
        bufferDescriptor.buffer = slot.buffer;
        bufferDescriptor.offset = 0;
        bufferDescriptor.range = bufferSize;
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = slot.descriptorSet;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.pBufferInfo = &bufferDescriptor;
        vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    }

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorLayout_;
    if (vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr, &pipelineLayout_) != VK_SUCCESS) {
        destroy();
        return false;
    }

    bool spirvOk = false;
    const std::vector<std::uint32_t> vertexCode = readSpirv(context.shaderDirectory + "/text.vert.spv", spirvOk);
    const std::vector<std::uint32_t> msdfCode = readSpirv(context.shaderDirectory + "/text_msdf.frag.spv", spirvOk);
    const std::vector<std::uint32_t> ttfCode = readSpirv(context.shaderDirectory + "/text_ttf.frag.spv", spirvOk);
    if (vertexCode.empty() || msdfCode.empty() || ttfCode.empty()) {
        destroy();
        return false;
    }

    const auto createModule = [&](const std::vector<std::uint32_t>& code, VkShaderModule& module) noexcept {
        VkShaderModuleCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        info.codeSize = code.size() * sizeof(std::uint32_t);
        info.pCode = code.data();
        return vkCreateShaderModule(device_, &info, nullptr, &module) == VK_SUCCESS;
    };
    VkShaderModule vertexModule = VK_NULL_HANDLE;
    VkShaderModule msdfModule = VK_NULL_HANDLE;
    VkShaderModule ttfModule = VK_NULL_HANDLE;
    if (!createModule(vertexCode, vertexModule) || !createModule(msdfCode, msdfModule) ||
        !createModule(ttfCode, ttfModule)) {
        if (vertexModule != VK_NULL_HANDLE) vkDestroyShaderModule(device_, vertexModule, nullptr);
        if (msdfModule != VK_NULL_HANDLE) vkDestroyShaderModule(device_, msdfModule, nullptr);
        if (ttfModule != VK_NULL_HANDLE) vkDestroyShaderModule(device_, ttfModule, nullptr);
        destroy();
        return false;
    }

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
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
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_ALWAYS;
    VkPipelineColorBlendAttachmentState colorAttachment{};
    colorAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorAttachment.blendEnable = VK_TRUE;
    colorAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &colorAttachment;
    const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    const auto createPipeline = [&](VkShaderModule fragment, VkPipeline& pipeline) noexcept {
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertexModule;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragment;
        stages[1].pName = "main";
        VkGraphicsPipelineCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        info.stageCount = 2;
        info.pStages = stages;
        info.pVertexInputState = &vertexInput;
        info.pInputAssemblyState = &inputAssembly;
        info.pViewportState = &viewportState;
        info.pRasterizationState = &rasterization;
        info.pMultisampleState = &multisample;
        info.pDepthStencilState = &depthStencil;
        info.pColorBlendState = &colorBlend;
        info.pDynamicState = &dynamicState;
        info.layout = pipelineLayout_;
        info.renderPass = context.renderPass;
        info.subpass = 0;
        return vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline) == VK_SUCCESS;
    };

    const bool pipelinesOk = createPipeline(msdfModule, msdfPipeline_) && createPipeline(ttfModule, ttfPipeline_);
    vkDestroyShaderModule(device_, vertexModule, nullptr);
    vkDestroyShaderModule(device_, msdfModule, nullptr);
    vkDestroyShaderModule(device_, ttfModule, nullptr);
    if (!pipelinesOk) {
        destroy();
        return false;
    }
    return true;
}

void TextRenderer::destroy() noexcept {
    if (device_ == VK_NULL_HANDLE) {
        *this = TextRenderer{};
        return;
    }
    for (Slot& slot : slots_) {
        if (slot.mapped != nullptr) {
            vkUnmapMemory(device_, slot.memory);
            slot.mapped = nullptr;
        }
        if (slot.buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device_, slot.buffer, nullptr);
        }
        if (slot.memory != VK_NULL_HANDLE) {
            vkFreeMemory(device_, slot.memory, nullptr);
        }
        slot = {};
    }
    if (msdfPipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(device_, msdfPipeline_, nullptr);
    if (ttfPipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(device_, ttfPipeline_, nullptr);
    if (pipelineLayout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
    if (descriptorPool_ != VK_NULL_HANDLE) vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
    if (descriptorLayout_ != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device_, descriptorLayout_, nullptr);
    *this = TextRenderer{};
}

void TextRenderer::setAtlasTexture(const AtlasTexture& texture) noexcept {
    atlas_ = texture;
    updateDescriptors();
}

void TextRenderer::updateDescriptors() noexcept {
    if (device_ == VK_NULL_HANDLE || atlas_.view == VK_NULL_HANDLE || atlas_.sampler == VK_NULL_HANDLE) {
        return;
    }
    for (Slot& slot : slots_) {
        if (slot.descriptorSet == VK_NULL_HANDLE) {
            continue;
        }
        VkDescriptorImageInfo imageInfo{};
        imageInfo.sampler = atlas_.sampler;
        imageInfo.imageView = atlas_.view;
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = slot.descriptorSet;
        write.dstBinding = 1;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &imageInfo;
        vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    }
}

float TextRenderer::lineHeight(const UiTextAtlasDesc& atlas, float size) const noexcept {
    const float em = atlas.emSize > 0.0f ? atlas.emSize : 32.0f;
    return atlas.lineHeight * (size / em);
}

float TextRenderer::measureText(
        const UiTextAtlasDesc& atlas,
        std::string_view text,
        float size) const {
    const float em = atlas.emSize > 0.0f ? atlas.emSize : 32.0f;
    const float scale = size / em;
    float width = 0.0f;
    float maxWidth = 0.0f;
    std::size_t index = 0;
    while (index < text.size()) {
        const std::uint32_t cp = decodeUtf8(text, index);
        if (cp == '\n') {
            maxWidth = width > maxWidth ? width : maxWidth;
            width = 0.0f;
            continue;
        }
        if (const UiGlyph* glyph = findGlyph(atlas, cp)) {
            width += glyph->advance * scale;
        }
    }
    return width > maxWidth ? width : maxWidth;
}

std::vector<TextGlyph> TextRenderer::layoutText(
        const UiTextAtlasDesc& atlas,
        std::string_view text,
        float x,
        float y,
        float size,
        const TextColor& color,
        float outlineWidth,
        const TextColor& outlineColor) const {
    (void)outlineWidth;
    (void)outlineColor;
    const float em = atlas.emSize > 0.0f ? atlas.emSize : 32.0f;
    const float scale = size / em;
    const float lineAdvance = lineHeight(atlas, size);

    std::vector<TextGlyph> glyphs;
    glyphs.reserve(text.size());
    float cursorX = x;
    float cursorY = y;

    std::size_t index = 0;
    while (index < text.size()) {
        const std::uint32_t cp = decodeUtf8(text, index);
        if (cp == '\n') {
            cursorX = x;
            cursorY += lineAdvance;
            continue;
        }
        const UiGlyph* glyph = findGlyph(atlas, cp);
        if (glyph == nullptr) {
            continue;
        }
        if (glyph->planeWidth > 0.0f && glyph->planeHeight > 0.0f) {
            TextGlyph instance;
            instance.position[0] = cursorX + glyph->planeLeft * size;
            instance.position[1] = cursorY + (atlas.ascender - glyph->planeTop) * size;
            instance.size[0] = glyph->planeWidth * size;
            instance.size[1] = glyph->planeHeight * size;
            instance.uv[0] = glyph->u0;
            instance.uv[1] = glyph->v0;
            instance.uv[2] = glyph->u1;
            instance.uv[3] = glyph->v1;
            instance.color[0] = color.r;
            instance.color[1] = color.g;
            instance.color[2] = color.b;
            instance.color[3] = color.a;
            instance.glyphScale = scale;
            glyphs.push_back(instance);
        }
        cursorX += glyph->advance * scale;
    }
    return glyphs;
}

void TextRenderer::renderText(
        VkCommandBuffer commandBuffer,
        std::uint32_t frameSlot,
        float screenWidth,
        float screenHeight,
        float guiScale,
        float outlineWidth,
        const TextColor& outlineColor,
        const std::vector<TextGlyph>& glyphs,
        std::uint64_t signature) noexcept {
    if (commandBuffer == VK_NULL_HANDLE || glyphs.empty() || device_ == VK_NULL_HANDLE) {
        return;
    }
    Slot& slot = slots_[frameSlot % SlotCount];
    if (slot.mapped == nullptr) {
        return;
    }
    const std::size_t count = glyphs.size() < MaxGlyphs ? glyphs.size() : MaxGlyphs;

    // Persistent batch: identical content for this slot needs no re-upload.
    if (signature == 0 || signature != slot.uploadedSignature) {
        FontUniforms uniforms{};
        uniforms.screenData[0] = screenWidth;
        uniforms.screenData[1] = screenHeight;
        uniforms.screenData[2] = guiScale;
        uniforms.screenData[3] = outlineWidth;
        uniforms.outlineColor[0] = outlineColor.r;
        uniforms.outlineColor[1] = outlineColor.g;
        uniforms.outlineColor[2] = outlineColor.b;
        uniforms.outlineColor[3] = outlineColor.a;
        uniforms.atlasData[0] = atlas_.width;
        uniforms.atlasData[1] = atlas_.height;
        uniforms.atlasData[2] = atlas_.distanceRange;
        uniforms.atlasData[3] = atlas_.fontSize;
        uniforms.charCount[0] = static_cast<std::int32_t>(count);

        std::memcpy(slot.mapped, &uniforms, sizeof(uniforms));
        std::memcpy(static_cast<std::uint8_t*>(slot.mapped) + sizeof(uniforms),
                    glyphs.data(), count * sizeof(TextGlyph));
        slot.uploadedSignature = signature;
    }

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      mode_ == Mode::Ttf ? ttfPipeline_ : msdfPipeline_);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_,
                            0, 1, &slot.descriptorSet, 0, nullptr);
    vkCmdDraw(commandBuffer, static_cast<std::uint32_t>(count) * 6, 1, 0, 0);
}

} // namespace Aporia::graphics_internal
