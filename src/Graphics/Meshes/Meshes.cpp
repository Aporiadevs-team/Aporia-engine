#include "Graphics/Graphics.hpp"

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
    float specReflection = reflectionsEnabled ? 1.0f : 0.0f;
    std::int32_t specTaps = shadowTaps;
    std::int32_t specData[3] = {specTaps, 0, static_cast<std::int32_t>(maxLights)};
    std::memcpy(specData + 1, &specReflection, sizeof(float));
    VkSpecializationInfo specInfo{};
    specInfo.mapEntryCount = 3;
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

// ================= merged: MeshPrimitives.cpp =================
#include "Aporia/Graphics/MeshPrimitives.hpp"

#include <cmath>

namespace Aporia {
namespace apr {

namespace {

void addQuad(MeshData& mesh,
             const MeshVertex& a,
             const MeshVertex& b,
             const MeshVertex& c,
             const MeshVertex& d) {
    const std::uint32_t base = static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.insert(mesh.vertices.end(), {a, b, c, d});
    mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
}

void addQuadReverse(MeshData& mesh,
                    const MeshVertex& a,
                    const MeshVertex& b,
                    const MeshVertex& c,
                    const MeshVertex& d) {
    const std::uint32_t base = static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.insert(mesh.vertices.end(), {a, b, c, d});
    mesh.indices.insert(mesh.indices.end(), {base, base + 2, base + 1, base, base + 3, base + 2});
}

MeshVertex vertex(const float position[3], const float normal[3], float u, float v) {
    MeshVertex result;
    result.position[0] = position[0];
    result.position[1] = position[1];
    result.position[2] = position[2];
    result.normal[0] = normal[0];
    result.normal[1] = normal[1];
    result.normal[2] = normal[2];
    result.uv[0] = u;
    result.uv[1] = v;
    return result;
}

}

MeshData makeBoxMesh(float halfX, float halfY, float halfZ) {
    MeshData mesh;
    mesh.vertices.reserve(24);
    mesh.indices.reserve(36);

    const float px[3] = {1.0f, 0.0f, 0.0f};
    const float nx[3] = {-1.0f, 0.0f, 0.0f};
    const float py[3] = {0.0f, 1.0f, 0.0f};
    const float ny[3] = {0.0f, -1.0f, 0.0f};
    const float pz[3] = {0.0f, 0.0f, 1.0f};
    const float nz[3] = {0.0f, 0.0f, -1.0f};

    const auto face = [&](const float normal[3], const float uAxis[3], const float vAxis[3]) {
        const float center[3] = {normal[0] * halfX, normal[1] * halfY, normal[2] * halfZ};
        const float uExtent[3] = {uAxis[0] * halfX, uAxis[1] * halfY, uAxis[2] * halfZ};
        const float vExtent[3] = {vAxis[0] * halfX, vAxis[1] * halfY, vAxis[2] * halfZ};
        const float corners[4][3] = {
            {center[0] - uExtent[0] - vExtent[0], center[1] - uExtent[1] - vExtent[1], center[2] - uExtent[2] - vExtent[2]},
            {center[0] + uExtent[0] - vExtent[0], center[1] + uExtent[1] - vExtent[1], center[2] + uExtent[2] - vExtent[2]},
            {center[0] + uExtent[0] + vExtent[0], center[1] + uExtent[1] + vExtent[1], center[2] + uExtent[2] + vExtent[2]},
            {center[0] - uExtent[0] + vExtent[0], center[1] - uExtent[1] + vExtent[1], center[2] - uExtent[2] + vExtent[2]},
        };
        // The face axes above produce inward triangle winding; reverse it so
        // rasterization winding agrees with the explicitly stored outward normal.
        addQuadReverse(mesh,
            vertex(corners[0], normal, 0.0f, 0.0f),
            vertex(corners[1], normal, 1.0f, 0.0f),
            vertex(corners[2], normal, 1.0f, 1.0f),
            vertex(corners[3], normal, 0.0f, 1.0f));
    };

    face(pz, py, px);
    face(nz, px, py);
    face(py, px, pz);
    face(ny, pz, px);
    face(px, pz, py);
    face(nx, py, pz);

    return mesh;
}

MeshData makeSphereMesh(float radius, std::uint32_t segments, std::uint32_t rings) {
    MeshData mesh;
    mesh.vertices.reserve(static_cast<std::size_t>((rings + 1) * (segments + 1)));

    constexpr float pi = 3.14159265358979323846f;
    for (std::uint32_t ring = 0; ring <= rings; ++ring) {
        const float phi = pi * static_cast<float>(ring) / static_cast<float>(rings);
        const float sinPhi = std::sin(phi);
        const float cosPhi = std::cos(phi);
        for (std::uint32_t segment = 0; segment <= segments; ++segment) {
            const float theta = 2.0f * pi * static_cast<float>(segment) / static_cast<float>(segments);
            const float sinTheta = std::sin(theta);
            const float cosTheta = std::cos(theta);

            const float x = cosTheta * sinPhi;
            const float y = sinTheta * sinPhi;
            const float z = cosPhi;

            MeshVertex v;
            v.position[0] = radius * x;
            v.position[1] = radius * y;
            v.position[2] = radius * z;
            v.normal[0] = x;
            v.normal[1] = y;
            v.normal[2] = z;
            v.uv[0] = static_cast<float>(segment) / static_cast<float>(segments);
            v.uv[1] = static_cast<float>(ring) / static_cast<float>(rings);
            mesh.vertices.push_back(v);
        }
    }

    for (std::uint32_t ring = 0; ring < rings; ++ring) {
        for (std::uint32_t segment = 0; segment < segments; ++segment) {
            const std::uint32_t first = ring * (segments + 1) + segment;
            const std::uint32_t second = first + segments + 1;
            mesh.indices.insert(mesh.indices.end(), {first, second, first + 1, first + 1, second, second + 1});
        }
    }

    return mesh;
}

MeshData makePlaneMesh(float halfX, float halfY) {
    MeshData mesh;
    const float n[3] = {0.0f, 0.0f, 1.0f};
    const float a[3] = {-halfX, -halfY, 0.0f};
    const float b[3] = {halfX, -halfY, 0.0f};
    const float c[3] = {halfX, halfY, 0.0f};
    const float d[3] = {-halfX, halfY, 0.0f};
    addQuad(mesh, vertex(a, n, 0.0f, 0.0f), vertex(b, n, 1.0f, 0.0f), vertex(c, n, 1.0f, 1.0f), vertex(d, n, 0.0f, 1.0f));
    return mesh;
}

MeshData makeCapsuleMesh(float radius, float halfHeight, std::uint32_t segments, std::uint32_t rings) {
    MeshData mesh;
    mesh.vertices.reserve(static_cast<std::size_t>((2 * rings + 2) * (segments + 1)));

    constexpr float pi = 3.14159265358979323846f;
    for (std::uint32_t ring = 0; ring <= rings; ++ring) {
        const float phi = 0.5f * pi * static_cast<float>(ring) / static_cast<float>(rings);
        const float sinPhi = std::sin(phi);
        const float cosPhi = std::cos(phi);
        for (std::uint32_t segment = 0; segment <= segments; ++segment) {
            const float theta = 2.0f * pi * static_cast<float>(segment) / static_cast<float>(segments);
            MeshVertex v;
            v.position[0] = radius * std::cos(theta) * sinPhi;
            v.position[1] = radius * std::sin(theta) * sinPhi;
            v.position[2] = halfHeight + radius * cosPhi;
            v.normal[0] = std::cos(theta) * sinPhi;
            v.normal[1] = std::sin(theta) * sinPhi;
            v.normal[2] = cosPhi;
            v.uv[0] = static_cast<float>(segment) / static_cast<float>(segments);
            v.uv[1] = static_cast<float>(ring) / static_cast<float>(2 * rings);
            mesh.vertices.push_back(v);
        }
    }
    for (std::uint32_t ring = 0; ring <= rings; ++ring) {
        const float phi = 0.5f * pi * (1.0f - static_cast<float>(ring) / static_cast<float>(rings));
        const float sinPhi = std::sin(phi);
        const float cosPhi = std::cos(phi);
        for (std::uint32_t segment = 0; segment <= segments; ++segment) {
            const float theta = 2.0f * pi * static_cast<float>(segment) / static_cast<float>(segments);
            MeshVertex v;
            v.position[0] = radius * std::cos(theta) * sinPhi;
            v.position[1] = radius * std::sin(theta) * sinPhi;
            v.position[2] = -halfHeight - radius * cosPhi;
            v.normal[0] = std::cos(theta) * sinPhi;
            v.normal[1] = std::sin(theta) * sinPhi;
            v.normal[2] = -cosPhi;
            v.uv[0] = static_cast<float>(segment) / static_cast<float>(segments);
            v.uv[1] = 0.5f + static_cast<float>(ring) / static_cast<float>(2 * rings);
            mesh.vertices.push_back(v);
        }
    }

    const std::uint32_t rowSize = segments + 1;
    const std::uint32_t topRows = rings + 1;
    const std::uint32_t totalRows = 2 * topRows;
    const auto addRow = [&](std::uint32_t rowIndex, std::uint32_t segmentsCount) {
        for (std::uint32_t segment = 0; segment < segmentsCount; ++segment) {
            const std::uint32_t first = rowIndex * rowSize + segment;
            const std::uint32_t second = first + rowSize;
            mesh.indices.insert(mesh.indices.end(), {first, second, first + 1, first + 1, second, second + 1});
        }
    };
    // Every band between consecutive rows, including the cylinder wall that
    // joins the two hemispheres and the whole lower hemisphere.
    for (std::uint32_t row = 0; row + 1 < totalRows; ++row) {
        addRow(row, segments);
    }

    return mesh;
}

}
}
