#pragma once

#if defined(_WIN32) && !defined(VK_USE_PLATFORM_WIN32_KHR)
#define VK_USE_PLATFORM_WIN32_KHR
#endif

#include "Device/SceneManager/SceneBindings/SceneBindings.hpp"
#include "Graphics/GlobalDrawer/Smooth/TAA/TaaRenderer.hpp"
#include "Graphics/GlobalDrawer/Shadows/ShadowRenderer.hpp"

#include <cstdint>
#include <string>
#include <vulkan/vulkan.h>

namespace Aporia::graphics_internal {

class TextureStore;

// Owns the offscreen quality-pass targets (HDR scene target, planar reflection
// target and the shared fallback depth attachment), their render passes/sampler
// and the shadow map resources. Graphics::Impl inherits this state so existing
// member access stays unchanged.
struct QualityTargets {
    struct Device {
        VkDevice device = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkPhysicalDeviceMemoryProperties memoryProperties{};
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkQueue queue = VK_NULL_HANDLE;
    };

    struct RebuildContext {
        VkRenderPass mainRenderPass = VK_NULL_HANDLE;
        VkFormat colorFormat = VK_FORMAT_UNDEFINED;
        VkFormat depthFormat = VK_FORMAT_UNDEFINED;
        bool headless = true;
        VkExtent2D swapchainExtent{};
        std::uint32_t renderWidth = 0;
        std::uint32_t renderHeight = 0;
        bool hdrEnabled = false;
        bool reflectionsEnabled = false;
        float reflectionScale = 0.0f;
        std::uint32_t shadowMapSize = 0;
        std::uint32_t antiAliasing = 0;
        std::uint32_t sceneUniformSize = 0;
        SceneBindings* scene = nullptr;
        const TextureStore* textures = nullptr;
        TaaRenderer* taa = nullptr;
        VkDescriptorSetLayout tonemapDescriptorLayout = VK_NULL_HANDLE;
        VkDescriptorPool tonemapDescriptorPool = VK_NULL_HANDLE;
        VkShaderModule tonemapVertexShader = VK_NULL_HANDLE;
        VkShaderModule taaFragmentShader = VK_NULL_HANDLE;
#if defined(APORIA_EXPERIMENTAL)
        // Froxel volumetric grid the tonemap pass samples (owned by
        // VolumetricRenderer). Unbound when volumetric is unavailable.
        VkImageView froxelView = VK_NULL_HANDLE;
        VkSampler froxelSampler = VK_NULL_HANDLE;
#endif
    };

    Device gpu{};

    // Fallback depth attachment for the headless target and offscreen targets.
    VkImage depthImage = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory = VK_NULL_HANDLE;
    VkImageView depthView = VK_NULL_HANDLE;
    std::uint32_t depthWidth = 0;
    std::uint32_t depthHeight = 0;

    // HDR scene target: all meshes render here; tonemap pass copies it out.
    VkImage hdrImage = VK_NULL_HANDLE;
    VkDeviceMemory hdrMemory = VK_NULL_HANDLE;
    VkImageView hdrView = VK_NULL_HANDLE;
    VkSampler hdrSampler = VK_NULL_HANDLE;
    VkFramebuffer hdrFramebuffer = VK_NULL_HANDLE;
    VkImage hdrDepthImage = VK_NULL_HANDLE;
    VkDeviceMemory hdrDepthMemory = VK_NULL_HANDLE;
    VkImageView hdrDepthView = VK_NULL_HANDLE;
    std::uint32_t hdrWidth = 0;
    std::uint32_t hdrHeight = 0;
    VkFormat hdrFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    VkRenderPass hdrRenderPass = VK_NULL_HANDLE;

    // Planar reflection target: mirrored scene rendered from the reflected camera.
    VkImage reflectImage = VK_NULL_HANDLE;
    VkDeviceMemory reflectMemory = VK_NULL_HANDLE;
    VkImageView reflectView = VK_NULL_HANDLE;
    VkFramebuffer reflectFramebuffer = VK_NULL_HANDLE;
    VkImage reflectDepthImage = VK_NULL_HANDLE;
    VkDeviceMemory reflectDepthMemory = VK_NULL_HANDLE;
    VkImageView reflectDepthView = VK_NULL_HANDLE;
    std::uint32_t reflectWidth = 0;
    std::uint32_t reflectHeight = 0;
    VkRenderPass reflectRenderPass = VK_NULL_HANDLE;
    VkSampler reflectSampler = VK_NULL_HANDLE;

