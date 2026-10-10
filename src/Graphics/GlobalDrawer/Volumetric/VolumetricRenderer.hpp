#pragma once

#if defined(_WIN32) && !defined(VK_USE_PLATFORM_WIN32_KHR)
#define VK_USE_PLATFORM_WIN32_KHR
#endif

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>

namespace Aporia::graphics_internal {

// EXPERIMENTAL froxel volumetric fog producer (behind APORIA_EXPERIMENTAL).
//
// Owns a camera-frustum-aligned 3D froxel grid and the compute pipeline that
// fills it with in-scattered sunlight each frame. The tonemap pass samples the
// grid along each pixel's view ray; this module only produces the volume. It is
// deliberately self-contained: no engine pass depends on it and, when the device
// cannot store to RGBA16F, it degrades to an empty 1x1x1 volume (valid()==false)
// instead of failing engine initialization.
class VolumetricRenderer {
public:
    // Grid resolution: half-ish of a 720p frame, 64 depth slices up close.
    static constexpr std::uint32_t kGridX = 160;
    static constexpr std::uint32_t kGridY = 90;
    static constexpr std::uint32_t kGridZ = 64;
    static constexpr std::uint32_t kSlots = 2;

    struct Device {
        VkDevice device = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkPhysicalDeviceMemoryProperties memoryProperties{};
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE; // utility buffer for one-shot uploads
        VkQueue queue = VK_NULL_HANDLE;
    };

    // Matches the InjectParams block in shaders/volumetric_inject.comp.
    struct InjectParams {
        float invViewProj[16];
        float sunViewProj[16];
        float cameraPosition[4];
        float sunDirection[4]; // xyz toward the sun, w intensity
        float sunColor[4];     // rgb
        float fogParams[4];    // x near, y far, z density, w height falloff
        float gridParams[4];   // x phaseG, y noiseScale, z groundZ, w time
    };

    // Creates the froxel grid, sampler and compute pipeline. `shadowView` /
    // `shadowSampler` describe the sun depth map the inject pass samples.
    bool initialize(
        const Device& device,
        const std::string& shaderDirectory,
        VkImageView shadowView,
        VkSampler shadowSampler,
        std::string& error) noexcept;

    // Re-points the inject pass at a (re)created shadow map. Safe every frame.
    void bindShadow(VkImageView shadowView, VkSampler shadowSampler) noexcept;

    // Uploads one frame slot's inject parameters.
    bool writeParams(std::uint32_t slot, const InjectParams& params) noexcept;

    // Records the barriers + dispatch that fills the grid. Must be called
    // outside a render pass, after the shadow map for this frame is produced.
    void record(VkCommandBuffer commandBuffer, std::uint32_t slot) noexcept;

    [[nodiscard]] VkImageView view() const noexcept { return view_; }
    [[nodiscard]] VkSampler sampler() const noexcept { return sampler_; }
    [[nodiscard]] bool storageSupported() const noexcept { return storageSupported_; }

    void destroy() noexcept;

private:
    bool createImage(std::string& error) noexcept;
    bool createPipeline(const std::string& shaderDirectory, std::string& error) noexcept;
    bool createParams() noexcept;
    bool writeDescriptors(VkImageView shadowView, VkSampler shadowSampler) noexcept;

    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties memoryProperties_{};
    VkCommandBuffer utilityCommandBuffer_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;

    VkImage image_ = VK_NULL_HANDLE;
    VkDeviceMemory imageMemory_ = VK_NULL_HANDLE;
    VkImageView view_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;

    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkShaderModule shaderModule_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet sets_[kSlots] = {};
    VkBuffer paramBuffers_[kSlots] = {};
    VkDeviceMemory paramMemories_[kSlots] = {};
    void* paramMapped_[kSlots] = {};

    bool storageSupported_ = true;
    bool valid_ = false;
};

} // namespace Aporia::graphics_internal
