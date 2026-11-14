#pragma once

#if defined(_WIN32) && !defined(VK_USE_PLATFORM_WIN32_KHR)
#define VK_USE_PLATFORM_WIN32_KHR
#endif

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>

namespace Aporia::graphics_internal {

// Procedural infinite sky. Owns its shader modules, pipeline layout, descriptor
// set and the pipelines for the main and HDR render passes. The sky has no
// geometry: the fragment shader rebuilds the view ray from the inverse
// view-projection, so it is the world background and can never be left behind.
class SkyRenderer {
public:
    static constexpr std::uint32_t kSlots = 2;

    struct Uniforms {
        float cameraRight[4];
        float cameraUp[4];
        float cameraForward[4];
        float cameraPosition[4];
        float sunDirection[4]; // xyz toward sun, w intensity
        float sunColor[4];     // rgb, a disk/sharpness
        float skyParams[4];    // x horizon exponent, y haze, z glow, w tan(halfFovY)
        float renderParams[4]; // x aspect
        float twilight[4];     // rgb warm glow (dawn yellow / dusk pink), a intensity
        float seasonSky[4];    // rgb zenith tint, a tint strength (0 = palette default)
        float seasonHaze[4];   // rgb horizon tint
        float fog[4];          // rgb weather fog colour, a horizon fog blend (0 = off)
    };

    bool initialize(
        VkDevice device,
        const VkPhysicalDeviceMemoryProperties& memoryProperties,
        VkRenderPass renderPass,
        VkRenderPass hdrRenderPass,
        const std::string& shaderDirectory,
        std::string& error);
    void shutdown(VkDevice device) noexcept;

    bool writeUniforms(VkDevice device, std::uint32_t slot, const Uniforms& uniforms) noexcept;
    void record(VkCommandBuffer cmd, bool hdr, std::uint32_t slot) const noexcept;

    [[nodiscard]] bool valid() const noexcept { return pipeline_ != VK_NULL_HANDLE; }

private:
    bool createPipeline(VkDevice device, VkRenderPass renderPass, VkShaderModule vertex, VkShaderModule fragment,
                        VkPipeline& out, std::string& error);

    VkDevice device_ = VK_NULL_HANDLE;
    VkShaderModule vertexShader_ = VK_NULL_HANDLE;
    VkShaderModule fragmentShader_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipeline hdrPipeline_ = VK_NULL_HANDLE;
    VkBuffer buffers_[kSlots] = {};
    VkDeviceMemory memory_[kSlots] = {};
    VkDescriptorSet sets_[kSlots] = {};
};

} // namespace Aporia::graphics_internal