    ShadowRenderer::Resources shadow;
    VkDescriptorSet tonemapDescriptorSet = VK_NULL_HANDLE;

    // Half-res separable blur chain for the UI glass: pass 1 blurs the HDR scene
    // horizontally into blurA, pass 2 blurs blurA vertically into blurB, and the
    // UI composites blurB. Both targets reuse the HDR render pass (colour+depth).
    struct BlurTarget {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        VkImage depthImage = VK_NULL_HANDLE;
        VkDeviceMemory depthMemory = VK_NULL_HANDLE;
        VkImageView depthView = VK_NULL_HANDLE;
    };
    BlurTarget blurA;
    BlurTarget blurB;
    VkSampler blurSampler = VK_NULL_HANDLE;
    VkDescriptorSet blurASet = VK_NULL_HANDLE; // samples blurA (vertical pass input)
    VkDescriptorSet blurBSet = VK_NULL_HANDLE; // samples blurB (UI input)
    std::uint32_t blurWidth = 0;
    std::uint32_t blurHeight = 0;
    [[nodiscard]] bool blurReady() const noexcept { return blurB.framebuffer != VK_NULL_HANDLE; }

    // Layered backdrop target: the tonemapped scene plus the accumulated UI
    // layers, blurred per level so a glass rect captures the layers below it.
    BlurTarget uiTarget;
    VkDescriptorSet uiTargetSet = VK_NULL_HANDLE;
    VkRenderPass uiAppendRenderPass = VK_NULL_HANDLE;
    VkFramebuffer uiAppendFramebuffer = VK_NULL_HANDLE;
    [[nodiscard]] bool uiTargetReady() const noexcept { return uiTarget.framebuffer != VK_NULL_HANDLE; }

#if defined(APORIA_EXPERIMENTAL)
    // Froxel fog integration parameters for the tonemap shader. The froxel grid
    // itself lives in VolumetricRenderer; this only carries the camera mapping.
    struct VolumetricUniforms {
        float invViewProj[16];
        float cameraPosition[4]; // xyz eye
        float params[4];         // x near, y far, z maxDistance, w steps
        float params2[4];        // x strength, y enabled
    };
    VkSampler tonemapDepthSampler = VK_NULL_HANDLE;
    VkBuffer tonemapVolumetricBuffer = VK_NULL_HANDLE;
    VkDeviceMemory tonemapVolumetricMemory = VK_NULL_HANDLE;

    // Uploads the tonemap-side froxel parameters (safe to call every frame).
    bool writeTonemapVolumetric(const VolumetricUniforms& uniforms) noexcept;
#endif

    void setDevice(const Device& device) noexcept { gpu = device; }

    [[nodiscard]] VkFormat chooseDepthFormat() const noexcept;
    // Creates the depth image at the requested size when needed. Updates depthFormat.
    bool ensureDepthForExtent(VkFormat& depthFormat, std::uint32_t width, std::uint32_t height, std::string& error);
    void destroyDepth(VkDevice device) noexcept;

    bool createHdrRenderPass(VkFormat depthFormat, std::string& error);
    bool createReflectRenderPass(VkFormat colorFormat, VkFormat depthFormat, std::string& error);
    bool createShadowResources(VkFormat depthFormat, std::uint32_t mapSize, std::string& error);

    // (Re)creates HDR/shadow/reflection targets for the current preset.
    bool rebuild(const RebuildContext& rebuildContext, std::string& error);
    // Destroys every target, the render passes/sampler and the shadow resources.
    // `tonemapPool` (owned elsewhere) is reset to reclaim the tonemap set.
    void destroyTargets(VkDevice device, VkDescriptorPool tonemapPool, TaaRenderer& taa, SceneBindings& scene) noexcept;

private:
    bool createColorTarget(
        VkFormat depthFormat,
        VkFormat format,
        VkImageUsageFlags extraUsage,
        std::uint32_t width,
        std::uint32_t height,
        VkRenderPass compatibleRenderPass,
        VkImageView& outView,
        VkFramebuffer& outFramebuffer,
        VkImage& outImage,
        VkDeviceMemory& outMemory,
        VkImageView& outDepthView,
        VkImage& outDepthImage,
        VkDeviceMemory& outDepthMemory,
        std::string& error);
};

} // namespace Aporia::graphics_internal
