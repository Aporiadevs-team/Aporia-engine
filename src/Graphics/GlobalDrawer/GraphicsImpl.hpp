#pragma once

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "Aporia/Graphics/Graphics.hpp"
#include "Aporia/Math/Matrix4.hpp"

#include "Device/Device.hpp"
#include "Device/MemoryManager/MemoryManager.hpp"
#include "Device/Pipeline/PipelineFactory.hpp"
#include "Device/Init/RenderPass.hpp"
#include "Device/SceneManager/QualityTargets/QualityTargets.hpp"
#include "Device/SceneManager/SceneBindings/SceneBindings.hpp"
#include "Device/Swapchain/Swapchain.hpp"
#include "Device/Threading/JobSystem.hpp"
#include "Graphics/GlobalDrawer/Text/FontStore.hpp"
#include "Graphics/GlobalDrawer/Hdr/HdrRenderer.hpp"
#include "Graphics/GlobalDrawer/Materials/MaterialStore.hpp"
#include "Graphics/GlobalDrawer/Meshes/MeshRenderer.hpp"
#include "Graphics/GlobalDrawer/Meshes/MeshStore.hpp"
#include "Graphics/GlobalDrawer/Rectangulars/RectRenderer.hpp"
#include "Graphics/GlobalDrawer/Shadows/ShadowRenderer.hpp"
#include "Graphics/GlobalDrawer/Support/Support.hpp"
#include "Graphics/GlobalDrawer/Text/TextRenderer.hpp"
#include "Graphics/TextureManager/TextureStore.hpp"
#include "Graphics/TextureManager/TextureManager.hpp"

