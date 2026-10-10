#pragma once

#if defined(_WIN32) && !defined(VK_USE_PLATFORM_WIN32_KHR)
#define VK_USE_PLATFORM_WIN32_KHR
#endif

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>

namespace Aporia::graphics_internal {

// Everything the initial pipeline pass builds: one shader module / layout /
// pipeline per renderer family. Owned by Graphics::Impl and torn down by
// destroyPipelines().
struct PipelineSet {
    // Debug triangle pass.
    VkShaderModule vertexShader = VK_NULL_HANDLE;
    VkShaderModule fragmentShader = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;

    // Mesh pass (main + HDR variant).
    VkShaderModule meshVertexShader = VK_NULL_HANDLE;
    VkShaderModule meshFragmentShader = VK_NULL_HANDLE;
    VkPipelineLayout meshPipelineLayout = VK_NULL_HANDLE;
    VkPipeline meshPipeline = VK_NULL_HANDLE;
    VkPipeline hdrMeshPipeline = VK_NULL_HANDLE;
    // Alpha-blended, depth-write-off variants of the mesh pipelines (glass).
    VkPipeline meshTransparentPipeline = VK_NULL_HANDLE;
    VkPipeline hdrMeshTransparentPipeline = VK_NULL_HANDLE;

    // Shadow depth pass.
    VkShaderModule shadowVertexShader = VK_NULL_HANDLE;
    VkShaderModule shadowFragmentShader = VK_NULL_HANDLE;
    VkPipelineLayout shadowPipelineLayout = VK_NULL_HANDLE;
    VkPipeline shadowPipeline = VK_NULL_HANDLE;

    // Tonemap + UI composite pass.
    VkShaderModule tonemapVertexShader = VK_NULL_HANDLE;
    VkShaderModule tonemapFragmentShader = VK_NULL_HANDLE;
    VkShaderModule taaFragmentShader = VK_NULL_HANDLE;
    VkShaderModule uiFragmentShader = VK_NULL_HANDLE;
    VkShaderModule blurFragmentShader = VK_NULL_HANDLE;
    VkDescriptorSetLayout tonemapDescriptorLayout = VK_NULL_HANDLE;
    VkPipelineLayout tonemapPipelineLayout = VK_NULL_HANDLE;
    VkDescriptorPool tonemapDescriptorPool = VK_NULL_HANDLE;
    VkPipeline tonemapPipeline = VK_NULL_HANDLE;
    VkPipeline uiPipeline = VK_NULL_HANDLE;
    VkPipeline blurPipeline = VK_NULL_HANDLE;
    VkPipeline blurPresentPipeline = VK_NULL_HANDLE;
};

// Inputs gather every dependency the pipelines need to be created. All handles
// must be valid for the passes that are requested (e.g. shadowRenderPass for
// the shadow pipeline).
struct PipelineContext {
    VkDevice device = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkRenderPass hdrRenderPass = VK_NULL_HANDLE;       // optional
    VkRenderPass shadowRenderPass = VK_NULL_HANDLE;
    VkDescriptorSetLayout triangleLayout = VK_NULL_HANDLE;  // set 0 for the debug triangle
    VkDescriptorSetLayout sceneLayout = VK_NULL_HANDLE;     // set 0 for mesh/shadow
    VkDescriptorSetLayout materialLayout = VK_NULL_HANDLE;  // set 1 for mesh (experimental, optional)
    std::string shaderDirectory;
    bool reflectionsEnabled = false;
    std::int32_t shadowTaps = 9;
    std::int32_t maxLights = 16;
};

// Creates the shader modules, descriptor layouts/pools and pipelines. On
// failure `error` receives a message and false is returned; already-created
// handles stay in `out` so the caller can destroy them through destroyPipelines.
bool createPipelines(const PipelineContext& context, PipelineSet& out, std::string& error);

// Destroys every non-null handle. Safe to call on a partially-built set.
void destroyPipelines(VkDevice device, PipelineSet& pipelines) noexcept;

} // namespace Aporia::graphics_internal
