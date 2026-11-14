#pragma once

#if defined(_WIN32) && !defined(VK_USE_PLATFORM_WIN32_KHR)
#define VK_USE_PLATFORM_WIN32_KHR
#endif

#include <vulkan/vulkan.h>

#include <cstdint>

namespace Aporia::graphics_internal {

// Temporal anti-aliasing subsystem. Owns the resolve/history targets, the
// resolve pipeline and the uniforms; Graphics only hands it the scene color /
// depth views and the per-frame matrices.
class TaaRenderer {
public:
    struct Context {
        VkDevice device = VK_NULL_HANDLE;
        VkPhysicalDeviceMemoryProperties memoryProperties{};
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE; // upload command buffer
        VkQueue queue = VK_NULL_HANDLE;
        VkRenderPass renderPass = VK_NULL_HANDLE;       // HDR render pass
        VkFormat hdrFormat = VK_FORMAT_UNDEFINED;
        VkFormat depthFormat = VK_FORMAT_UNDEFINED;
        VkShaderModule vertexShader = VK_NULL_HANDLE;   // tonemap.vert
        VkShaderModule fragmentShader = VK_NULL_HANDLE; // taa.frag
        VkImageView colorView = VK_NULL_HANDLE;         // scene HDR color
        VkImageView depthView = VK_NULL_HANDLE;         // scene depth
        VkSampler colorSampler = VK_NULL_HANDLE;
    };

    // (Re)creates the resolve targets for the given size. No-op when the size
    // is unchanged and resources are valid.
    bool initialize(const Context& context, std::uint32_t width, std::uint32_t height);
    void shutdown() noexcept;

    [[nodiscard]] bool valid() const noexcept { return valid_; }
    [[nodiscard]] VkImageView resolveView() const noexcept { return resolveView_; }
    [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }

    // Records the resolve pass and copies the result into the history buffer.
    // `depthImage` is the scene depth image (barriered to SHADER_READ here).
    void record(
        VkCommandBuffer commandBuffer,
        VkImage depthImage,
        std::uint32_t frameSlot,
        const float prevViewProj[16],
        const float invViewProj[16],
        float blend) noexcept;

    void resetHistory() noexcept { historyValid_ = false; }

private:
    bool uploadUniforms(std::uint32_t frameSlot, const float prevViewProj[16], const float invViewProj[16], float blend);

    static constexpr std::uint32_t kSlots = 2;

    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties memoryProperties_{};
    VkCommandBuffer uploadCommandBuffer_ = VK_NULL_HANDLE;
    VkQueue uploadQueue_ = VK_NULL_HANDLE;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkFormat hdrFormat_ = VK_FORMAT_UNDEFINED;
    VkFormat depthFormat_ = VK_FORMAT_UNDEFINED;
    VkShaderModule vertexShader_ = VK_NULL_HANDLE;
    VkShaderModule fragmentShader_ = VK_NULL_HANDLE;
    VkImageView colorView_ = VK_NULL_HANDLE;
    VkImageView depthView_ = VK_NULL_HANDLE;
    VkSampler colorSampler_ = VK_NULL_HANDLE;

    VkDescriptorSetLayout descriptorLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSets_[kSlots]{};
    VkBuffer uniformBuffers_[kSlots]{};
    VkDeviceMemory uniformMemories_[kSlots]{};

    VkImage resolveImage_ = VK_NULL_HANDLE;
    VkDeviceMemory resolveMemory_ = VK_NULL_HANDLE;
    VkImageView resolveView_ = VK_NULL_HANDLE;
    VkFramebuffer resolveFramebuffer_ = VK_NULL_HANDLE;
    VkImageView resolveDepthView_ = VK_NULL_HANDLE;
    VkImage resolveDepthImage_ = VK_NULL_HANDLE;
    VkDeviceMemory resolveDepthMemory_ = VK_NULL_HANDLE;

    VkImage historyImage_ = VK_NULL_HANDLE;
    VkDeviceMemory historyMemory_ = VK_NULL_HANDLE;
    VkImageView historyView_ = VK_NULL_HANDLE;

    VkSampler historySampler_ = VK_NULL_HANDLE;
    VkSampler depthSampler_ = VK_NULL_HANDLE;

    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    bool valid_ = false;
    bool historyValid_ = false;
};

} // namespace Aporia::graphics_internal