#if defined(_WIN32) && !defined(VK_USE_PLATFORM_WIN32_KHR)
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <vulkan/vulkan.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Aporia {

// Offscreen target edge used when GraphicsConfig leaves the headless size at 0.
inline constexpr std::uint32_t kDefaultHeadlessExtent = 64;

// Private implementation of the Graphics facade. Declared here so its member
// functions can be spread across several translation units by responsibility.
struct Graphics::Impl : graphics_internal::QualityTargets {
    graphics_internal::Device deviceService;
    graphics_internal::MeshRenderer meshRenderer;
    graphics_internal::ShadowRenderer shadowRenderer;
    graphics_internal::HdrRenderer hdrRenderer;
    graphics_internal::TextRenderer textRenderer;
    graphics_internal::RectRenderer rectRenderer;
    graphics_internal::TextureManager textureManager;
    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
    PFN_vkDestroyDebugUtilsMessengerEXT destroyDebugMessenger = nullptr;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    PFN_vkDestroySurfaceKHR destroySurface = nullptr;
#if defined(_WIN32)
    HWND window = nullptr;
#endif
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR getPhysicalDeviceSurfaceSupport = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR getPhysicalDeviceSurfaceCapabilities = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR getPhysicalDeviceSurfaceFormats = nullptr;
    PFN_vkGetPhysicalDeviceSurfacePresentModesKHR getPhysicalDeviceSurfacePresentModes = nullptr;
    PFN_vkCreateSwapchainKHR createSwapchain = nullptr;
    PFN_vkDestroySwapchainKHR destroySwapchain = nullptr;
    PFN_vkGetSwapchainImagesKHR getSwapchainImages = nullptr;
    PFN_vkAcquireNextImageKHR acquireNextImage = nullptr;
    PFN_vkQueuePresentKHR queuePresent = nullptr;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    std::uint32_t graphicsFamily = 0;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkImage colorImage = VK_NULL_HANDLE;
    VkDeviceMemory colorMemory = VK_NULL_HANDLE;
    VkImageView colorView = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainViews;
    std::vector<VkFramebuffer> swapchainFramebuffers;
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    VkExtent2D swapchainExtent{0, 0};
    VkSurfaceFormatKHR surfaceFormat{};
    VkColorSpaceKHR swapchainColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    // Shader modules, pipeline layouts and pipelines for every pass.
    graphics_internal::PipelineSet pipelines;
    // Texture handles, the albedo descriptor set and the geometry store.
    graphics_internal::TextureStore textureStore;

    // Per-frame-in-flight presentation synchronization: each frame slot owns
    // its acquire/render-finished semaphores and a GPU fence, so a fast host
    // never reuses GPU objects still pending from the previous frame.
    static constexpr std::uint32_t maxFramesInFlight = graphics_internal::Device::MaxFramesInFlight;
    graphics_internal::Device::FrameSync frameSync;
    std::uint32_t currentFrameSlot = 0;
    VkSemaphore acquireSemaphore = VK_NULL_HANDLE; // legacy single-slot, headless path
    VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE;

    // Quality pass resources. One scene uniform buffer + descriptor set per
    // frame-in-flight slot; shadow/reflection maps are stable shared images.
    graphics_internal::SceneBindings scene;
#if defined(APORIA_EXPERIMENTAL)
    // EXPERIMENTAL per-item multi-layer materials (set = 1).
    graphics_internal::MaterialStore materialStore;
    // EXPERIMENTAL worker pool used by the frame recorder.
    std::unique_ptr<graphics_internal::JobSystem> jobs;
    // EXPERIMENTAL separate command pool + buffer for the shadow pass, recorded
    // on a worker thread. A dedicated pool keeps the Vulkan external
    // synchronization rule (one pool, one recording thread) intact.
    VkCommandPool shadowCommandPool = VK_NULL_HANDLE;
    VkCommandBuffer shadowCommandBuffer = VK_NULL_HANDLE;
    bool shadowRecordedThisFrame = false;
#endif
    std::uint32_t shadowMapSize = 0;
    Aporia::ShadowConfig shadowConfig{};
    bool playerUltraShadows = false;

    // Temporal anti-aliasing: a resolve pass reprojects the previous frame with
    // the scene depth. The scene renders with a sub-pixel jitter so the history
    // accumulates into a supersampled image. Resources live in TaaRenderer.
    graphics_internal::TaaRenderer taa;
    float taaViewProj[16]{};
    float taaPrevViewProj[16]{};
    float taaInvViewProj[16]{};
    std::uint32_t taaFrameIndex = 0;
    bool hdrEnabled = false;
    std::uint32_t antiAliasing = 1;
    float antiAliasingStrength = 0.75f;
    bool reflectionsEnabled = false;
    int shadowTaps = 9;
    float reflectionScale = 0.0f;
    float shadowStrength = 0.35f;
    bool shadowsEnabled = true;
    double avgFrameMs = 0.0;
    Aporia::EnvironmentState environment{};
    Aporia::UiDrawList uiDrawList{};
    Aporia::BlurConfig blurConfig{};
    // Persistent text batch: laid-out glyphs are reused while the draw list
    // content is unchanged, so static HUD/menus skip layout and UBO upload.
    std::vector<graphics_internal::TextGlyph> cachedUiGlyphs;
    std::uint64_t cachedUiTextSignature = 0;
    bool cachedUiTextValid = false;

    VkFormat depthFormat = VK_FORMAT_UNDEFINED;
    std::vector<VkImageView> swapchainDepthViews;
    std::vector<VkDeviceMemory> swapchainDepthMemory;
    std::vector<VkImage> swapchainDepthImages;

    graphics_internal::MeshStore meshStore;
    graphics_internal::FontStore fontStore;

    struct PushConstants {
        float viewProj[16];
        float model[16];
        float color[4];
    };

    struct SceneUniforms {
        float sunViewProj[16];
        float sunDirection[4];
        float cameraPosition[4];
        float shadingParams[4];
        float screenParams[4];
        float shadowParams[4];
    };
    static_assert(sizeof(SceneUniforms) == 144, "SceneUniforms must stay one 144-byte block");

    CameraState pendingCamera{};
    float pendingViewProj[16]{};
    float pendingProjection[16]{};
    std::vector<RenderItem> pendingItems;
    bool drawDebugTriangle = true;

    // Headless main target size. Members shadow the 64x64 defaults for every
    // Impl member function; Graphics::readbackPixels reads them explicitly.
    std::uint32_t renderWidth = kDefaultHeadlessExtent;
    std::uint32_t renderHeight = kDefaultHeadlessExtent;

    VkPhysicalDeviceMemoryProperties memoryProperties{};
    VkFormat colorFormat = VK_FORMAT_UNDEFINED;
    std::uint64_t frameCount = 0;
    std::uint64_t presentCount = 0;
    std::uint32_t validationMessageCount = 0;
    bool headless = true;
    bool validationEnabled = false;
    std::string deviceName;
    std::string lastError;

    Aporia::GraphicsQuality activeQuality = Aporia::GraphicsQuality::Medium;
    float shadowExtent = 30.0f;

    // ---- inline helpers -----------------------------------------------------
    void setError(std::string message) {
        Aporia::graphics_internal::engineLog("[error] %s", message.c_str());
        lastError = std::move(message);
    }

    // Column-major 4x4 multiply: out = a * b (a, b, out are float[16]).
    static void multiplyMat4(const float* a, const float* b, float* out) noexcept {
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                float sum = 0.0f;
                for (int index = 0; index < 4; ++index) {
                    sum += a[index * 4 + row] * b[column * 4 + index];
                }
                out[column * 4 + row] = sum;
            }
        }
    }

    // Radical-inverse sequence used for the sub-pixel TAA jitter.
    static double halton(std::uint32_t index, std::uint32_t base) noexcept {
        double result = 0.0;
        double fraction = 1.0;
        while (index > 0) {
            fraction /= static_cast<double>(base);
            result += fraction * static_cast<double>(index % base);
            index /= base;
        }
        return result;
    }

    // Sun view-projection keeps the light camera aimed at the scene center;
    // the light travels opposite to the direction toward the sun.
    void buildSunMatrix(float outMatrix[16]) const noexcept {
        graphics_internal::ShadowRenderer::buildSunMatrix(
            environment.sunDirection, pendingCamera.position, shadowExtent, shadowMapSize, outMatrix);
    }

    // Camera mirrored across z = environment.mirrorZ for the planar reflection
    // pass (Z-up world). Forward direction comes from the view matrix third column.
    Mat4 reflectViewMatrix() const noexcept;

    // Column-major 4x4 inverse (gluInvertMatrix); returns false if singular.
    static bool invertMat4(const float* m, float* out) noexcept;

    // ---- members defined per domain ----------------------------------------
    void applyQualityPreset(Aporia::GraphicsQuality quality) noexcept;
    bool rebuildMeshPipelines();
    void destroyDepthResources() noexcept;
    void writeSceneUniforms(std::uint32_t slot, const float sunMatrix[16]);
    bool ensureDepthForExtent(std::uint32_t width, std::uint32_t height);
    bool createHdrRenderPass();
    bool createShadowResources();
    bool rebuildQualityTargets();
    void destroyQualityTargets() noexcept;
    bool configureNativeSurface();
    bool createRenderPass();
    void destroyHeadlessTarget() noexcept;
    bool createHeadlessTarget();
    void destroySwapchainResources() noexcept;
    bool createSwapchainResources();
    bool recreateSwapchain() noexcept;
    void renderShadowMap(VkCommandBuffer buffer, const float sunMatrix[16]);
    void renderUiText(VkCommandBuffer buffer, std::uint32_t width, std::uint32_t height);
    void renderTonemap(VkFramebuffer target, std::uint32_t width, std::uint32_t height);
    bool renderFrameCommands(VkFramebuffer targetFramebuffer, std::uint32_t width, std::uint32_t height) noexcept;
    void destroy() noexcept;
};

} // namespace Aporia
