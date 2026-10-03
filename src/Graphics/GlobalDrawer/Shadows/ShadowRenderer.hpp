#pragma once

#include "Aporia/Graphics/Graphics.hpp"
#include "../Meshes/MeshRenderer.hpp"

#include <cstdint>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.h>

namespace Aporia::graphics_internal {

// Owns the sun depth map: image, view, sampler, framebuffer and render pass.
// Sampling policy (PCF/PCSS/contact/cascades) comes from ShadowConfig; the
// resources here only describe the depth target itself.
class ShadowRenderer {
public:
    struct Resources {
        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
        std::uint32_t mapSize = 0;
    };

    static bool createRenderPass(VkDevice device, VkFormat depthFormat, Resources& resources) noexcept;

    static bool createTargets(
        VkDevice device,
        const VkPhysicalDeviceMemoryProperties& memoryProperties,
        VkFormat depthFormat,
        std::uint32_t mapSize,
        Resources& resources) noexcept;

    static bool createResources(
        VkDevice device,
        const VkPhysicalDeviceMemoryProperties& memoryProperties,
        VkFormat depthFormat,
        std::uint32_t mapSize,
        Resources& resources) noexcept;

    static void destroyTargets(VkDevice device, Resources& resources) noexcept;
    static void destroyResources(VkDevice device, Resources& resources) noexcept;

    struct RenderContext {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        std::uint32_t mapSize = 0;
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    };

    // Renders the scene from the sun into the depth map.
    static void renderShadowMap(
        const RenderContext& context,
        const float sunMatrix[16],
        const std::vector<RenderItem>& items,
        const std::unordered_map<std::uint64_t, MeshRenderer::Resource>& meshes) noexcept;

    void setConfig(const ShadowConfig& config) noexcept { config_ = config; }
    [[nodiscard]] const ShadowConfig& config() const noexcept { return config_; }
    void setPlayerUltra(bool enabled) noexcept { playerUltra_ = enabled; }
    [[nodiscard]] bool playerUltra() const noexcept { return playerUltra_; }

private:
    ShadowConfig config_{};
    bool playerUltra_ = false;
};

} // namespace Aporia::graphics_internal
