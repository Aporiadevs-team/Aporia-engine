#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "Aporia/Graphics/Graphics.hpp"
#include "Device/Device.hpp"
#include "Support/Support.hpp"
#include "Text/FontAtlasBuilder.hpp"
#include "Hdr/HdrRenderer.hpp"
#include "Meshes/MeshRenderer.hpp"
#include "Rectangulars/RectRenderer.hpp"
#include "Shadows/ShadowRenderer.hpp"
#include "Text/TextRenderer.hpp"
#include "../TextureManager/TextureManager.hpp"
#include "Aporia/Graphics/MeshPrimitives.hpp"
#include "Aporia/Math/Matrix4.hpp"

#if defined(_WIN32) && !defined(VK_USE_PLATFORM_WIN32_KHR)
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#ifndef APORIA_VULKAN_SHADER_DIR
#define APORIA_VULKAN_SHADER_DIR "vulkan-shaders"
#endif

// Frame-level diagnostics: set APORIA_GRAPHICS_DEBUG=1 to trace acquire/extent/present.
namespace {
bool graphicsDebugEnabled() {
    char* value = nullptr;
    std::size_t valueLength = 0;
    static const bool enabled = _dupenv_s(&value, &valueLength, "APORIA_GRAPHICS_DEBUG") == 0 && value != nullptr;
    if (value != nullptr) {
        std::free(value);
    }
    return enabled;
}

// Engine-side file log: survives GUI hosts and redirected runs; flushed per line.
void engineLog(const char* format, ...) {
    FILE* logFile = nullptr;
    if (fopen_s(&logFile, "aporia_engine.log", "a") != 0 || logFile == nullptr) {
        return;
    }
    va_list arguments;
    va_start(arguments, format);
    std::vfprintf(logFile, format, arguments);
    va_end(arguments);
    std::fputc('\n', logFile);
    std::fflush(logFile);
    std::fclose(logFile);
}

void traceFrame(
    const char* stage,
    std::uint32_t swapWidth,
    std::uint32_t swapHeight,
    std::uint32_t windowWidth,
    std::uint32_t windowHeight,
    VkFormat format,
    std::uint32_t imageIndex,
    VkResult result) {
    if (!graphicsDebugEnabled()) {
        return;
    }
    std::fprintf(
        stderr,
        "[Aporia frame] %s swap=%ux%u window=%ux%u fmt=%d img=%u res=%d\n",
        stage,
        swapWidth,
        swapHeight,
        windowWidth,
        windowHeight,
        static_cast<int>(format),
        imageIndex,
        static_cast<int>(result));
    std::fflush(stderr);
}
}

namespace {

// Offscreen target edge used when GraphicsConfig leaves the headless size at 0.
constexpr std::uint32_t kDefaultHeadlessExtent = 64;

using Aporia::graphics_internal::qualityParameters;
using Aporia::graphics_internal::QualityParameters;
using Aporia::graphics_internal::chooseColorFormat;
using Aporia::graphics_internal::chooseHdrFormat;
using Aporia::graphics_internal::chooseDepthFormat;
using Aporia::graphics_internal::textureFormatInfo;
using Aporia::graphics_internal::readSpirv;
using Aporia::graphics_internal::chooseSurfaceFormat;
using Aporia::graphics_internal::choosePresentMode;
#if defined(_WIN32)
using Aporia::graphics_internal::getWindowExtent;
#endif

bool findMemoryType(
    const VkPhysicalDeviceMemoryProperties& properties,
    std::uint32_t typeBits,
    VkMemoryPropertyFlags required,
    std::uint32_t& type) noexcept {
    for (std::uint32_t index = 0; index < properties.memoryTypeCount; ++index) {
        if ((typeBits & (1u << index)) == 0) {
            continue;
        }
        if ((properties.memoryTypes[index].propertyFlags & required) == required) {
            type = index;
            return true;
        }
    }
    return false;
}

// Mirrors a world Z-up point across the plane z = mirrorZ.
void reflectPoint(double mirrorZ, double& x, double& y, double& z) noexcept {
    x = x;
    y = y;
    z = 2.0 * mirrorZ - z;
}

}


namespace Aporia {

struct Graphics::Impl {
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
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    VkShaderModule vertexShader = VK_NULL_HANDLE;
    VkShaderModule fragmentShader = VK_NULL_HANDLE;

    // Per-frame-in-flight presentation synchronization: each frame slot owns
    // its acquire/render-finished semaphores and a GPU fence, so a fast host
    // never reuses GPU objects still pending from the previous frame.
    static constexpr std::uint32_t maxFramesInFlight = graphics_internal::Device::MaxFramesInFlight;
    graphics_internal::Device::FrameSync frameSync;
    std::uint32_t currentFrameSlot = 0;
    VkSemaphore acquireSemaphore = VK_NULL_HANDLE; // legacy single-slot, headless path
    VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE;

    // Mesh pipeline: vertex data in buffers, camera in push constants.
    VkPipelineLayout meshPipelineLayout = VK_NULL_HANDLE;
    VkPipeline meshPipeline = VK_NULL_HANDLE;
    // Same mesh pipeline but created against the HDR render pass (Medium/High
    // presets render the main pass into the HDR target).
    VkPipeline hdrMeshPipeline = VK_NULL_HANDLE;
    VkRenderPass hdrRenderPass = VK_NULL_HANDLE;
    VkShaderModule meshVertexShader = VK_NULL_HANDLE;
    VkShaderModule meshFragmentShader = VK_NULL_HANDLE;

    // Quality pass resources. One scene uniform buffer + descriptor set per
    // frame-in-flight slot; shadow/reflection maps are stable shared images.
    static constexpr std::uint32_t sceneUniformCount = 2;
    VkBuffer sceneUniformBuffers[sceneUniformCount] = {};
    VkDeviceMemory sceneUniformMemory[sceneUniformCount] = {};
    VkDescriptorPool sceneDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout sceneDescriptorLayout = VK_NULL_HANDLE;
    VkDescriptorSet sceneDescriptorSets[sceneUniformCount] = {};
    VkPipeline shadowPipeline = VK_NULL_HANDLE;
    VkPipelineLayout shadowPipelineLayout = VK_NULL_HANDLE;
    VkShaderModule shadowVertexShader = VK_NULL_HANDLE;
    VkShaderModule shadowFragmentShader = VK_NULL_HANDLE;
    graphics_internal::ShadowRenderer::Resources shadow;
    VkSampler reflectSampler = VK_NULL_HANDLE;
    // Reflection target needs its own render pass: the shared main pass ends
    // in PRESENT_SRC_KHR in windowed mode, which cannot be sampled.
    VkRenderPass reflectRenderPass = VK_NULL_HANDLE;
    std::uint32_t shadowMapSize = 0;
    Aporia::ShadowConfig shadowConfig{};
    bool playerUltraShadows = false;
    VkPipeline tonemapPipeline = VK_NULL_HANDLE;
    VkPipelineLayout tonemapPipelineLayout = VK_NULL_HANDLE;
    VkShaderModule tonemapVertexShader = VK_NULL_HANDLE;
    VkShaderModule tonemapFragmentShader = VK_NULL_HANDLE;
    VkDescriptorPool tonemapDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout tonemapDescriptorLayout = VK_NULL_HANDLE;
    VkDescriptorSet tonemapDescriptorSet = VK_NULL_HANDLE;
    bool hdrEnabled = false;
    std::uint32_t antiAliasing = 1;
    bool reflectionsEnabled = false;
    int shadowTaps = 9;
    float reflectionScale = 0.0f;
    float shadowStrength = 0.35f;
    std::uint32_t textureScalePercent = 100;
    double avgFrameMs = 0.0;
    Aporia::EnvironmentState environment{};
    Aporia::UiDrawList uiDrawList{};
    Aporia::BlurConfig blurConfig{};
    // Persistent text batch: laid-out glyphs are reused while the draw list
    // content is unchanged, so static HUD/menus skip layout and UBO upload.
    std::vector<graphics_internal::TextGlyph> cachedUiGlyphs;
    std::uint64_t cachedUiTextSignature = 0;
    bool cachedUiTextValid = false;

    VkImage depthImage = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory = VK_NULL_HANDLE;
    VkImageView depthView = VK_NULL_HANDLE;
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;
    std::uint32_t depthWidth = 0;
    std::uint32_t depthHeight = 0;
    std::vector<VkImageView> swapchainDepthViews;
    std::vector<VkDeviceMemory> swapchainDepthMemory;
    std::vector<VkImage> swapchainDepthImages;

    using MeshResource = graphics_internal::MeshRenderer::Resource;

    struct PushConstants {
        float viewProj[16];
        float model[16];
        float color[4];
    };

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

    std::unordered_map<std::uint64_t, MeshResource> meshes;
    std::uint64_t nextMeshId = 1;
    CameraState pendingCamera{};
    float pendingViewProj[16]{};
    float pendingProjection[16]{};
    std::vector<RenderItem> pendingItems;
    bool drawDebugTriangle = true;

    // Camera mirrored across z = environment.mirrorZ for the planar
    // reflection pass (Z-up world). Forward direction comes from the view
    // matrix third column.
    Mat4 reflectViewMatrix() const noexcept {
        const double mirrorZ = static_cast<double>(environment.mirrorZ);
        const auto reflect = [mirrorZ](double z) { return 2.0 * mirrorZ - z; };
        const float forwardX = -pendingViewProj[8];
        const float forwardY = -pendingViewProj[9];
        const float forwardZ = -pendingViewProj[10];
        const Vec3 reflectedEye(
            pendingCamera.position[0],
            pendingCamera.position[1],
            reflect(static_cast<double>(pendingCamera.position[2])));
        const Vec3 target(
            pendingCamera.position[0] + forwardX * 100.0,
            pendingCamera.position[1] + forwardY * 100.0,
            reflect(static_cast<double>(pendingCamera.position[2]) + forwardZ * 100.0));
        const Vec3 up(0.0, 0.0, 1.0);
        Mat4 view = Mat4::lookAt(reflectedEye, target, up);
        // Mirrored view flips handedness; negate the x basis column.
        view.m[0][0] = -view.m[0][0];
        view.m[1][0] = -view.m[1][0];
        view.m[2][0] = -view.m[2][0];
        view.m[3][0] = -view.m[3][0];
        return view;
    }

    using TextureResource = graphics_internal::TextureManager::Resource;

    std::unordered_map<std::uint64_t, TextureResource> textures;
    TextureHandle defaultTexture;
    TextureHandle boundTexture;
    std::uint64_t nextTextureId = 1;
    std::uint64_t textureUploadCount = 0;

    struct FontResource {
        TextureHandle texture;
        Aporia::UiTextAtlasDesc atlas;
    };
    std::unordered_map<std::uint64_t, FontResource> fonts;
    std::uint64_t nextFontId = 1;

    // HDR scene target: all meshes render here; tonemap pass copies it to the
    // final target.    // final color target. At Low quality the mesh pass renders straight to the
    // final target and this image stays unused.
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

    // Headless main target size. Members shadow the 64x64 defaults for every
    // Impl member function; Graphics::readbackPixels reads them explicitly.
    std::uint32_t renderWidth = kDefaultHeadlessExtent;
    std::uint32_t renderHeight = kDefaultHeadlessExtent;


    // Planar reflection target: mirrored scene rendered from the reflected
    // camera. Sized reflectionScale relative to the main target.
    VkImage reflectImage = VK_NULL_HANDLE;
    VkDeviceMemory reflectMemory = VK_NULL_HANDLE;
    VkImageView reflectView = VK_NULL_HANDLE;
    VkFramebuffer reflectFramebuffer = VK_NULL_HANDLE;
    VkImage reflectDepthImage = VK_NULL_HANDLE;
    VkDeviceMemory reflectDepthMemory = VK_NULL_HANDLE;
    VkImageView reflectDepthView = VK_NULL_HANDLE;
    std::uint32_t reflectWidth = 0;
    std::uint32_t reflectHeight = 0;

    struct SceneUniforms {
        float sunViewProj[16];
        float sunDirection[4];
        float cameraPosition[4];
        float shadingParams[4];
        float screenParams[4];
        float shadowParams[4];
    };
    static_assert(sizeof(SceneUniforms) == 144, "SceneUniforms must stay one 144-byte block");

    VkPhysicalDeviceMemoryProperties memoryProperties{};
    VkFormat colorFormat = VK_FORMAT_UNDEFINED;
    std::uint64_t frameCount = 0;
    std::uint64_t presentCount = 0;
    std::uint32_t validationMessageCount = 0;
    bool headless = true;
    bool validationEnabled = false;
    std::string deviceName;
    std::string lastError;

    void setError(std::string message) {
        engineLog("[error] %s", message.c_str());
        lastError = std::move(message);
    }

    void applyQualityPreset(Aporia::GraphicsQuality quality) noexcept {
        const QualityParameters parameters = qualityParameters(quality);
        shadowMapSize = parameters.shadowMapSize;
        shadowTaps = parameters.shadowTaps;
        hdrEnabled = parameters.hdr;
        reflectionsEnabled = parameters.reflections;
        reflectionScale = parameters.reflectionScale;
        shadowStrength = parameters.shadowStrength;
        textureScalePercent = parameters.textureScalePercent;
        shadowConfig.mapSize = parameters.shadowMapSize;
        shadowConfig.cascadeCount = 1;
        shadowConfig.passCount = 1;
        shadowConfig.pcfRadius = parameters.shadowTaps <= 1 ? 0u : parameters.shadowTaps <= 9 ? 1u : 3u;
        shadowConfig.strength = parameters.shadowStrength;
        shadowConfig.filter = quality == Aporia::GraphicsQuality::High ? Aporia::ShadowFilter::Pcss : Aporia::ShadowFilter::Pcf;
        shadowConfig.contactShadows = quality != Aporia::GraphicsQuality::Low;
        shadowConfig.contactDistance = quality == Aporia::GraphicsQuality::High ? 3.0f : 1.5f;
        activeQuality = quality;
        hdrRenderer.setQuality(quality);
        shadowRenderer.setConfig(shadowConfig);
    }

    Aporia::GraphicsQuality activeQuality = Aporia::GraphicsQuality::Medium;
    float shadowExtent = 30.0f;

    bool createBufferWithData(
        VkDeviceSize size,
        const void* data,
        VkBufferUsageFlags usage,
        VkBuffer& buffer,
        VkDeviceMemory& memory) {
        graphics_internal::MeshRenderer::UploadContext context;
        context.device = device;
        context.memoryProperties = memoryProperties;
        context.commandBuffer = commandBuffer;
        context.queue = queue;
        if (!graphics_internal::MeshRenderer::uploadBuffer(
                context, size, data, usage, buffer, memory)) {
            setError("Buffer upload failed");
            return false;
        }
        return true;
    }


    VkFormat chooseDepthFormat() noexcept {
        return graphics_internal::chooseDepthFormat(physicalDevice);
    }


    void destroyDepthResources() noexcept {
        if (device != VK_NULL_HANDLE) {
            if (depthView != VK_NULL_HANDLE) {
                vkDestroyImageView(device, depthView, nullptr);
            }
            if (depthImage != VK_NULL_HANDLE) {
                vkDestroyImage(device, depthImage, nullptr);
            }
            if (depthMemory != VK_NULL_HANDLE) {
                vkFreeMemory(device, depthMemory, nullptr);
            }
        }
        for (VkImageView view : swapchainDepthViews) {
            if (device != VK_NULL_HANDLE) {
                vkDestroyImageView(device, view, nullptr);
            }
        }
        for (VkImage image : swapchainDepthImages) {
            if (device != VK_NULL_HANDLE) {
                vkDestroyImage(device, image, nullptr);
            }
        }
        for (VkDeviceMemory memory : swapchainDepthMemory) {
            if (device != VK_NULL_HANDLE) {
                vkFreeMemory(device, memory, nullptr);
            }
        }
        swapchainDepthViews.clear();
        swapchainDepthImages.clear();
        swapchainDepthMemory.clear();
        depthImage = VK_NULL_HANDLE;
        depthMemory = VK_NULL_HANDLE;
        depthView = VK_NULL_HANDLE;
        depthWidth = 0;
        depthHeight = 0;
    }

    bool createDepthImage(VkExtent2D extent, VkImage& image, VkDeviceMemory& memory, VkImageView& view) {
        if (!graphics_internal::HdrRenderer::createDepthImage(
                device, memoryProperties, depthFormat, extent, image, memory, view)) {
            setError("Depth image creation failed");
            return false;
        }
        return true;
    }


    bool ensureDepthForExtent(std::uint32_t width, std::uint32_t height) {
        if (depthFormat == VK_FORMAT_UNDEFINED) {
            depthFormat = chooseDepthFormat();
            if (depthFormat == VK_FORMAT_UNDEFINED) {
                setError("No supported depth attachment format");
                return false;
            }
        }
        if (depthImage != VK_NULL_HANDLE && depthWidth == width && depthHeight == height) {
            return true;
        }
        if (depthImage != VK_NULL_HANDLE) {
            vkDestroyImageView(device, depthView, nullptr);
            vkDestroyImage(device, depthImage, nullptr);
            vkFreeMemory(device, depthMemory, nullptr);
            depthImage = VK_NULL_HANDLE;
            depthMemory = VK_NULL_HANDLE;
            depthView = VK_NULL_HANDLE;
        }
        if (!createDepthImage({width, height}, depthImage, depthMemory, depthView)) {
            return false;
        }
        depthWidth = width;
        depthHeight = height;
        return true;
    }

    // ---------- quality pass resources ----------

    void destroyQualityTargets() noexcept {
        if (device != VK_NULL_HANDLE) {
            if (hdrFramebuffer != VK_NULL_HANDLE) {
                vkDestroyFramebuffer(device, hdrFramebuffer, nullptr);
            }
            if (hdrSampler != VK_NULL_HANDLE) {
                vkDestroySampler(device, hdrSampler, nullptr);
            }
            if (hdrDepthView != VK_NULL_HANDLE) {
                vkDestroyImageView(device, hdrDepthView, nullptr);
            }
            if (hdrDepthImage != VK_NULL_HANDLE) {
                vkDestroyImage(device, hdrDepthImage, nullptr);
            }
            if (hdrDepthMemory != VK_NULL_HANDLE) {
                vkFreeMemory(device, hdrDepthMemory, nullptr);
            }
            if (hdrView != VK_NULL_HANDLE) {
                vkDestroyImageView(device, hdrView, nullptr);
            }
            if (hdrImage != VK_NULL_HANDLE) {
                vkDestroyImage(device, hdrImage, nullptr);
            }
            if (hdrMemory != VK_NULL_HANDLE) {
                vkFreeMemory(device, hdrMemory, nullptr);
            }
            if (reflectFramebuffer != VK_NULL_HANDLE) {
                vkDestroyFramebuffer(device, reflectFramebuffer, nullptr);
            }
            if (reflectDepthView != VK_NULL_HANDLE) {
                vkDestroyImageView(device, reflectDepthView, nullptr);
            }
            if (reflectDepthImage != VK_NULL_HANDLE) {
                vkDestroyImage(device, reflectDepthImage, nullptr);
            }
            if (reflectDepthMemory != VK_NULL_HANDLE) {
                vkFreeMemory(device, reflectDepthMemory, nullptr);
            }
            if (reflectView != VK_NULL_HANDLE) {
                vkDestroyImageView(device, reflectView, nullptr);
            }
            if (reflectImage != VK_NULL_HANDLE) {
                vkDestroyImage(device, reflectImage, nullptr);
            }
            if (reflectMemory != VK_NULL_HANDLE) {
                vkFreeMemory(device, reflectMemory, nullptr);
            }
            graphics_internal::ShadowRenderer::destroyResources(device, shadow);
            if (reflectRenderPass != VK_NULL_HANDLE) {
                vkDestroyRenderPass(device, reflectRenderPass, nullptr);
            }
            if (reflectSampler != VK_NULL_HANDLE) {
                vkDestroySampler(device, reflectSampler, nullptr);
            }
            for (std::uint32_t slot = 0; slot < sceneUniformCount; ++slot) {
                if (sceneUniformBuffers[slot] != VK_NULL_HANDLE) {
                    vkDestroyBuffer(device, sceneUniformBuffers[slot], nullptr);
                    sceneUniformBuffers[slot] = VK_NULL_HANDLE;
                }
                if (sceneUniformMemory[slot] != VK_NULL_HANDLE) {
                    vkFreeMemory(device, sceneUniformMemory[slot], nullptr);
                    sceneUniformMemory[slot] = VK_NULL_HANDLE;
                }
            }
            if (sceneDescriptorPool != VK_NULL_HANDLE) {
                // Pool is rebuilt each time; the layout outlives it because
                // mesh/shadow pipeline layouts reference it.
                vkDestroyDescriptorPool(device, sceneDescriptorPool, nullptr);
            }
            if (tonemapDescriptorPool != VK_NULL_HANDLE) {
                // Pool is owned by initialize/destroy; rebuild only reclaims
                // the tonemap descriptor set allocated from it.
                vkResetDescriptorPool(device, tonemapDescriptorPool, 0);
            }
        }
        hdrImage = VK_NULL_HANDLE;
        hdrMemory = VK_NULL_HANDLE;
        hdrView = VK_NULL_HANDLE;
        hdrSampler = VK_NULL_HANDLE;
        hdrFramebuffer = VK_NULL_HANDLE;
        hdrDepthImage = VK_NULL_HANDLE;
        hdrDepthMemory = VK_NULL_HANDLE;
        hdrDepthView = VK_NULL_HANDLE;
        hdrWidth = 0;
        hdrHeight = 0;
        reflectImage = VK_NULL_HANDLE;
        reflectMemory = VK_NULL_HANDLE;
        reflectView = VK_NULL_HANDLE;
        reflectFramebuffer = VK_NULL_HANDLE;
        reflectDepthImage = VK_NULL_HANDLE;
        reflectDepthMemory = VK_NULL_HANDLE;
        reflectDepthView = VK_NULL_HANDLE;
        reflectWidth = 0;
        reflectHeight = 0;
        shadow = {};
        reflectSampler = VK_NULL_HANDLE;
        reflectRenderPass = VK_NULL_HANDLE;
        // shadowMapSize is preset configuration, not a GPU target; it must
        // survive rebuilds so createShadowResources() sees the right size.
        sceneDescriptorPool = VK_NULL_HANDLE;
        for (std::uint32_t slot = 0; slot < sceneUniformCount; ++slot) {
            sceneDescriptorSets[slot] = VK_NULL_HANDLE;
        }
        tonemapDescriptorSet = VK_NULL_HANDLE;
    }

    // Offscreen color target with its own depth attachment, compatible with
    // the main depth-bearing render pass.
    bool createColorTarget(
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
        VkDeviceMemory& outDepthMemory) {
        if (!graphics_internal::HdrRenderer::createColorTarget(
                device, memoryProperties, depthFormat, format, extraUsage, width, height,
                compatibleRenderPass, outView, outFramebuffer, outImage, outMemory,
                outDepthView, outDepthImage, outDepthMemory)) {
            setError("Quality target creation failed");
            return false;
        }
        return true;
    }


    // Reflection target render pass: identical to the main one but always
    // ends in COLOR_ATTACHMENT_OPTIMAL so the image can be sampled later.
    bool createReflectRenderPass() {
        if (reflectRenderPass != VK_NULL_HANDLE) {
            return true;
        }
        if (depthFormat == VK_FORMAT_UNDEFINED) {
            depthFormat = chooseDepthFormat();
            if (depthFormat == VK_FORMAT_UNDEFINED) {
                setError("No supported depth attachment format");
                return false;
            }
        }
        if (!graphics_internal::HdrRenderer::createRenderPass(
                device, colorFormat, depthFormat, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, reflectRenderPass)) {
            setError("vkCreateRenderPass(reflect) failed");
            return false;
        }
        return true;
    }


    bool createShadowResources() {
        if (depthFormat == VK_FORMAT_UNDEFINED) {
            depthFormat = chooseDepthFormat();
            if (depthFormat == VK_FORMAT_UNDEFINED) {
                setError("No supported depth attachment format");
                return false;
            }
        }
        if (!graphics_internal::ShadowRenderer::createResources(
                device, memoryProperties, depthFormat, shadowMapSize, shadow)) {
            setError("Shadow map resource creation failed");
            return false;
        }

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.maxLod = 1.0f;
        if (vkCreateSampler(device, &samplerInfo, nullptr, &reflectSampler) != VK_SUCCESS) {
            setError("vkCreateSampler(reflection) failed");
            return false;
        }
        return true;
    }


    bool createSceneDescriptorLayout() {
        // Matches the shader declaration in mesh.frag/mesh.vert:
        // 0 = albedo sampler, 1 = shadow map, 2 = reflection map,
        // 3 = SceneUniforms UBO.
        VkDescriptorSetLayoutBinding bindings[4]{};
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[2].binding = 2;
        bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[3].binding = 3;
        bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[3].descriptorCount = 1;
        bindings[3].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 4;
        layoutInfo.pBindings = bindings;
        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &sceneDescriptorLayout) != VK_SUCCESS) {
            setError("vkCreateDescriptorSetLayout(scene) failed");
            return false;
        }
        return true;
    }

    bool createSceneUniforms() {
        if (sceneDescriptorLayout == VK_NULL_HANDLE) {
            if (!createSceneDescriptorLayout()) {
                return false;
            }
        }
        VkDescriptorPoolSize poolSizes[2]{};
        poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSizes[0].descriptorCount = sceneUniformCount * 2;
        poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSizes[1].descriptorCount = sceneUniformCount * 2;
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = sceneUniformCount;
        poolInfo.poolSizeCount = 2;
        poolInfo.pPoolSizes = poolSizes;
        if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &sceneDescriptorPool) != VK_SUCCESS) {
            setError("vkCreateDescriptorPool(scene) failed");
            return false;
        }

        const VkDeviceSize uniformSize = sizeof(SceneUniforms);
        for (std::uint32_t slot = 0; slot < sceneUniformCount; ++slot) {
            VkBufferCreateInfo bufferInfo{};
            bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferInfo.size = uniformSize;
            bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
            bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            if (vkCreateBuffer(device, &bufferInfo, nullptr, &sceneUniformBuffers[slot]) != VK_SUCCESS) {
                setError("vkCreateBuffer(scene uniforms) failed");
                return false;
            }
            VkMemoryRequirements requirements{};
            vkGetBufferMemoryRequirements(device, sceneUniformBuffers[slot], &requirements);
            std::uint32_t memoryType = 0;
            if (!findMemoryType(
                    memoryProperties,
                    requirements.memoryTypeBits,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    memoryType)) {
                setError("No host-visible memory for scene uniforms");
                return false;
            }
            VkMemoryAllocateInfo allocateInfo{};
            allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocateInfo.allocationSize = requirements.size;
            allocateInfo.memoryTypeIndex = memoryType;
            if (vkAllocateMemory(device, &allocateInfo, nullptr, &sceneUniformMemory[slot]) != VK_SUCCESS) {
                setError("vkAllocateMemory(scene uniforms) failed");
                return false;
            }
            if (vkBindBufferMemory(device, sceneUniformBuffers[slot], sceneUniformMemory[slot], 0) != VK_SUCCESS) {
                setError("vkBindBufferMemory(scene uniforms) failed");
                return false;
            }

            VkDescriptorSetAllocateInfo allocateSetInfo{};
            allocateSetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocateSetInfo.descriptorPool = sceneDescriptorPool;
            allocateSetInfo.descriptorSetCount = 1;
            allocateSetInfo.pSetLayouts = &sceneDescriptorLayout;
            if (vkAllocateDescriptorSets(device, &allocateSetInfo, &sceneDescriptorSets[slot]) != VK_SUCCESS) {
                setError("vkAllocateDescriptorSets(scene) failed");
                return false;
            }

            VkDescriptorBufferInfo bufferDescriptor{};
            bufferDescriptor.buffer = sceneUniformBuffers[slot];
            bufferDescriptor.offset = 0;
            bufferDescriptor.range = uniformSize;

            const auto defaultIt = textures.find(defaultTexture.value());
            const auto textureIt = textures.find(boundTexture.value());
            const auto& albedoResource = textureIt != textures.end() ? textureIt->second : defaultIt->second;

            VkDescriptorImageInfo albedoDescriptor{};
            albedoDescriptor.sampler = albedoResource.sampler;
            albedoDescriptor.imageView = albedoResource.view;
            albedoDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            VkDescriptorImageInfo shadowDescriptor{};
            shadowDescriptor.sampler = shadow.sampler;
            shadowDescriptor.imageView = shadow.view;
            shadowDescriptor.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            std::array<VkWriteDescriptorSet, 4> writes{};
            writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].dstSet = sceneDescriptorSets[slot];
            writes[0].dstBinding = 0;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[0].descriptorCount = 1;
            writes[0].pImageInfo = &albedoDescriptor;
            writes[1] = writes[0];
            writes[1].dstBinding = 1;
            writes[1].pImageInfo = &shadowDescriptor;
            writes[2] = writes[0];
            writes[2].dstBinding = 2;
            // Reflection map descriptor is filled in after the reflect target
            // exists (see rebuildQualityTargets); a null view is invalid, so
            // reuse the albedo texture until then.
            writes[2].pImageInfo = &albedoDescriptor;
            writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[3].dstSet = sceneDescriptorSets[slot];
            writes[3].dstBinding = 3;
            writes[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[3].descriptorCount = 1;
            writes[3].pBufferInfo = &bufferDescriptor;
            vkUpdateDescriptorSets(device, 4, writes.data(), 0, nullptr);
        }
        return true;
    }

    void refreshSceneTextureBinding() {
        if (sceneDescriptorPool == VK_NULL_HANDLE) {
            return;
        }
        const auto textureIt = textures.find(boundTexture.value());
        if (textureIt == textures.end() || textureIt->second.view == VK_NULL_HANDLE) {
            return;
        }
        for (std::uint32_t slot = 0; slot < sceneUniformCount; ++slot) {
            if (sceneDescriptorSets[slot] == VK_NULL_HANDLE) {
                continue;
            }
            VkDescriptorImageInfo albedoDescriptor{};
            albedoDescriptor.sampler = textureIt->second.sampler;
            albedoDescriptor.imageView = textureIt->second.view;
            albedoDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = sceneDescriptorSets[slot];
            write.dstBinding = 0;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.descriptorCount = 1;
            write.pImageInfo = &albedoDescriptor;
            vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        }
    }

    bool uploadTextureData(
        TextureResource& texture,
        const void* pixels,
        std::size_t dataSize,
        VkImageLayout oldLayout) {
        graphics_internal::TextureManager::UploadContext context;
        context.device = device;
        context.memoryProperties = memoryProperties;
        context.commandBuffer = commandBuffer;
        context.queue = queue;
        if (!graphics_internal::TextureManager::uploadTexture(
                context, texture, pixels, dataSize, oldLayout)) {
            setError("Texture upload failed");
            return false;
        }
        ++textureUploadCount;
        return true;
    }


    bool createTextureResource(const TextureDesc& description, TextureResource& texture) {
        if (device == VK_NULL_HANDLE || commandBuffer == VK_NULL_HANDLE ||
            description.width == 0 || description.height == 0 || description.pixels == nullptr ||
            description.dataSize == 0) {
            setError("Texture description is invalid");
            return false;
        }

        VkFormat format = VK_FORMAT_UNDEFINED;
        std::uint32_t bytesPerPixel = 0;
        if (!textureFormatInfo(description.format, format, bytesPerPixel)) {
            setError("Texture format is unsupported");
            return false;
        }
        if (description.width > 16384 || description.height > 16384 ||
            description.width > std::numeric_limits<std::size_t>::max() / description.height ||
            description.width * description.height > std::numeric_limits<std::size_t>::max() / bytesPerPixel ||
            description.dataSize != description.width * description.height * bytesPerPixel) {
            setError("Texture dimensions or data size are invalid");
            return false;
        }

        VkFormatProperties formatProperties{};
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &formatProperties);
        if ((formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) == 0 ||
            (formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_TRANSFER_DST_BIT) == 0) {
            setError("Texture format does not support sampling and uploads");
            return false;
        }

        graphics_internal::TextureManager::CreateContext context;
        context.device = device;
        context.physicalDevice = physicalDevice;
        context.memoryProperties = memoryProperties;
        context.commandBuffer = commandBuffer;
        context.queue = queue;
        if (!graphics_internal::TextureManager::createResource(
                context, description, format, bytesPerPixel, textureScalePercent, texture)) {
            setError("Texture resource creation failed");
            return false;
        }
        ++textureUploadCount;
        return true;
    }


    bool createDefaultTexture() {
        constexpr std::uint8_t pixels[] = {255, 255, 255, 255};
        TextureDesc description;
        description.width = 1;
        description.height = 1;
        description.format = TextureFormat::Rgba8Unorm;
        description.pixels = pixels;
        description.dataSize = sizeof(pixels);

        TextureResource texture;
        if (!createTextureResource(description, texture)) {
            return false;
        }
        const std::uint64_t id = nextTextureId++;
        try {
            const auto result = textures.emplace(id, std::move(texture));
            if (!result.second) {
                setError("Default texture handle allocation failed");
                return false;
            }
        } catch (...) {
            setError("Default texture allocation failed");
            return false;
        }
        defaultTexture = TextureHandle{id};
        boundTexture = defaultTexture;
        return true;
    }

    bool createDescriptorResources() {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &binding;
        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
            setError("vkCreateDescriptorSetLayout failed");
            return false;
        }

        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSize.descriptorCount = 1;
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
            setError("vkCreateDescriptorPool failed");
            return false;
        }

        VkDescriptorSetAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocateInfo.descriptorPool = descriptorPool;
        allocateInfo.descriptorSetCount = 1;
        allocateInfo.pSetLayouts = &descriptorSetLayout;
        if (vkAllocateDescriptorSets(device, &allocateInfo, &descriptorSet) != VK_SUCCESS) {
            setError("vkAllocateDescriptorSets failed");
            return false;
        }

        const auto textureIt = textures.find(defaultTexture.value());
        if (textureIt == textures.end()) {
            setError("Default texture is unavailable");
            return false;
        }
        VkDescriptorImageInfo imageInfo{};
        imageInfo.sampler = textureIt->second.sampler;
        imageInfo.imageView = textureIt->second.view;
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = descriptorSet;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &imageInfo;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        return true;
    }

    void destroyDescriptorResources() noexcept {
        if (device != VK_NULL_HANDLE) {
            if (descriptorPool != VK_NULL_HANDLE) {
                vkDestroyDescriptorPool(device, descriptorPool, nullptr);
            }
            if (descriptorSetLayout != VK_NULL_HANDLE) {
                vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
            }
        }
        descriptorPool = VK_NULL_HANDLE;
        descriptorSetLayout = VK_NULL_HANDLE;
        descriptorSet = VK_NULL_HANDLE;
    }

    bool bindTextureDescriptor(TextureHandle handle) {
        if (descriptorSet == VK_NULL_HANDLE || !handle.valid()) {
            setError("Texture descriptor is not available");
            return false;
        }
        const auto textureIt = textures.find(handle.value());
        if (textureIt == textures.end()) {
            setError("Texture handle is invalid");
            return false;
        }

        VkDescriptorImageInfo imageInfo{};
        imageInfo.sampler = textureIt->second.sampler;
        imageInfo.imageView = textureIt->second.view;
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = descriptorSet;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &imageInfo;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        boundTexture = handle;
        refreshSceneTextureBinding();
        return true;
    }

    // Sun view-projection keeps the light camera aimed at the scene center;
    // the light travels opposite to the direction toward the sun.
    void buildSunMatrix(float outMatrix[16]) const noexcept {
        graphics_internal::ShadowRenderer::buildSunMatrix(
            environment.sunDirection, pendingCamera.position, shadowExtent, shadowMapSize, outMatrix);
    }


    void writeSceneUniforms(std::uint32_t slot, const float sunMatrix[16]) {
        if (sceneUniformMemory[slot] == VK_NULL_HANDLE) {
            return;
        }
        void* mapped = nullptr;
        if (vkMapMemory(device, sceneUniformMemory[slot], 0, sizeof(SceneUniforms), 0, &mapped) != VK_SUCCESS || mapped == nullptr) {
            return;
        }
        SceneUniforms uniforms{};
        std::memcpy(uniforms.sunViewProj, sunMatrix, sizeof(uniforms.sunViewProj));
        uniforms.sunDirection[0] = environment.sunDirection[0];
        uniforms.sunDirection[1] = environment.sunDirection[1];
        uniforms.sunDirection[2] = environment.sunDirection[2];
        uniforms.sunDirection[3] = environment.sunIntensity;
        uniforms.cameraPosition[0] = static_cast<float>(pendingCamera.position[0]);
        uniforms.cameraPosition[1] = static_cast<float>(pendingCamera.position[1]);
        uniforms.cameraPosition[2] = static_cast<float>(pendingCamera.position[2]);
        uniforms.shadingParams[0] = 0.32f; // ambient
        uniforms.shadingParams[1] = shadowStrength;
        uniforms.shadingParams[2] = environment.mirrorZ;
        uniforms.shadingParams[3] = shadowMapSize > 0 ? 1.0f / static_cast<float>(shadowMapSize) : 0.0f;
        uniforms.screenParams[0] = static_cast<float>(hdrEnabled ? hdrWidth : (headless ? renderWidth : swapchainExtent.width));
        uniforms.screenParams[1] = static_cast<float>(hdrEnabled ? hdrHeight : (headless ? renderHeight : swapchainExtent.height));
        uniforms.shadowParams[0] = static_cast<float>(shadowConfig.filter);
        uniforms.shadowParams[1] = static_cast<float>(shadowConfig.blockerSamples);
        uniforms.shadowParams[2] = static_cast<float>(shadowConfig.filterSamples);
        uniforms.shadowParams[3] = shadowConfig.contactShadows ? shadowConfig.contactDistance : 0.0f;
        std::memcpy(mapped, &uniforms, sizeof(uniforms));
        vkUnmapMemory(device, sceneUniformMemory[slot]);
    }

    // (Re)creates HDR/shadow/reflection targets for the current preset and
    // main target size. Main target size comes from the headless constants or
    // the swapchain extent.
    bool rebuildQualityTargets() {
        const std::uint32_t mainWidth = headless ? renderWidth : swapchainExtent.width;
        const std::uint32_t mainHeight = headless ? renderHeight : swapchainExtent.height;
        if (mainWidth == 0 || mainHeight == 0) {
            setError("Quality targets require a valid main target size");
            return false;
        }

        vkDeviceWaitIdle(device);
        destroyQualityTargets();

        if (hdrFormat == VK_FORMAT_UNDEFINED) {
            hdrFormat = chooseHdrFormat(physicalDevice);
            if (hdrFormat == VK_FORMAT_UNDEFINED) {
                setError("No supported HDR color format");
                return false;
            }
        }

        if (hdrEnabled) {
            if (!createColorTarget(
                    hdrFormat,
                    VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                    mainWidth,
                    mainHeight,
                    hdrRenderPass != VK_NULL_HANDLE ? hdrRenderPass : renderPass,
                    hdrView,
                    hdrFramebuffer,
                    hdrImage,
                    hdrMemory,
                    hdrDepthView,
                    hdrDepthImage,
                    hdrDepthMemory)) {
                return false;
            }
            hdrWidth = mainWidth;
            hdrHeight = mainHeight;

            VkSamplerCreateInfo samplerInfo{};
            samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            samplerInfo.magFilter = VK_FILTER_LINEAR;
            samplerInfo.minFilter = VK_FILTER_LINEAR;
            samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.maxLod = 1.0f;
            if (vkCreateSampler(device, &samplerInfo, nullptr, &hdrSampler) != VK_SUCCESS) {
                setError("vkCreateSampler(hdr) failed");
                return false;
            }

            // Tonemap pass samples the HDR target.
            VkDescriptorSetAllocateInfo allocateSetInfo{};
            allocateSetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocateSetInfo.descriptorPool = tonemapDescriptorPool;
            allocateSetInfo.descriptorSetCount = 1;
            allocateSetInfo.pSetLayouts = &tonemapDescriptorLayout;
            if (vkAllocateDescriptorSets(device, &allocateSetInfo, &tonemapDescriptorSet) != VK_SUCCESS) {
                setError("vkAllocateDescriptorSets(tonemap) failed");
                return false;
            }
            VkDescriptorImageInfo hdrDescriptor{};
            hdrDescriptor.sampler = hdrSampler;
            hdrDescriptor.imageView = hdrView;
            hdrDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = tonemapDescriptorSet;
            write.dstBinding = 0;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.descriptorCount = 1;
            write.pImageInfo = &hdrDescriptor;
            vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        }

        if (reflectionsEnabled) {
            if (!createReflectRenderPass()) {
                return false;
            }
            const std::uint32_t reflectTargetWidth = std::max(1u, static_cast<std::uint32_t>(
                static_cast<float>(mainWidth) * reflectionScale));
            const std::uint32_t reflectTargetHeight = std::max(1u, static_cast<std::uint32_t>(
                static_cast<float>(mainHeight) * reflectionScale));
            if (!createColorTarget(
                    colorFormat,
                    0,
                    reflectTargetWidth,
                    reflectTargetHeight,
                    reflectRenderPass != VK_NULL_HANDLE ? reflectRenderPass : renderPass,
                    reflectView,
                    reflectFramebuffer,
                    reflectImage,
                    reflectMemory,
                    reflectDepthView,
                    reflectDepthImage,
                    reflectDepthMemory)) {
                return false;
            }
            reflectWidth = reflectTargetWidth;
            reflectHeight = reflectTargetHeight;
        }

        if (!createShadowResources()) {
            return false;
        }
        if (!createSceneUniforms()) {
            return false;
        }
        refreshSceneTextureBinding();
        if (reflectionsEnabled && reflectView != VK_NULL_HANDLE) {
            for (std::uint32_t slot = 0; slot < sceneUniformCount; ++slot) {
                if (sceneDescriptorSets[slot] == VK_NULL_HANDLE) {
                    continue;
                }
                VkDescriptorImageInfo reflectDescriptor{};
                reflectDescriptor.sampler = reflectSampler;
                reflectDescriptor.imageView = reflectView;
                reflectDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                VkWriteDescriptorSet write{};
                write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.dstSet = sceneDescriptorSets[slot];
                write.dstBinding = 2;
                write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                write.descriptorCount = 1;
                write.pImageInfo = &reflectDescriptor;
                vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
            }
        }
        return true;
    }

    bool configureNativeSurface() {
#if !defined(_WIN32)
        setError("Native Vulkan windows are currently supported only on Windows");
        return false;
#else
        if (surface == VK_NULL_HANDLE || window == nullptr) {
            setError("Native Vulkan surface is not configured");
            return false;
        }
        if (getPhysicalDeviceSurfaceSupport == nullptr ||
            getPhysicalDeviceSurfaceCapabilities == nullptr ||
            getPhysicalDeviceSurfaceFormats == nullptr ||
            getPhysicalDeviceSurfacePresentModes == nullptr) {
            setError("Vulkan surface extension entry points are unavailable");
            return false;
        }

        VkBool32 supported = VK_FALSE;
        if (getPhysicalDeviceSurfaceSupport(physicalDevice, graphicsFamily, surface, &supported) != VK_SUCCESS || supported != VK_TRUE) {
            setError("Vulkan graphics queue does not support the native surface");
            return false;
        }

        VkSurfaceFormatKHR selectedFormat{};
        if (!chooseSurfaceFormat(physicalDevice, surface, selectedFormat)) {
            setError("No supported Vulkan surface format");
            return false;
        }
        surfaceFormat = selectedFormat;
        swapchainColorSpace = selectedFormat.colorSpace;
        colorFormat = selectedFormat.format;
        presentMode = choosePresentMode(physicalDevice, surface, deviceService.vsync);
        return true;
#endif
    }

    bool createRenderPass() {
        if (depthFormat == VK_FORMAT_UNDEFINED) {
            depthFormat = chooseDepthFormat();
            if (depthFormat == VK_FORMAT_UNDEFINED) {
                setError("No supported depth attachment format");
                return false;
            }
        }
        const VkImageLayout finalLayout = headless
            ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
            : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        if (!graphics_internal::HdrRenderer::createRenderPass(
                device, colorFormat, depthFormat, finalLayout, renderPass)) {
            setError("vkCreateRenderPass failed");
            return false;
        }
        return true;
    }


    // HDR render pass: same structure as the main one, HDR color format.
    bool createHdrRenderPass() {
        if (hdrRenderPass != VK_NULL_HANDLE) {
            return true;
        }
        if (depthFormat == VK_FORMAT_UNDEFINED) {
            depthFormat = chooseDepthFormat();
            if (depthFormat == VK_FORMAT_UNDEFINED) {
                setError("No supported depth attachment format");
                return false;
            }
        }
        if (!graphics_internal::HdrRenderer::createRenderPass(
                device, hdrFormat, depthFormat, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, hdrRenderPass)) {
            setError("vkCreateRenderPass(hdr) failed");
            return false;
        }
        return true;
    }


    void destroyHeadlessTarget() noexcept {
        if (device == VK_NULL_HANDLE) {
            return;
        }
        if (framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device, framebuffer, nullptr);
            framebuffer = VK_NULL_HANDLE;
        }
        if (colorView != VK_NULL_HANDLE) {
            vkDestroyImageView(device, colorView, nullptr);
            colorView = VK_NULL_HANDLE;
        }
        if (colorImage != VK_NULL_HANDLE) {
            vkDestroyImage(device, colorImage, nullptr);
            colorImage = VK_NULL_HANDLE;
        }
        if (colorMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, colorMemory, nullptr);
            colorMemory = VK_NULL_HANDLE;
        }
    }

    bool createHeadlessTarget() {
        if (!ensureDepthForExtent(renderWidth, renderHeight)) {
            return false;
        }

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = colorFormat;
        imageInfo.extent = {renderWidth, renderHeight, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(device, &imageInfo, nullptr, &colorImage) != VK_SUCCESS) {
            setError("vkCreateImage failed");
            return false;
        }

        VkMemoryRequirements imageRequirements{};
        vkGetImageMemoryRequirements(device, colorImage, &imageRequirements);
        std::uint32_t memoryType = 0;
        if (!findMemoryType(memoryProperties, imageRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memoryType)) {
            setError("No device-local memory type for Vulkan color image");
            return false;
        }

        VkMemoryAllocateInfo memoryInfo{};
        memoryInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memoryInfo.allocationSize = imageRequirements.size;
        memoryInfo.memoryTypeIndex = memoryType;
        if (vkAllocateMemory(device, &memoryInfo, nullptr, &colorMemory) != VK_SUCCESS) {
            setError("vkAllocateMemory failed");
            return false;
        }
        if (vkBindImageMemory(device, colorImage, colorMemory, 0) != VK_SUCCESS) {
            setError("vkBindImageMemory failed");
            return false;
        }

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = colorImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = colorFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        if (vkCreateImageView(device, &viewInfo, nullptr, &colorView) != VK_SUCCESS) {
            setError("vkCreateImageView failed");
            return false;
        }

        std::array<VkImageView, 2> attachments{colorView, depthView};
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = static_cast<std::uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = renderWidth;
        framebufferInfo.height = renderHeight;
        framebufferInfo.layers = 1;
        if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &framebuffer) != VK_SUCCESS) {
            setError("vkCreateFramebuffer failed");
            return false;
        }
        return true;
    }

    void destroySwapchainResources() noexcept {
        if (device == VK_NULL_HANDLE) {
            swapchainImages.clear();
            swapchainViews.clear();
            swapchainFramebuffers.clear();
            swapchainDepthViews.clear();
            swapchainDepthImages.clear();
            swapchainDepthMemory.clear();
            swapchain = VK_NULL_HANDLE;
            swapchainExtent = {0, 0};
            return;
        }
        for (VkFramebuffer framebufferHandle : swapchainFramebuffers) {
            vkDestroyFramebuffer(device, framebufferHandle, nullptr);
        }
        for (VkImageView imageView : swapchainViews) {
            vkDestroyImageView(device, imageView, nullptr);
        }
        if (swapchain != VK_NULL_HANDLE && destroySwapchain != nullptr) {
            destroySwapchain(device, swapchain, nullptr);
        }
        swapchainFramebuffers.clear();
        swapchainViews.clear();
        swapchainImages.clear();
        for (VkImageView view : swapchainDepthViews) {
            vkDestroyImageView(device, view, nullptr);
        }
        for (VkImage image : swapchainDepthImages) {
            vkDestroyImage(device, image, nullptr);
        }
        for (VkDeviceMemory memory : swapchainDepthMemory) {
            vkFreeMemory(device, memory, nullptr);
        }
        swapchainDepthViews.clear();
        swapchainDepthImages.clear();
        swapchainDepthMemory.clear();
        swapchain = VK_NULL_HANDLE;
        swapchainExtent = {0, 0};
    }

    bool createSwapchainResources() {
#if !defined(_WIN32)
        setError("Native Vulkan swapchains are currently supported only on Windows");
        return false;
#else
        if (surface == VK_NULL_HANDLE || window == nullptr || getPhysicalDeviceSurfaceCapabilities == nullptr) {
            setError("Native Vulkan swapchain is not configured");
            return false;
        }

        VkSurfaceCapabilitiesKHR capabilities{};
        if (getPhysicalDeviceSurfaceCapabilities(physicalDevice, surface, &capabilities) != VK_SUCCESS) {
            setError("vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed");
            return false;
        }

        VkExtent2D clientExtent{};
        if (!getWindowExtent(window, clientExtent)) {
            setError("Native window has no drawable client area");
            return false;
        }

        VkExtent2D desiredExtent = clientExtent;
        if (capabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max()) {
            desiredExtent = capabilities.currentExtent;
        } else {
            desiredExtent.width = std::max(capabilities.minImageExtent.width,
                                           std::min(capabilities.maxImageExtent.width, desiredExtent.width));
            desiredExtent.height = std::max(capabilities.minImageExtent.height,
                                            std::min(capabilities.maxImageExtent.height, desiredExtent.height));
        }
        if (desiredExtent.width == 0 || desiredExtent.height == 0) {
            setError("Native window has zero-sized swapchain extent");
            return false;
        }

        VkSwapchainCreateInfoKHR swapchainInfo{};
        swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swapchainInfo.surface = surface;
        swapchainInfo.minImageCount = capabilities.minImageCount;
        swapchainInfo.imageFormat = colorFormat;
        swapchainInfo.imageColorSpace = swapchainColorSpace;
        swapchainInfo.imageExtent = desiredExtent;
        swapchainInfo.imageArrayLayers = 1;
        swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        swapchainInfo.preTransform = capabilities.currentTransform;
        swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        swapchainInfo.presentMode = presentMode;
        swapchainInfo.clipped = VK_TRUE;
        swapchainInfo.oldSwapchain = swapchain;
        if (createSwapchain == nullptr || getSwapchainImages == nullptr) {
            setError("Vulkan swapchain entry points are unavailable");
            return false;
        }
        if (createSwapchain(device, &swapchainInfo, nullptr, &swapchain) != VK_SUCCESS) {
            setError("vkCreateSwapchainKHR failed");
            return false;
        }

        std::uint32_t imageCount = 0;
        if (getSwapchainImages(device, swapchain, &imageCount, nullptr) != VK_SUCCESS || imageCount == 0) {
            setError("vkGetSwapchainImagesKHR failed");
            return false;
        }
        swapchainImages.resize(imageCount);
        if (getSwapchainImages(device, swapchain, &imageCount, swapchainImages.data()) != VK_SUCCESS) {
            setError("vkGetSwapchainImagesKHR failed");
            return false;
        }

        swapchainViews.resize(swapchainImages.size());
        for (std::size_t index = 0; index < swapchainImages.size(); ++index) {
            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = swapchainImages[index];
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = colorFormat;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.layerCount = 1;
            if (vkCreateImageView(device, &viewInfo, nullptr, &swapchainViews[index]) != VK_SUCCESS) {
                setError("vkCreateImageView(swapchain) failed");
                return false;
            }
        }

        if (depthFormat == VK_FORMAT_UNDEFINED) {
            depthFormat = chooseDepthFormat();
            if (depthFormat == VK_FORMAT_UNDEFINED) {
                setError("No supported depth attachment format");
                return false;
            }
        }
        swapchainDepthViews.resize(swapchainImages.size());
        swapchainDepthImages.resize(swapchainImages.size());
        swapchainDepthMemory.resize(swapchainImages.size());
        for (std::size_t index = 0; index < swapchainImages.size(); ++index) {
            if (!createDepthImage(desiredExtent, swapchainDepthImages[index], swapchainDepthMemory[index], swapchainDepthViews[index])) {
                return false;
            }
        }

        swapchainFramebuffers.resize(swapchainImages.size());
        for (std::size_t index = 0; index < swapchainViews.size(); ++index) {
            std::array<VkImageView, 2> attachments{swapchainViews[index], swapchainDepthViews[index]};
            VkFramebufferCreateInfo framebufferInfo{};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = renderPass;
            framebufferInfo.attachmentCount = static_cast<std::uint32_t>(attachments.size());
            framebufferInfo.pAttachments = attachments.data();
            framebufferInfo.width = desiredExtent.width;
            framebufferInfo.height = desiredExtent.height;
            framebufferInfo.layers = 1;
            if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &swapchainFramebuffers[index]) != VK_SUCCESS) {
                setError("vkCreateFramebuffer(swapchain) failed");
                return false;
            }
        }

        swapchainExtent = desiredExtent;
        engineLog("swapchain created: %ux%u images=%zu fmt=%d present=%d",
                  desiredExtent.width,
                  desiredExtent.height,
                  swapchainImages.size(),
                  static_cast<int>(colorFormat),
                  static_cast<int>(presentMode));
        return true;
#endif
    }

    bool recreateSwapchain() noexcept {
        if (headless || device == VK_NULL_HANDLE || surface == VK_NULL_HANDLE) {
            setError("Native swapchain recreation is unavailable");
            return false;
        }
        vkDeviceWaitIdle(device);
        try {
            destroySwapchainResources();
            if (!createSwapchainResources()) {
                return false;
            }
            // Quality targets follow the swapchain size.
            return rebuildQualityTargets();
        } catch (...) {
            destroySwapchainResources();
            setError("Vulkan swapchain recreation failed due to allocation failure");
            return false;
        }
    }

    // Shadow pass: meshes into the sun depth map with the reflected camera
    // when reflections are on (the mirror needs shadows of reflected geometry).
    void renderShadowMap() {
        float sunMatrix[16];
        buildSunMatrix(sunMatrix);

        graphics_internal::ShadowRenderer::RenderContext context;
        context.commandBuffer = commandBuffer;
        context.renderPass = shadow.renderPass;
        context.framebuffer = shadow.framebuffer;
        context.mapSize = shadowMapSize;
        context.pipeline = shadowPipeline;
        context.pipelineLayout = shadowPipelineLayout;
        context.descriptorSet = sceneDescriptorSets[0];
        graphics_internal::ShadowRenderer::renderShadowMap(context, sunMatrix, pendingItems, meshes);
    }


    // Fullscreen tonemap/copy pass from the HDR target into `target`.
    void renderUiText(VkCommandBuffer buffer, std::uint32_t width, std::uint32_t height) {
        if (!textRenderer.initialized() || uiDrawList.textRuns.empty()) {
            return;
        }

        const UiTextAtlasDesc* atlasDesc = &uiDrawList.textAtlas;
        TextureHandle atlasTextureHandle = uiDrawList.textAtlas.texture;
        if (uiDrawList.font.valid()) {
            const auto fontIt = fonts.find(uiDrawList.font.value());
            if (fontIt == fonts.end()) {
                return;
            }
            atlasDesc = &fontIt->second.atlas;
            atlasTextureHandle = fontIt->second.texture;
        }

        const auto textureIt = textures.find(atlasTextureHandle.value());
        if (textureIt == textures.end() || textureIt->second.view == VK_NULL_HANDLE) {
            return;
        }
        graphics_internal::TextRenderer::AtlasTexture atlasTexture;
        atlasTexture.view = textureIt->second.view;
        atlasTexture.sampler = textureIt->second.sampler;
        atlasTexture.width = static_cast<float>(textureIt->second.width);
        atlasTexture.height = static_cast<float>(textureIt->second.height);
        atlasTexture.distanceRange = atlasDesc->distanceRange;
        atlasTexture.fontSize = atlasDesc->emSize;
        textRenderer.setMode(atlasDesc->mode == UiTextAtlasMode::TtfBitmap
            ? graphics_internal::TextRenderer::Mode::Ttf
            : graphics_internal::TextRenderer::Mode::Msdf);
        textRenderer.setAtlasTexture(atlasTexture);

        // Signature over everything that affects the glyph batch: the atlas and
        // each run's text/position/size/color. Unchanged content reuses the
        // cached layout and the TextRenderer skips the UBO upload.
        const auto hashBytes = [](std::uint64_t hash, const void* data, std::size_t size) noexcept {
            const auto* bytes = static_cast<const std::uint8_t*>(data);
            for (std::size_t index = 0; index < size; ++index) {
                hash ^= bytes[index];
                hash *= 1099511628211ull;
            }
            return hash;
        };
        std::uint64_t signature = 1469598103934665603ull;
        const std::uint64_t fontValue = uiDrawList.font.value();
        signature = hashBytes(signature, &fontValue, sizeof(fontValue));
        const auto hashFloat = [&](float value) { return hashBytes(signature, &value, sizeof(value)); };
        signature = hashFloat(static_cast<float>(atlasDesc->mode));
        signature = hashFloat(atlasDesc->emSize);
        signature = hashFloat(atlasDesc->distanceRange);
        signature = hashFloat(atlasDesc->lineHeight);
        signature = hashFloat(atlasDesc->ascender);
        for (const UiTextRun& run : uiDrawList.textRuns) {
            signature = hashBytes(signature, run.text.data(), run.text.size());
            signature = hashFloat(run.x);
            signature = hashFloat(run.y);
            signature = hashFloat(run.size);
            signature = hashFloat(run.outlineWidth);
            signature = hashFloat(run.color.r);
            signature = hashFloat(run.color.g);
            signature = hashFloat(run.color.b);
            signature = hashFloat(run.color.a);
            signature = hashFloat(run.outlineColor.r);
            signature = hashFloat(run.outlineColor.g);
            signature = hashFloat(run.outlineColor.b);
            signature = hashFloat(run.outlineColor.a);
        }

        float outlineWidth = 0.0f;
        graphics_internal::TextColor outlineColor{};
        if (!cachedUiTextValid || signature != cachedUiTextSignature) {
            cachedUiGlyphs.clear();
            for (const UiTextRun& run : uiDrawList.textRuns) {
                const graphics_internal::TextColor color{run.color.r, run.color.g, run.color.b, run.color.a};
                const graphics_internal::TextColor outline{
                    run.outlineColor.r, run.outlineColor.g, run.outlineColor.b, run.outlineColor.a};
                const std::vector<graphics_internal::TextGlyph> runGlyphs = textRenderer.layoutText(
                    *atlasDesc, run.text, run.x, run.y, run.size, color, run.outlineWidth, outline);
                cachedUiGlyphs.insert(cachedUiGlyphs.end(), runGlyphs.begin(), runGlyphs.end());
                if (run.outlineWidth > 0.0f) {
                    outlineWidth = run.outlineWidth;
                    outlineColor = outline;
                }
            }
            cachedUiTextSignature = signature;
            cachedUiTextValid = true;
        } else {
            for (const UiTextRun& run : uiDrawList.textRuns) {
                if (run.outlineWidth > 0.0f) {
                    outlineWidth = run.outlineWidth;
                    outlineColor = {run.outlineColor.r, run.outlineColor.g,
                                    run.outlineColor.b, run.outlineColor.a};
                }
            }
        }

        textRenderer.renderText(buffer, currentFrameSlot,
                                static_cast<float>(width), static_cast<float>(height),
                                1.0f, outlineWidth, outlineColor, cachedUiGlyphs, signature);
    }

    void renderTonemap(VkFramebuffer target, std::uint32_t width, std::uint32_t height) {
        graphics_internal::HdrRenderer::TonemapContext context;
        context.commandBuffer = commandBuffer;
        context.renderPass = renderPass;
        context.pipeline = tonemapPipeline;
        context.pipelineLayout = tonemapPipelineLayout;
        context.descriptorSet = tonemapDescriptorSet;
        graphics_internal::HdrRenderer::renderTonemap(
            context, target, width, height,
            [this, width, height](VkCommandBuffer buffer, VkPipelineLayout layout) {
                rectRenderer.draw(buffer, layout, width, height, uiDrawList);
                renderUiText(buffer, width, height);
            });
    }


    bool renderFrameCommands(VkFramebuffer targetFramebuffer, std::uint32_t width, std::uint32_t height) noexcept {
        if (vkResetCommandBuffer(commandBuffer, 0) != VK_SUCCESS) {
            setError("vkResetCommandBuffer failed");
            return false;
        }

        VkCommandBufferBeginInfo commandBeginInfo{};
        commandBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        commandBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (vkBeginCommandBuffer(commandBuffer, &commandBeginInfo) != VK_SUCCESS) {
            setError("vkBeginCommandBuffer failed");
            return false;
        }

        const std::uint32_t uniformSlot = currentFrameSlot % sceneUniformCount;
        float sunMatrix[16];
        buildSunMatrix(sunMatrix);
        writeSceneUniforms(uniformSlot, sunMatrix);

        if (shadowPipeline != VK_NULL_HANDLE && !pendingItems.empty()) {
            renderShadowMap();
        }

        // Reflection pass: mirrored camera renders the scene into a smaller
        // target first, so the main pass can sample it.
        const bool useReflection = reflectionsEnabled && reflectFramebuffer != VK_NULL_HANDLE;
        if (useReflection && !pendingItems.empty()) {
            std::array<VkClearValue, 2> reflectClear{};
            reflectClear[0].color = {{0.02f, 0.03f, 0.05f, 1.0f}};
            reflectClear[1].depthStencil = {1.0f, 0};
            VkRenderPassBeginInfo renderPassBegin{};
            renderPassBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            renderPassBegin.renderPass = renderPass;
            renderPassBegin.framebuffer = reflectFramebuffer;
            renderPassBegin.renderArea.extent = {reflectWidth, reflectHeight};
            renderPassBegin.clearValueCount = static_cast<std::uint32_t>(reflectClear.size());
            renderPassBegin.pClearValues = reflectClear.data();
            vkCmdBeginRenderPass(commandBuffer, &renderPassBegin, VK_SUBPASS_CONTENTS_INLINE);

            VkViewport reflectViewport{};
            reflectViewport.width = static_cast<float>(reflectWidth);
            reflectViewport.height = static_cast<float>(reflectHeight);
            reflectViewport.maxDepth = 1.0f;
            vkCmdSetViewport(commandBuffer, 0, 1, &reflectViewport);
            VkRect2D reflectScissor{};
            reflectScissor.extent = {reflectWidth, reflectHeight};
            vkCmdSetScissor(commandBuffer, 0, 1, &reflectScissor);

            const Mat4 mirroredView = reflectViewMatrix();
            float reflectViewProj[16];
            Impl::multiplyMat4(pendingProjection, mirroredView.m[0], reflectViewProj);
            graphics_internal::MeshRenderer::renderMeshes(
                commandBuffer, meshPipeline, meshPipelineLayout,
                sceneDescriptorSets[uniformSlot], reflectViewProj,
                pendingItems, meshes);
            vkCmdEndRenderPass(commandBuffer);
        }

        // Main pass: meshes render into the HDR target at Medium/High, or
        // straight into the final target at Low.
        VkFramebuffer mainFramebuffer = targetFramebuffer;
        std::uint32_t mainWidth = width;
        std::uint32_t mainHeight = height;
        VkRenderPass mainRenderPass = renderPass;
        VkPipeline mainMeshPipeline = meshPipeline;
        const bool useHdr = hdrEnabled && hdrFramebuffer != VK_NULL_HANDLE && hdrMeshPipeline != VK_NULL_HANDLE;
        if (useHdr) {
            mainFramebuffer = hdrFramebuffer;
            mainWidth = hdrWidth;
            mainHeight = hdrHeight;
            mainRenderPass = hdrRenderPass;
            mainMeshPipeline = hdrMeshPipeline;
        }

        {
            std::array<VkClearValue, 2> clearValues{};
            clearValues[0].color = {{0.02f, 0.03f, 0.05f, 1.0f}};
            clearValues[1].depthStencil = {1.0f, 0};
            VkRenderPassBeginInfo renderPassBegin{};
            renderPassBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            renderPassBegin.renderPass = mainRenderPass;
            renderPassBegin.framebuffer = mainFramebuffer;
            renderPassBegin.renderArea.extent = {mainWidth, mainHeight};
            renderPassBegin.clearValueCount = static_cast<std::uint32_t>(clearValues.size());
            renderPassBegin.pClearValues = clearValues.data();
            vkCmdBeginRenderPass(commandBuffer, &renderPassBegin, VK_SUBPASS_CONTENTS_INLINE);

            VkViewport viewport{};
            viewport.width = static_cast<float>(mainWidth);
            viewport.height = static_cast<float>(mainHeight);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
            VkRect2D scissor{};
            scissor.extent = {mainWidth, mainHeight};
            vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

            // The debug triangle pipeline targets the non-HDR color format;
            // skip it when the scene renders through the HDR pass.
            if (drawDebugTriangle && !useHdr) {
                vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                vkCmdBindDescriptorSets(
                    commandBuffer,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    pipelineLayout,
                    0,
                    1,
                    &descriptorSet,
                    0,
                    nullptr);
                vkCmdDraw(commandBuffer, 3, 1, 0, 0);
            }

            if (mainMeshPipeline != VK_NULL_HANDLE && !pendingItems.empty()) {
                graphics_internal::MeshRenderer::renderMeshes(
                    commandBuffer, meshPipeline, meshPipelineLayout,
                    sceneDescriptorSets[uniformSlot], pendingViewProj,
                    pendingItems, meshes, mainMeshPipeline);
            }
            vkCmdEndRenderPass(commandBuffer);
        }

        if (useHdr) {
            renderTonemap(targetFramebuffer, width, height);
        }

        if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
            setError("vkEndCommandBuffer failed");
            return false;
        }
        return true;
    }

    void destroy() noexcept {
        if (device != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(device);
        }

        textRenderer.destroy();

        if (device != VK_NULL_HANDLE) {
            if (pipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(device, pipeline, nullptr);
                pipeline = VK_NULL_HANDLE;
            }
            if (pipelineLayout != VK_NULL_HANDLE) {
                vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
                pipelineLayout = VK_NULL_HANDLE;
            }
            if (meshPipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(device, meshPipeline, nullptr);
                meshPipeline = VK_NULL_HANDLE;
            }
            if (hdrMeshPipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(device, hdrMeshPipeline, nullptr);
                hdrMeshPipeline = VK_NULL_HANDLE;
            }
            if (meshPipelineLayout != VK_NULL_HANDLE) {
                vkDestroyPipelineLayout(device, meshPipelineLayout, nullptr);
                meshPipelineLayout = VK_NULL_HANDLE;
            }
            if (meshVertexShader != VK_NULL_HANDLE) {
                vkDestroyShaderModule(device, meshVertexShader, nullptr);
                meshVertexShader = VK_NULL_HANDLE;
            }
            if (meshFragmentShader != VK_NULL_HANDLE) {
                vkDestroyShaderModule(device, meshFragmentShader, nullptr);
                meshFragmentShader = VK_NULL_HANDLE;
            }
            graphics_internal::Device::destroyFrameSync(device, frameSync);
            destroyDescriptorResources();
            for (auto& entry : textures) {
                graphics_internal::TextureManager::destroyResource(device, entry.second);
            }
            textures.clear();
            defaultTexture = invalidTextureHandle;
            boundTexture = invalidTextureHandle;
            nextTextureId = 1;
            textureUploadCount = 0;
            for (auto& entry : meshes) {
                graphics_internal::MeshRenderer::destroyResource(device, entry.second);
            }
            meshes.clear();
            nextMeshId = 1;
            pendingItems.clear();
            destroyHeadlessTarget();
            destroyQualityTargets();
            destroyDepthResources();
            destroySwapchainResources();
            if (shadowPipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(device, shadowPipeline, nullptr);
                shadowPipeline = VK_NULL_HANDLE;
            }
            if (shadowPipelineLayout != VK_NULL_HANDLE) {
                vkDestroyPipelineLayout(device, shadowPipelineLayout, nullptr);
                shadowPipelineLayout = VK_NULL_HANDLE;
            }
            if (shadowVertexShader != VK_NULL_HANDLE) {
                vkDestroyShaderModule(device, shadowVertexShader, nullptr);
                shadowVertexShader = VK_NULL_HANDLE;
            }
            if (shadowFragmentShader != VK_NULL_HANDLE) {
                vkDestroyShaderModule(device, shadowFragmentShader, nullptr);
                shadowFragmentShader = VK_NULL_HANDLE;
            }
            if (sceneDescriptorLayout != VK_NULL_HANDLE) {
                vkDestroyDescriptorSetLayout(device, sceneDescriptorLayout, nullptr);
                sceneDescriptorLayout = VK_NULL_HANDLE;
            }
            if (tonemapPipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(device, tonemapPipeline, nullptr);
                tonemapPipeline = VK_NULL_HANDLE;
            }
            if (tonemapPipelineLayout != VK_NULL_HANDLE) {
                vkDestroyPipelineLayout(device, tonemapPipelineLayout, nullptr);
                tonemapPipelineLayout = VK_NULL_HANDLE;
            }
            if (tonemapDescriptorPool != VK_NULL_HANDLE) {
                vkDestroyDescriptorPool(device, tonemapDescriptorPool, nullptr);
                tonemapDescriptorPool = VK_NULL_HANDLE;
            }
            if (tonemapDescriptorLayout != VK_NULL_HANDLE) {
                vkDestroyDescriptorSetLayout(device, tonemapDescriptorLayout, nullptr);
                tonemapDescriptorLayout = VK_NULL_HANDLE;
            }
            if (tonemapVertexShader != VK_NULL_HANDLE) {
                vkDestroyShaderModule(device, tonemapVertexShader, nullptr);
                tonemapVertexShader = VK_NULL_HANDLE;
            }
            if (tonemapFragmentShader != VK_NULL_HANDLE) {
                vkDestroyShaderModule(device, tonemapFragmentShader, nullptr);
                tonemapFragmentShader = VK_NULL_HANDLE;
            }
            if (renderPass != VK_NULL_HANDLE) {
                vkDestroyRenderPass(device, renderPass, nullptr);
                renderPass = VK_NULL_HANDLE;
            }
            if (hdrRenderPass != VK_NULL_HANDLE) {
                vkDestroyRenderPass(device, hdrRenderPass, nullptr);
                hdrRenderPass = VK_NULL_HANDLE;
            }
            if (vertexShader != VK_NULL_HANDLE) {
                vkDestroyShaderModule(device, vertexShader, nullptr);
                vertexShader = VK_NULL_HANDLE;
            }
            if (fragmentShader != VK_NULL_HANDLE) {
                vkDestroyShaderModule(device, fragmentShader, nullptr);
                fragmentShader = VK_NULL_HANDLE;
            }
            if (commandPool != VK_NULL_HANDLE) {
                vkDestroyCommandPool(device, commandPool, nullptr);
                commandPool = VK_NULL_HANDLE;
            }
            vkDestroyDevice(device, nullptr);
            device = VK_NULL_HANDLE;
        } else {
            destroySwapchainResources();
            for (auto& entry : textures) {
                graphics_internal::TextureManager::destroyResource(device, entry.second);
            }
            textures.clear();
            defaultTexture = invalidTextureHandle;
            boundTexture = invalidTextureHandle;
            nextTextureId = 1;
            textureUploadCount = 0;
            for (auto& entry : meshes) {
                graphics_internal::MeshRenderer::destroyResource(device, entry.second);
            }
            meshes.clear();
            nextMeshId = 1;
            pendingItems.clear();
            destroyQualityTargets();
            destroyDepthResources();
        }

        if (instance != VK_NULL_HANDLE) {
            if (surface != VK_NULL_HANDLE && destroySurface != nullptr) {
                destroySurface(instance, surface, nullptr);
            }
            surface = VK_NULL_HANDLE;
            if (debugMessenger != VK_NULL_HANDLE && destroyDebugMessenger != nullptr) {
                destroyDebugMessenger(instance, debugMessenger, nullptr);
            }
            debugMessenger = VK_NULL_HANDLE;
            destroyDebugMessenger = nullptr;
            vkDestroyInstance(instance, nullptr);
            instance = VK_NULL_HANDLE;
        }

        physicalDevice = VK_NULL_HANDLE;
        queue = VK_NULL_HANDLE;
        commandBuffer = VK_NULL_HANDLE;
        frameSync = {};
        currentFrameSlot = 0;
        colorFormat = VK_FORMAT_UNDEFINED;
        depthFormat = VK_FORMAT_UNDEFINED;
        surfaceFormat = {};
        swapchainColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        swapchainExtent = {0, 0};
        presentMode = VK_PRESENT_MODE_FIFO_KHR;
        headless = true;
        validationEnabled = false;
        presentCount = 0;
        surface = VK_NULL_HANDLE;
        destroySurface = nullptr;
        getPhysicalDeviceSurfaceSupport = nullptr;
        getPhysicalDeviceSurfaceCapabilities = nullptr;
        getPhysicalDeviceSurfaceFormats = nullptr;
        getPhysicalDeviceSurfacePresentModes = nullptr;
        createSwapchain = nullptr;
        destroySwapchain = nullptr;
        getSwapchainImages = nullptr;
        acquireNextImage = nullptr;
        queuePresent = nullptr;
#if defined(_WIN32)
        window = nullptr;
#endif
        deviceName.clear();
        hdrEnabled = false;
        reflectionsEnabled = false;
        shadowTaps = 9;
        reflectionScale = 0.0f;
        shadowStrength = 0.35f;
        textureScalePercent = 100;
        activeQuality = Aporia::GraphicsQuality::Medium;
        environment = Aporia::EnvironmentState{};
        hdrFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    }
};

Graphics::Graphics()
    : impl_(std::make_unique<Impl>()) {
}

Graphics::~Graphics() = default;
Graphics::Graphics(Graphics&&) noexcept = default;

Graphics& Graphics::operator=(Graphics&& other) noexcept {
    if (this != &other) {
        shutdown();
        impl_ = std::move(other.impl_);
    }
    return *this;
}

bool Graphics::initialize(const GraphicsContext& context, const GraphicsConfig& config) {
    shutdown();
    if (!impl_) {
        impl_ = std::make_unique<Impl>();
    }
    impl_->lastError.clear();
    impl_->frameCount = 0;
    impl_->presentCount = 0;
    impl_->headless = config.headless;
    impl_->renderWidth = config.headlessWidth > 0 ? config.headlessWidth : kDefaultHeadlessExtent;
    impl_->renderHeight = config.headlessHeight > 0 ? config.headlessHeight : kDefaultHeadlessExtent;

    impl_->validationMessageCount = 0;
    impl_->environment.sunDirection[0] = config.sunDirection[0];
    impl_->environment.sunDirection[1] = config.sunDirection[1];
    impl_->environment.sunDirection[2] = config.sunDirection[2];
    impl_->environment.sunIntensity = 1.0f;
    impl_->environment.mirrorZ = config.mirrorZ;
    impl_->shadowExtent = config.shadowExtent;
    impl_->applyQualityPreset(config.quality);
    impl_->antiAliasing = static_cast<std::uint32_t>(config.antiAliasing);
    impl_->blurConfig = config.blur;
    impl_->shadowConfig = config.shadows;
    impl_->shadowConfig.mapSize = std::max(1u, config.shadows.mapSize);
    impl_->shadowConfig.cascadeCount = std::max(1u, config.shadows.cascadeCount);
    impl_->shadowConfig.passCount = std::max(1u, config.shadows.passCount);
    impl_->shadowConfig.pcfRadius = std::min(8u, config.shadows.pcfRadius);
    impl_->playerUltraShadows = config.playerUltraShadows;
    impl_->shadowMapSize = impl_->shadowConfig.mapSize;
    impl_->shadowTaps = static_cast<int>((impl_->shadowConfig.pcfRadius * 2 + 1) * (impl_->shadowConfig.pcfRadius * 2 + 1));
    impl_->shadowStrength = impl_->shadowConfig.strength;
    impl_->shadowRenderer.setConfig(impl_->shadowConfig);
    impl_->shadowRenderer.setPlayerUltra(impl_->playerUltraShadows);
    impl_->hdrRenderer.setAntiAliasing(config.antiAliasing);

    struct InitializationGuard {
        Impl* impl = nullptr;
        bool committed = false;

        ~InitializationGuard() {
            if (!committed && impl != nullptr) {
                impl->destroy();
            }
        }
    } guard{impl_.get()};

    if (!config.headless) {
#if defined(_WIN32)
        if (context.nativeWindow == 0) {
            impl_->setError("Native graphics initialization requires a native window handle");
            return false;
        }
#else
        impl_->setError("Native graphics initialization is not supported on this platform");
        return false;
#endif
    }

    graphics_internal::Device::InstanceResult instanceResult;
    if (!graphics_internal::Device::createInstance(
            config.headless, config.enableValidation, &impl_->validationMessageCount, instanceResult)) {
        impl_->setError("Vulkan instance creation failed");
        return false;
    }
    impl_->validationEnabled = instanceResult.validationEnabled;
    impl_->instance = instanceResult.instance;
    impl_->debugMessenger = instanceResult.messenger;
    impl_->destroyDebugMessenger = instanceResult.destroyMessenger;
    engineLog("=== init headless=%d validation=%d ===", config.headless ? 1 : 0, impl_->validationEnabled ? 1 : 0);

    if (!impl_->headless) {
#if defined(_WIN32)
        auto createSurface = reinterpret_cast<PFN_vkCreateWin32SurfaceKHR>(
            vkGetInstanceProcAddr(impl_->instance, "vkCreateWin32SurfaceKHR"));
        if (createSurface == nullptr) {
            impl_->setError("vkCreateWin32SurfaceKHR is unavailable");
            return false;
        }
        VkWin32SurfaceCreateInfoKHR surfaceInfo{};
        surfaceInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
        surfaceInfo.hwnd = reinterpret_cast<HWND>(context.nativeWindow);
        surfaceInfo.hinstance = GetModuleHandleW(nullptr);
        if (createSurface(impl_->instance, &surfaceInfo, nullptr, &impl_->surface) != VK_SUCCESS) {
            impl_->setError("vkCreateWin32SurfaceKHR failed");
            return false;
        }
        impl_->window = surfaceInfo.hwnd;
        if (config.showWindow) {
            ShowWindow(impl_->window, SW_SHOW);
            UpdateWindow(impl_->window);
        }
        impl_->destroySurface = reinterpret_cast<PFN_vkDestroySurfaceKHR>(
            vkGetInstanceProcAddr(impl_->instance, "vkDestroySurfaceKHR"));
        impl_->getPhysicalDeviceSurfaceSupport = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceSupportKHR>(
            vkGetInstanceProcAddr(impl_->instance, "vkGetPhysicalDeviceSurfaceSupportKHR"));
        impl_->getPhysicalDeviceSurfaceCapabilities = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>(
            vkGetInstanceProcAddr(impl_->instance, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR"));
        impl_->getPhysicalDeviceSurfaceFormats = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceFormatsKHR>(
            vkGetInstanceProcAddr(impl_->instance, "vkGetPhysicalDeviceSurfaceFormatsKHR"));
        impl_->getPhysicalDeviceSurfacePresentModes = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfacePresentModesKHR>(
            vkGetInstanceProcAddr(impl_->instance, "vkGetPhysicalDeviceSurfacePresentModesKHR"));
#else
        impl_->setError("Native Vulkan surfaces are not supported on this platform");
        return false;
#endif
    }


    graphics_internal::Device::DeviceCreateResult deviceResult;
    if (!graphics_internal::Device::createDevice(impl_->instance, impl_->headless, deviceResult)) {
        impl_->setError("Vulkan device creation failed");
        shutdown();
        return false;
    }
    impl_->physicalDevice = deviceResult.physicalDevice;
    impl_->device = deviceResult.device;
    impl_->queue = deviceResult.queue;
    impl_->graphicsFamily = deviceResult.graphicsFamily;
    impl_->memoryProperties = deviceResult.memoryProperties;
    impl_->deviceName = deviceResult.deviceName;
    engineLog("device: %s headless=%d", impl_->deviceName.c_str(), impl_->headless ? 1 : 0);

    if (!impl_->headless) {
        impl_->createSwapchain = reinterpret_cast<PFN_vkCreateSwapchainKHR>(
            vkGetDeviceProcAddr(impl_->device, "vkCreateSwapchainKHR"));
        impl_->destroySwapchain = reinterpret_cast<PFN_vkDestroySwapchainKHR>(
            vkGetDeviceProcAddr(impl_->device, "vkDestroySwapchainKHR"));
        impl_->getSwapchainImages = reinterpret_cast<PFN_vkGetSwapchainImagesKHR>(
            vkGetDeviceProcAddr(impl_->device, "vkGetSwapchainImagesKHR"));
        impl_->acquireNextImage = reinterpret_cast<PFN_vkAcquireNextImageKHR>(
            vkGetDeviceProcAddr(impl_->device, "vkAcquireNextImageKHR"));
        impl_->queuePresent = reinterpret_cast<PFN_vkQueuePresentKHR>(
            vkGetDeviceProcAddr(impl_->device, "vkQueuePresentKHR"));
    }

    if (impl_->headless) {

        if (!chooseColorFormat(impl_->physicalDevice, impl_->colorFormat)) {
            impl_->setError("No supported Vulkan color attachment format");
            shutdown();
            return false;
        }
    } else if (!impl_->configureNativeSurface()) {
        shutdown();
        return false;
    }

    if (impl_->hdrEnabled) {
        impl_->hdrFormat = chooseHdrFormat(impl_->physicalDevice);
        if (impl_->hdrFormat == VK_FORMAT_UNDEFINED) {
            impl_->setError("No supported HDR color format");
            shutdown();
            return false;
        }
        if (!impl_->createHdrRenderPass()) {
            shutdown();
            return false;
        }
    }
    if (!impl_->createShadowResources()) {
        shutdown();
        return false;
    }

    if (!graphics_internal::Device::createCommandPool(impl_->device, impl_->graphicsFamily, impl_->commandPool)) {
        impl_->setError("vkCreateCommandPool failed");
        shutdown();
        return false;
    }
    if (!graphics_internal::Device::createCommandBuffer(impl_->device, impl_->commandPool, impl_->commandBuffer)) {
        impl_->setError("vkAllocateCommandBuffers failed");
        shutdown();
        return false;
    }


    if (!impl_->headless) {
        if (!graphics_internal::Device::createFrameSync(impl_->device, impl_->frameSync)) {
            impl_->setError("vkCreateSemaphore/vkCreateFence failed");
            shutdown();
            return false;
        }
    }


    if (!impl_->createRenderPass()) {
        shutdown();
        return false;
    }
    if (impl_->headless) {
        if (!impl_->createHeadlessTarget()) {
            shutdown();
            return false;
        }
    } else if (!impl_->createSwapchainResources()) {
        shutdown();
        return false;
    }

    if (!impl_->createDefaultTexture()) {
        shutdown();
        return false;
    }
    if (!impl_->createDescriptorResources()) {
        shutdown();
        return false;
    }
    // Scene layout must exist before the mesh/shadow pipelines referencing it.
    if (!impl_->createSceneDescriptorLayout()) {
        shutdown();
        return false;
    }

    const std::string shaderDirectory = APORIA_VULKAN_SHADER_DIR;
    bool spirvLoaded = false;
    const std::vector<std::uint32_t> vertexCode = readSpirv(shaderDirectory + "/triangle.vert.spv", spirvLoaded);
    if (!spirvLoaded) {
        impl_->setError("Failed to load compiled vertex shader");
        shutdown();
        return false;
    }
    const std::vector<std::uint32_t> fragmentCode = readSpirv(shaderDirectory + "/triangle.frag.spv", spirvLoaded);
    if (!spirvLoaded) {
        impl_->setError("Failed to load compiled fragment shader");
        shutdown();
        return false;
    }

    VkShaderModuleCreateInfo vertexModuleInfo{};
    vertexModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vertexModuleInfo.codeSize = vertexCode.size() * sizeof(std::uint32_t);
    vertexModuleInfo.pCode = vertexCode.data();
    if (vkCreateShaderModule(impl_->device, &vertexModuleInfo, nullptr, &impl_->vertexShader) != VK_SUCCESS) {
        impl_->setError("vkCreateShaderModule(vertex) failed");
        shutdown();
        return false;
    }

    VkShaderModuleCreateInfo fragmentModuleInfo{};
    fragmentModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    fragmentModuleInfo.codeSize = fragmentCode.size() * sizeof(std::uint32_t);
    fragmentModuleInfo.pCode = fragmentCode.data();
    if (vkCreateShaderModule(impl_->device, &fragmentModuleInfo, nullptr, &impl_->fragmentShader) != VK_SUCCESS) {
        impl_->setError("vkCreateShaderModule(fragment) failed");
        shutdown();
        return false;
    }

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &impl_->descriptorSetLayout;
    if (vkCreatePipelineLayout(impl_->device, &pipelineLayoutInfo, nullptr, &impl_->pipelineLayout) != VK_SUCCESS) {
        impl_->setError("vkCreatePipelineLayout failed");
        shutdown();
        return false;
    }

    VkPipelineShaderStageCreateInfo shaderStages[2]{};
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = impl_->vertexShader;
    shaderStages[0].pName = "main";
    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = impl_->fragmentShader;
    shaderStages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

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

    VkPipelineColorBlendAttachmentState colorAttachment{};
    colorAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &colorAttachment;

    const VkDynamicState dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkPipelineDepthStencilStateCreateInfo triangleDepthStencil{};
    triangleDepthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    triangleDepthStencil.depthTestEnable = VK_TRUE;
    triangleDepthStencil.depthWriteEnable = VK_TRUE;
    triangleDepthStencil.depthCompareOp = VK_COMPARE_OP_ALWAYS;
    triangleDepthStencil.minDepthBounds = 0.0f;
    triangleDepthStencil.maxDepthBounds = 1.0f;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterization;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &triangleDepthStencil;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = impl_->pipelineLayout;
    pipelineInfo.renderPass = impl_->renderPass;
    pipelineInfo.subpass = 0;
    if (vkCreateGraphicsPipelines(impl_->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &impl_->pipeline) != VK_SUCCESS) {
        impl_->setError("vkCreateGraphicsPipelines failed");
        shutdown();
        return false;
    }

    // Mesh pipeline: vertex buffers + push-constant camera.
    const std::vector<std::uint32_t> meshVertexCode = readSpirv(shaderDirectory + "/mesh.vert.spv", spirvLoaded);
    if (!spirvLoaded) {
        impl_->setError("Failed to load compiled mesh vertex shader");
        shutdown();
        return false;
    }
    const std::vector<std::uint32_t> meshFragmentCode = readSpirv(shaderDirectory + "/mesh.frag.spv", spirvLoaded);
    if (!spirvLoaded) {
        impl_->setError("Failed to load compiled mesh fragment shader");
        shutdown();
        return false;
    }

    VkShaderModuleCreateInfo meshVertexModuleInfo{};
    meshVertexModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    meshVertexModuleInfo.codeSize = meshVertexCode.size() * sizeof(std::uint32_t);
    meshVertexModuleInfo.pCode = meshVertexCode.data();
    if (vkCreateShaderModule(impl_->device, &meshVertexModuleInfo, nullptr, &impl_->meshVertexShader) != VK_SUCCESS) {
        impl_->setError("vkCreateShaderModule(mesh vertex) failed");
        shutdown();
        return false;
    }

    VkShaderModuleCreateInfo meshFragmentModuleInfo{};
    meshFragmentModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    meshFragmentModuleInfo.codeSize = meshFragmentCode.size() * sizeof(std::uint32_t);
    meshFragmentModuleInfo.pCode = meshFragmentCode.data();
    if (vkCreateShaderModule(impl_->device, &meshFragmentModuleInfo, nullptr, &impl_->meshFragmentShader) != VK_SUCCESS) {
        impl_->setError("vkCreateShaderModule(mesh fragment) failed");
        shutdown();
        return false;
    }

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(Impl::PushConstants);

    VkPipelineLayoutCreateInfo meshLayoutInfo{};
    meshLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    meshLayoutInfo.setLayoutCount = 1;
    meshLayoutInfo.pSetLayouts = &impl_->sceneDescriptorLayout;
    meshLayoutInfo.pushConstantRangeCount = 1;
    meshLayoutInfo.pPushConstantRanges = &pushRange;
    if (vkCreatePipelineLayout(impl_->device, &meshLayoutInfo, nullptr, &impl_->meshPipelineLayout) != VK_SUCCESS) {
        impl_->setError("vkCreatePipelineLayout(mesh) failed");
        shutdown();
        return false;
    }

    VkPipelineShaderStageCreateInfo meshShaderStages[2]{};
    meshShaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    meshShaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    meshShaderStages[0].module = impl_->meshVertexShader;
    meshShaderStages[0].pName = "main";
    meshShaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    meshShaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    meshShaderStages[1].module = impl_->meshFragmentShader;
    meshShaderStages[1].pName = "main";

    VkVertexInputBindingDescription vertexBinding{};
    vertexBinding.binding = 0;
    vertexBinding.stride = static_cast<std::uint32_t>(sizeof(MeshVertex));
    vertexBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 3> vertexAttributes{};
    vertexAttributes[0].binding = 0;
    vertexAttributes[0].location = 0;
    vertexAttributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    vertexAttributes[0].offset = offsetof(MeshVertex, position);
    vertexAttributes[1].binding = 0;
    vertexAttributes[1].location = 1;
    vertexAttributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    vertexAttributes[1].offset = offsetof(MeshVertex, normal);
    vertexAttributes[2].binding = 0;
    vertexAttributes[2].location = 2;
    vertexAttributes[2].format = VK_FORMAT_R32G32_SFLOAT;
    vertexAttributes[2].offset = offsetof(MeshVertex, uv);

    VkPipelineVertexInputStateCreateInfo meshVertexInput{};
    meshVertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    meshVertexInput.vertexBindingDescriptionCount = 1;
    meshVertexInput.pVertexBindingDescriptions = &vertexBinding;
    meshVertexInput.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(vertexAttributes.size());
    meshVertexInput.pVertexAttributeDescriptions = vertexAttributes.data();

    // The depth-only shader consumes only position; sharing the full mesh
    // declaration triggered validation warnings for unused normal/UV inputs.
    VkVertexInputAttributeDescription shadowVertexAttribute{};
    shadowVertexAttribute.binding = 0;
    shadowVertexAttribute.location = 0;
    shadowVertexAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
    shadowVertexAttribute.offset = offsetof(MeshVertex, position);
    VkPipelineVertexInputStateCreateInfo shadowVertexInput{};
    shadowVertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    shadowVertexInput.vertexBindingDescriptionCount = 1;
    shadowVertexInput.pVertexBindingDescriptions = &vertexBinding;
    shadowVertexInput.vertexAttributeDescriptionCount = 1;
    shadowVertexInput.pVertexAttributeDescriptions = &shadowVertexAttribute;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.minDepthBounds = 0.0f;
    depthStencil.maxDepthBounds = 1.0f;

    VkGraphicsPipelineCreateInfo meshPipelineInfo{};
    meshPipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    meshPipelineInfo.stageCount = 2;
    meshPipelineInfo.pStages = meshShaderStages;
    meshPipelineInfo.pVertexInputState = &meshVertexInput;
    meshPipelineInfo.pInputAssemblyState = &inputAssembly;
    meshPipelineInfo.pViewportState = &viewportState;
    meshPipelineInfo.pRasterizationState = &rasterization;
    meshPipelineInfo.pMultisampleState = &multisample;
    meshPipelineInfo.pDepthStencilState = &depthStencil;
    meshPipelineInfo.pColorBlendState = &colorBlend;
    meshPipelineInfo.pDynamicState = &dynamicState;
    meshPipelineInfo.layout = impl_->meshPipelineLayout;
    meshPipelineInfo.renderPass = impl_->renderPass;
    meshPipelineInfo.subpass = 0;
    // Quality specialization: PCF tap count and reflection toggle compile per
    // preset, so Low pays neither cost.
    VkSpecializationMapEntry specEntries[2]{};
    specEntries[0].constantID = 0;
    specEntries[0].offset = 0;
    specEntries[0].size = sizeof(std::int32_t);
    specEntries[1].constantID = 1;
    specEntries[1].offset = sizeof(std::int32_t);
    specEntries[1].size = sizeof(float);
    float specReflection = impl_->reflectionsEnabled ? 1.0f : 0.0f;
    std::int32_t specTaps = impl_->shadowTaps;
    std::int32_t specData[2] = {specTaps, 0};
    std::memcpy(specData + 1, &specReflection, sizeof(float));
    VkSpecializationInfo meshSpecInfo{};
    meshSpecInfo.mapEntryCount = 2;
    meshSpecInfo.pMapEntries = specEntries;
    meshSpecInfo.dataSize = sizeof(specData);
    meshSpecInfo.pData = specData;
    meshShaderStages[1].pSpecializationInfo = &meshSpecInfo;
    if (vkCreateGraphicsPipelines(impl_->device, VK_NULL_HANDLE, 1, &meshPipelineInfo, nullptr, &impl_->meshPipeline) != VK_SUCCESS) {
        impl_->setError("vkCreateGraphicsPipelines(mesh) failed");
        shutdown();
        return false;
    }
    if (impl_->hdrRenderPass != VK_NULL_HANDLE) {
        meshPipelineInfo.renderPass = impl_->hdrRenderPass;
        if (vkCreateGraphicsPipelines(impl_->device, VK_NULL_HANDLE, 1, &meshPipelineInfo, nullptr, &impl_->hdrMeshPipeline) != VK_SUCCESS) {
            impl_->setError("vkCreateGraphicsPipelines(hdr mesh) failed");
            shutdown();
            return false;
        }
    }

    // Quality pipelines: shadow depth pass and HDR tonemap pass.
    const std::vector<std::uint32_t> shadowVertexCode = readSpirv(shaderDirectory + "/shadow.vert.spv", spirvLoaded);
    if (!spirvLoaded) {
        impl_->setError("Failed to load compiled shadow vertex shader");
        shutdown();
        return false;
    }
    const std::vector<std::uint32_t> shadowFragmentCode = readSpirv(shaderDirectory + "/shadow.frag.spv", spirvLoaded);
    if (!spirvLoaded) {
        impl_->setError("Failed to load compiled shadow fragment shader");
        shutdown();
        return false;
    }
    VkShaderModuleCreateInfo shadowVertexModuleInfo{};
    shadowVertexModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shadowVertexModuleInfo.codeSize = shadowVertexCode.size() * sizeof(std::uint32_t);
    shadowVertexModuleInfo.pCode = shadowVertexCode.data();
    if (vkCreateShaderModule(impl_->device, &shadowVertexModuleInfo, nullptr, &impl_->shadowVertexShader) != VK_SUCCESS) {
        impl_->setError("vkCreateShaderModule(shadow vertex) failed");
        shutdown();
        return false;
    }
    VkShaderModuleCreateInfo shadowFragmentModuleInfo{};
    shadowFragmentModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shadowFragmentModuleInfo.codeSize = shadowFragmentCode.size() * sizeof(std::uint32_t);
    shadowFragmentModuleInfo.pCode = shadowFragmentCode.data();
    if (vkCreateShaderModule(impl_->device, &shadowFragmentModuleInfo, nullptr, &impl_->shadowFragmentShader) != VK_SUCCESS) {
        impl_->setError("vkCreateShaderModule(shadow fragment) failed");
        shutdown();
        return false;
    }

    VkPushConstantRange shadowPushRange{};
    shadowPushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    shadowPushRange.offset = 0;
    shadowPushRange.size = sizeof(Impl::PushConstants);
    VkPipelineLayoutCreateInfo shadowLayoutInfo{};
    shadowLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    shadowLayoutInfo.setLayoutCount = 1;
    shadowLayoutInfo.pSetLayouts = &impl_->sceneDescriptorLayout;
    shadowLayoutInfo.pushConstantRangeCount = 1;
    shadowLayoutInfo.pPushConstantRanges = &shadowPushRange;
    if (vkCreatePipelineLayout(impl_->device, &shadowLayoutInfo, nullptr, &impl_->shadowPipelineLayout) != VK_SUCCESS) {
        impl_->setError("vkCreatePipelineLayout(shadow) failed");
        shutdown();
        return false;
    }

    VkPipelineShaderStageCreateInfo shadowStages[2]{};
    shadowStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shadowStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shadowStages[0].module = impl_->shadowVertexShader;
    shadowStages[0].pName = "main";
    shadowStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shadowStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shadowStages[1].module = impl_->shadowFragmentShader;
    shadowStages[1].pName = "main";

    VkPipelineDepthStencilStateCreateInfo shadowDepthStencil{};
    shadowDepthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    shadowDepthStencil.depthTestEnable = VK_TRUE;
    shadowDepthStencil.depthWriteEnable = VK_TRUE;
    shadowDepthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    shadowDepthStencil.minDepthBounds = 0.0f;
    shadowDepthStencil.maxDepthBounds = 1.0f;

    VkGraphicsPipelineCreateInfo shadowPipelineInfo{};
    shadowPipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    shadowPipelineInfo.stageCount = 2;
    shadowPipelineInfo.pStages = shadowStages;
    shadowPipelineInfo.pVertexInputState = &shadowVertexInput;
    shadowPipelineInfo.pInputAssemblyState = &inputAssembly;
    shadowPipelineInfo.pViewportState = &viewportState;
    shadowPipelineInfo.pRasterizationState = &rasterization;
    shadowPipelineInfo.pMultisampleState = &multisample;
    shadowPipelineInfo.pDepthStencilState = &shadowDepthStencil;
    shadowPipelineInfo.pDynamicState = &dynamicState;
    shadowPipelineInfo.layout = impl_->shadowPipelineLayout;
    shadowPipelineInfo.renderPass = impl_->shadow.renderPass;
    shadowPipelineInfo.subpass = 0;
    if (vkCreateGraphicsPipelines(impl_->device, VK_NULL_HANDLE, 1, &shadowPipelineInfo, nullptr, &impl_->shadowPipeline) != VK_SUCCESS) {
        impl_->setError("vkCreateGraphicsPipelines(shadow) failed");
        shutdown();
        return false;
    }

    const std::vector<std::uint32_t> tonemapVertexCode = readSpirv(shaderDirectory + "/tonemap.vert.spv", spirvLoaded);
    if (!spirvLoaded) {
        impl_->setError("Failed to load compiled tonemap vertex shader");
        shutdown();
        return false;
    }
    const std::vector<std::uint32_t> tonemapFragmentCode = readSpirv(shaderDirectory + "/tonemap.frag.spv", spirvLoaded);
    if (!spirvLoaded) {
        impl_->setError("Failed to load compiled tonemap fragment shader");
        shutdown();
        return false;
    }
    VkShaderModuleCreateInfo tonemapVertexModuleInfo{};
    tonemapVertexModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    tonemapVertexModuleInfo.codeSize = tonemapVertexCode.size() * sizeof(std::uint32_t);
    tonemapVertexModuleInfo.pCode = tonemapVertexCode.data();
    if (vkCreateShaderModule(impl_->device, &tonemapVertexModuleInfo, nullptr, &impl_->tonemapVertexShader) != VK_SUCCESS) {
        impl_->setError("vkCreateShaderModule(tonemap vertex) failed");
        shutdown();
        return false;
    }
    VkShaderModuleCreateInfo tonemapFragmentModuleInfo{};
    tonemapFragmentModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    tonemapFragmentModuleInfo.codeSize = tonemapFragmentCode.size() * sizeof(std::uint32_t);
    tonemapFragmentModuleInfo.pCode = tonemapFragmentCode.data();
    if (vkCreateShaderModule(impl_->device, &tonemapFragmentModuleInfo, nullptr, &impl_->tonemapFragmentShader) != VK_SUCCESS) {
        impl_->setError("vkCreateShaderModule(tonemap fragment) failed");
        shutdown();
        return false;
    }

    VkDescriptorSetLayoutBinding hdrSamplerBinding{};
    hdrSamplerBinding.binding = 0;
    hdrSamplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    hdrSamplerBinding.descriptorCount = 1;
    hdrSamplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo tonemapLayoutInfo{};
    tonemapLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    tonemapLayoutInfo.bindingCount = 1;
    tonemapLayoutInfo.pBindings = &hdrSamplerBinding;
    if (vkCreateDescriptorSetLayout(impl_->device, &tonemapLayoutInfo, nullptr, &impl_->tonemapDescriptorLayout) != VK_SUCCESS) {
        impl_->setError("vkCreateDescriptorSetLayout(tonemap) failed");
        shutdown();
        return false;
    }
    VkPipelineLayoutCreateInfo tonemapPipelineLayoutInfo{};
    tonemapPipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    tonemapPipelineLayoutInfo.setLayoutCount = 1;
    tonemapPipelineLayoutInfo.pSetLayouts = &impl_->tonemapDescriptorLayout;
    VkPushConstantRange tonemapPushRange{};
    tonemapPushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    tonemapPushRange.offset = 0;
    tonemapPushRange.size = sizeof(graphics_internal::RectRenderer::PushConstants);
    tonemapPipelineLayoutInfo.pushConstantRangeCount = 1;
    tonemapPipelineLayoutInfo.pPushConstantRanges = &tonemapPushRange;
    if (vkCreatePipelineLayout(impl_->device, &tonemapPipelineLayoutInfo, nullptr, &impl_->tonemapPipelineLayout) != VK_SUCCESS) {
        impl_->setError("vkCreatePipelineLayout(tonemap) failed");
        shutdown();
        return false;
    }
    VkDescriptorPoolSize tonemapPoolSize{};
    tonemapPoolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    tonemapPoolSize.descriptorCount = 1;
    VkDescriptorPoolCreateInfo tonemapPoolInfo{};
    tonemapPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    tonemapPoolInfo.maxSets = 1;
    tonemapPoolInfo.poolSizeCount = 1;
    tonemapPoolInfo.pPoolSizes = &tonemapPoolSize;
    if (vkCreateDescriptorPool(impl_->device, &tonemapPoolInfo, nullptr, &impl_->tonemapDescriptorPool) != VK_SUCCESS) {
        impl_->setError("vkCreateDescriptorPool(tonemap) failed");
        shutdown();
        return false;
    }

    VkPipelineShaderStageCreateInfo tonemapStages[2]{};
    tonemapStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    tonemapStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    tonemapStages[0].module = impl_->tonemapVertexShader;
    tonemapStages[0].pName = "main";
    tonemapStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    tonemapStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    tonemapStages[1].module = impl_->tonemapFragmentShader;
    tonemapStages[1].pName = "main";
    VkSpecializationMapEntry aaEntry{};
    aaEntry.constantID = 0;
    aaEntry.offset = 0;
    aaEntry.size = sizeof(std::int32_t);
    const std::int32_t aaMode = static_cast<std::int32_t>(impl_->antiAliasing);
    VkSpecializationInfo aaSpecialization{};
    aaSpecialization.mapEntryCount = 1;
    aaSpecialization.pMapEntries = &aaEntry;
    aaSpecialization.dataSize = sizeof(aaMode);
    aaSpecialization.pData = &aaMode;
    tonemapStages[1].pSpecializationInfo = &aaSpecialization;

    VkPipelineVertexInputStateCreateInfo emptyVertexInput{};
    emptyVertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo stripAssembly{};
    stripAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    stripAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    stripAssembly.primitiveRestartEnable = VK_FALSE;

    VkGraphicsPipelineCreateInfo tonemapPipelineInfo{};
    tonemapPipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    tonemapPipelineInfo.stageCount = 2;
    tonemapPipelineInfo.pStages = tonemapStages;
    tonemapPipelineInfo.pVertexInputState = &emptyVertexInput;
    tonemapPipelineInfo.pInputAssemblyState = &stripAssembly;
    tonemapPipelineInfo.pViewportState = &viewportState;
    tonemapPipelineInfo.pRasterizationState = &rasterization;
    tonemapPipelineInfo.pMultisampleState = &multisample;
    tonemapPipelineInfo.pDepthStencilState = &triangleDepthStencil;
    tonemapPipelineInfo.pColorBlendState = &colorBlend;
    tonemapPipelineInfo.pDynamicState = &dynamicState;
    tonemapPipelineInfo.layout = impl_->tonemapPipelineLayout;
    tonemapPipelineInfo.renderPass = impl_->renderPass;
    tonemapPipelineInfo.subpass = 0;
    if (vkCreateGraphicsPipelines(impl_->device, VK_NULL_HANDLE, 1, &tonemapPipelineInfo, nullptr, &impl_->tonemapPipeline) != VK_SUCCESS) {
        impl_->setError("vkCreateGraphicsPipelines(tonemap) failed");
        shutdown();
        return false;
    }

    if (!impl_->rebuildQualityTargets()) {
        shutdown();
        return false;
    }

    graphics_internal::TextRenderer::Context textContext;
    textContext.device = impl_->device;
    textContext.physicalDevice = impl_->physicalDevice;
    textContext.memoryProperties = impl_->memoryProperties;
    textContext.renderPass = impl_->renderPass;
    textContext.shaderDirectory = shaderDirectory;
    if (!impl_->textRenderer.initialize(textContext)) {
        impl_->setError("Text renderer initialization failed");
        shutdown();
        return false;
    }

    guard.committed = true;
    return true;
}


void Graphics::shutdown() noexcept {
    if (impl_) {
        impl_->destroy();
    }
}

bool Graphics::setQuality(GraphicsQuality quality) {
    if (!impl_) {
        return false;
    }
    if (!initialized()) {
        impl_->setError("Graphics is not initialized");
        return false;
    }
    if (quality == impl_->activeQuality) {
        return true;
    }
    impl_->applyQualityPreset(quality);
    return impl_->rebuildQualityTargets();
}

void Graphics::setShadowConfig(const ShadowConfig& config) {
    if (!impl_) {
        return;
    }
    impl_->shadowConfig = config;
    impl_->shadowConfig.mapSize = std::max(1u, config.mapSize);
    impl_->shadowConfig.cascadeCount = std::max(1u, config.cascadeCount);
    impl_->shadowConfig.passCount = std::max(1u, config.passCount);
    impl_->shadowConfig.pcfRadius = std::min(8u, config.pcfRadius);
    impl_->shadowMapSize = impl_->shadowConfig.mapSize;
    impl_->shadowTaps = static_cast<int>((impl_->shadowConfig.pcfRadius * 2 + 1) * (impl_->shadowConfig.pcfRadius * 2 + 1));
    impl_->shadowStrength = impl_->shadowConfig.strength;
    if (initialized()) {
        impl_->rebuildQualityTargets();
    }
}

ShadowConfig Graphics::shadowConfig() const noexcept {
    return impl_ ? impl_->shadowConfig : ShadowConfig{};
}

void Graphics::enablePlayerUltraShadows(bool enabled) noexcept {
    if (impl_) {
        impl_->playerUltraShadows = enabled;
    }
}

void Graphics::setUiDrawList(UiDrawList drawList) {
    if (impl_) {
        impl_->uiDrawList = std::move(drawList);
    }
}

void Graphics::clearUiDrawList() noexcept {
    if (impl_) {
        impl_->uiDrawList = {};
    }
}


bool Graphics::playerUltraShadowsEnabled() const noexcept {
    return impl_ != nullptr && impl_->playerUltraShadows;
}

void Graphics::setVsync(bool enabled) noexcept {
    if (!impl_) {
        return;
    }
    impl_->deviceService.vsync = enabled;
    if (!impl_->headless && initialized()) {
        impl_->configureNativeSurface();
        impl_->recreateSwapchain();
    }
}

bool Graphics::vsync() const noexcept {
    return impl_ != nullptr && impl_->deviceService.vsync;
}


bool Graphics::initialized() const noexcept {
    return impl_ && impl_->instance != VK_NULL_HANDLE && impl_->device != VK_NULL_HANDLE && impl_->pipeline != VK_NULL_HANDLE;
}

bool Graphics::renderFrame() noexcept {
    if (!impl_ || !initialized()) {
        if (impl_) {
            impl_->lastError = "Graphics is not initialized";
        }
        return false;
    }
    LARGE_INTEGER startCounter{};
    LARGE_INTEGER endCounter{};
    LARGE_INTEGER frequency{};
    const bool timing = QueryPerformanceCounter(&startCounter) != 0 &&
                        QueryPerformanceFrequency(&frequency) != 0 && frequency.QuadPart > 0;
    const bool submitted = impl_->headless ? renderHeadlessFrame() : renderNativeFrame();
    if (timing && QueryPerformanceCounter(&endCounter) != 0) {
        const double milliseconds = static_cast<double>(endCounter.QuadPart - startCounter.QuadPart) *
                                    1000.0 / static_cast<double>(frequency.QuadPart);
        constexpr double smoothing = 0.9;
        impl_->avgFrameMs = impl_->avgFrameMs <= 0.0
            ? milliseconds
            : impl_->avgFrameMs * smoothing + milliseconds * (1.0 - smoothing);
    }
    return submitted;
}

bool Graphics::renderHeadlessFrame() noexcept {
    if (!impl_) {
        return false;
    }
    if (!initialized()) {
        impl_->lastError = "Graphics is not initialized";
        return false;
    }
    if (!impl_->headless) {
        impl_->lastError = "Headless frame requested for a native graphics context";
        return false;
    }
    if (!impl_->renderFrameCommands(impl_->framebuffer, impl_->renderWidth, impl_->renderHeight)) {

        return false;
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &impl_->commandBuffer;
    if (vkQueueSubmit(impl_->queue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
        impl_->lastError = "vkQueueSubmit failed";
        return false;
    }
    if (vkQueueWaitIdle(impl_->queue) != VK_SUCCESS) {
        impl_->lastError = "vkQueueWaitIdle failed";
        return false;
    }

    ++impl_->frameCount;
    return true;
}

bool Graphics::renderNativeFrame() noexcept {
    if (!impl_) {
        return false;
    }
    if (!initialized()) {
        impl_->lastError = "Graphics is not initialized";
        return false;
    }
    if (impl_->headless) {
        impl_->lastError = "Native frame requested for a headless graphics context";
        return false;
    }
    if (impl_->acquireNextImage == nullptr || impl_->queuePresent == nullptr || impl_->swapchainFramebuffers.empty()) {
        impl_->lastError = "Vulkan swapchain is not available";
        return false;
    }

#if defined(_WIN32)
    VkExtent2D windowExtent{};
    if (!getWindowExtent(impl_->window, windowExtent)) {
        impl_->lastError = "Native window has no drawable client area";
        return false;
    }
    if (windowExtent.width != impl_->swapchainExtent.width ||
        windowExtent.height != impl_->swapchainExtent.height) {
        traceFrame("recreate", impl_->swapchainExtent.width, impl_->swapchainExtent.height,
                   windowExtent.width, windowExtent.height, impl_->colorFormat, 0, VK_SUCCESS);
        if (!impl_->recreateSwapchain()) {
            return false;
        }
    }
#endif

    const std::uint32_t frameSlot = impl_->currentFrameSlot;
    // Throttle: this slot's previous GPU work must finish before its command
    // buffer and semaphores are reused. Fence stays signaled until the frame
    // actually submits, so early-outs (recreate/acquire failure) never wedge
    // the slot.
    if (vkWaitForFences(impl_->device, 1, &impl_->frameSync.fences[frameSlot], VK_TRUE, std::numeric_limits<std::uint64_t>::max()) != VK_SUCCESS) {
        impl_->lastError = "vkWaitForFences failed";
        return false;
    }
    // The renderer currently records all passes into one primary command
    // buffer, so a frame slot fence alone cannot protect it when two slots
    // overlap. Serialize reuse until command buffers are allocated per slot.
    if (vkQueueWaitIdle(impl_->queue) != VK_SUCCESS) {
        impl_->lastError = "vkQueueWaitIdle before command buffer reuse failed";
        return false;
    }

    std::uint32_t imageIndex = 0;
    VkResult acquireResult = VK_ERROR_OUT_OF_DATE_KHR;
    for (int attempt = 0; attempt < 2; ++attempt) {
        acquireResult = impl_->acquireNextImage(
            impl_->device,
            impl_->swapchain,
            std::numeric_limits<std::uint64_t>::max(),
            impl_->frameSync.acquire[frameSlot],
            VK_NULL_HANDLE,
            &imageIndex);
        if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR && attempt == 0) {
            if (!impl_->recreateSwapchain()) {
                return false;
            }
            continue;
        }
        break;
    }
    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        impl_->lastError = "Vulkan swapchain is out of date";
        return false;
    }
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
        impl_->lastError = "vkAcquireNextImageKHR failed";
        return false;
    }
    traceFrame("acquire", impl_->swapchainExtent.width, impl_->swapchainExtent.height,
               windowExtent.width, windowExtent.height, impl_->colorFormat, imageIndex, acquireResult);
    if (imageIndex >= impl_->swapchainFramebuffers.size()) {
        impl_->lastError = "Vulkan swapchain image index is invalid";
        return false;
    }

    if (!impl_->renderFrameCommands(impl_->swapchainFramebuffers[imageIndex], impl_->swapchainExtent.width, impl_->swapchainExtent.height)) {
        return false;
    }

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    if (vkResetFences(impl_->device, 1, &impl_->frameSync.fences[frameSlot]) != VK_SUCCESS) {
        impl_->lastError = "vkResetFences failed";
        return false;
    }
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &impl_->frameSync.acquire[frameSlot];
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &impl_->commandBuffer;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &impl_->frameSync.renderFinished[frameSlot];
    if (vkQueueSubmit(impl_->queue, 1, &submitInfo, impl_->frameSync.fences[frameSlot]) != VK_SUCCESS) {
        impl_->lastError = "vkQueueSubmit failed";
        return false;
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &impl_->frameSync.renderFinished[frameSlot];
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &impl_->swapchain;
    presentInfo.pImageIndices = &imageIndex;
    const VkResult presentResult = impl_->queuePresent(impl_->queue, &presentInfo);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR) {
        const bool recreated = impl_->recreateSwapchain();
        impl_->lastError = recreated
            ? "Vulkan swapchain was recreated after presentation"
            : "Vulkan swapchain presentation is out of date";
        return false;
    }
    if (presentResult != VK_SUCCESS && presentResult != VK_SUBOPTIMAL_KHR) {
        impl_->lastError = "vkQueuePresentKHR failed";
        return false;
    }

    ++impl_->frameCount;
    ++impl_->presentCount;
    impl_->currentFrameSlot = (frameSlot + 1) % Impl::maxFramesInFlight;
    traceFrame("present", impl_->swapchainExtent.width, impl_->swapchainExtent.height,
               windowExtent.width, windowExtent.height, impl_->colorFormat, imageIndex, presentResult);
    return true;
}

TextureHandle Graphics::createTexture2D(const TextureDesc& description) {
    if (!impl_) {
        return invalidTextureHandle;
    }
    if (!initialized()) {
        impl_->setError("Graphics is not initialized");
        return invalidTextureHandle;
    }
    if (impl_->nextTextureId == 0) {
        impl_->setError("Texture handle space is exhausted");
        return invalidTextureHandle;
    }

    Impl::TextureResource texture;
    if (!impl_->createTextureResource(description, texture)) {
        return invalidTextureHandle;
    }

    const std::uint64_t id = impl_->nextTextureId++;
    if (impl_->textures.find(id) != impl_->textures.end()) {
        graphics_internal::TextureManager::destroyResource(impl_->device, texture);
        impl_->setError("Texture handle allocation failed");
        return invalidTextureHandle;
    }
    try {
        const auto result = impl_->textures.emplace(id, std::move(texture));
        if (!result.second) {
            graphics_internal::TextureManager::destroyResource(impl_->device, texture);
            impl_->setError("Texture handle allocation failed");
            return invalidTextureHandle;
        }
    } catch (...) {
        const auto textureIt = impl_->textures.find(id);
        if (textureIt != impl_->textures.end()) {
            graphics_internal::TextureManager::destroyResource(impl_->device, textureIt->second);
            impl_->textures.erase(textureIt);
        } else {
        graphics_internal::TextureManager::destroyResource(impl_->device, texture);
        }
        impl_->setError("Texture resource allocation failed");
        return invalidTextureHandle;
    }

    const TextureHandle handle{id};
    if (!impl_->bindTextureDescriptor(handle)) {
        const auto textureIt = impl_->textures.find(id);
        if (textureIt != impl_->textures.end()) {
            graphics_internal::TextureManager::destroyResource(impl_->device, textureIt->second);
            impl_->textures.erase(textureIt);
        }
        return invalidTextureHandle;
    }
    return handle;
}

bool Graphics::updateTexture2D(
    TextureHandle handle,
    const void* pixels,
    std::size_t dataSize) {
    if (!impl_) {
        return false;
    }
    if (!initialized()) {
        impl_->setError("Graphics is not initialized");
        return false;
    }
    if (!handle.valid()) {
        impl_->setError("Texture handle is invalid");
        return false;
    }
    const auto textureIt = impl_->textures.find(handle.value());
    if (textureIt == impl_->textures.end()) {
        impl_->setError("Texture handle is invalid");
        return false;
    }
    return impl_->uploadTextureData(
        textureIt->second,
        pixels,
        dataSize,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

bool Graphics::setTexture2D(TextureHandle handle) noexcept {
    if (!impl_) {
        return false;
    }
    if (!initialized()) {
        impl_->lastError = "Graphics is not initialized";
        return false;
    }
    if (impl_->device == VK_NULL_HANDLE || vkDeviceWaitIdle(impl_->device) != VK_SUCCESS) {
        impl_->lastError = "vkDeviceWaitIdle(texture bind) failed";
        return false;
    }
    return impl_->bindTextureDescriptor(handle);
}

void Graphics::destroyTexture2D(TextureHandle handle) noexcept {
    if (!impl_ || !handle.valid()) {
        return;
    }
    if (!initialized()) {
        impl_->lastError = "Graphics is not initialized";
        return;
    }
    if (handle == impl_->defaultTexture) {
        impl_->lastError = "Default texture cannot be destroyed";
        return;
    }
    const auto textureIt = impl_->textures.find(handle.value());
    if (textureIt == impl_->textures.end()) {
        impl_->lastError = "Texture handle is invalid";
        return;
    }
    if (impl_->device == VK_NULL_HANDLE || vkDeviceWaitIdle(impl_->device) != VK_SUCCESS) {
        impl_->lastError = "vkDeviceWaitIdle(texture destroy) failed";
        return;
    }
    if (impl_->boundTexture == handle && !impl_->defaultTexture.valid()) {
        impl_->lastError = "Default texture is unavailable";
        return;
    }
    if (impl_->boundTexture == handle && !impl_->bindTextureDescriptor(impl_->defaultTexture)) {
        return;
    }
    graphics_internal::TextureManager::destroyResource(impl_->device, textureIt->second);
    impl_->textures.erase(textureIt);
    impl_->refreshSceneTextureBinding();
}

MeshHandle Graphics::createMesh(const MeshData& data) {
    if (!impl_) {
        return invalidMeshHandle;
    }
    if (!initialized()) {
        impl_->setError("Graphics is not initialized");
        return invalidMeshHandle;
    }
    if (data.vertices.empty() || data.indices.empty()) {
        impl_->setError("Mesh data must contain vertices and indices");
        return invalidMeshHandle;
    }
    for (std::uint32_t index : data.indices) {
        if (index >= data.vertices.size()) {
            impl_->setError("Mesh index references a vertex outside the vertex buffer");
            return invalidMeshHandle;
        }
    }
    for (const MeshVertex& vertex : data.vertices) {
        for (float component : vertex.position) {
            if (!std::isfinite(component)) {
                impl_->setError("Mesh vertex position contains a non-finite value");
                return invalidMeshHandle;
            }
        }
    }
    if (data.vertices.size() > std::numeric_limits<std::uint32_t>::max() ||
        data.indices.size() > std::numeric_limits<std::uint32_t>::max()) {
        impl_->setError("Mesh data exceeds 32-bit index limits");
        return invalidMeshHandle;
    }

    Impl::MeshResource mesh;
    const VkDeviceSize vertexSize = data.vertices.size() * sizeof(MeshVertex);
    const VkDeviceSize indexSize = data.indices.size() * sizeof(std::uint32_t);
    if (!impl_->createBufferWithData(
            vertexSize,
            data.vertices.data(),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            mesh.vertexBuffer,
            mesh.vertexMemory)) {
        graphics_internal::MeshRenderer::destroyResource(impl_->device, mesh);
        return invalidMeshHandle;
    }
    if (!impl_->createBufferWithData(
            indexSize,
            data.indices.data(),
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            mesh.indexBuffer,
            mesh.indexMemory)) {
        graphics_internal::MeshRenderer::destroyResource(impl_->device, mesh);
        return invalidMeshHandle;
    }
    mesh.vertexCount = static_cast<std::uint32_t>(data.vertices.size());
    mesh.indexCount = static_cast<std::uint32_t>(data.indices.size());
    engineLog("mesh created: id=%llu vertices=%zu indices=%zu",
              static_cast<unsigned long long>(impl_->nextMeshId),
              data.vertices.size(),
              data.indices.size());

    const std::uint64_t id = impl_->nextMeshId++;
    try {
        const auto result = impl_->meshes.emplace(id, std::move(mesh));
        if (!result.second) {
            impl_->setError("Mesh handle allocation failed");
            return invalidMeshHandle;
        }
    } catch (...) {
        impl_->setError("Mesh resource allocation failed");
        return invalidMeshHandle;
    }
    return MeshHandle{id};
}

void Graphics::destroyMesh(MeshHandle handle) noexcept {
    if (!impl_ || !handle.valid()) {
        return;
    }
    if (!initialized()) {
        impl_->lastError = "Graphics is not initialized";
        return;
    }
    const auto meshIt = impl_->meshes.find(handle.value());
    if (meshIt == impl_->meshes.end()) {
        impl_->lastError = "Mesh handle is invalid";
        return;
    }
    if (impl_->device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(impl_->device);
    }
    graphics_internal::MeshRenderer::destroyResource(impl_->device, meshIt->second);
    impl_->meshes.erase(meshIt);
}

std::uint64_t Graphics::meshVertexCount(MeshHandle handle) const noexcept {
    if (!impl_ || !handle.valid()) {
        return 0;
    }
    const auto meshIt = impl_->meshes.find(handle.value());
    return meshIt == impl_->meshes.end() ? 0 : meshIt->second.vertexCount;
}

FontHandle Graphics::createFontFromTtf(const std::string& ttfPath, float pixelSize) {
    if (!impl_) {
        return invalidFontHandle;
    }
    if (!initialized()) {
        impl_->setError("Graphics is not initialized");
        return invalidFontHandle;
    }
    const std::vector<std::uint8_t> bytes = graphics_internal::readFileBytes(ttfPath);
    if (bytes.empty()) {
        impl_->setError("TTF file could not be read");
        return invalidFontHandle;
    }
    graphics_internal::BakedFontAtlas baked = graphics_internal::bakeTtfAtlas(bytes, pixelSize);
    if (baked.pixels.empty() || baked.atlas.glyphs.empty()) {
        impl_->setError("TTF atlas baking failed");
        return invalidFontHandle;
    }

    TextureDesc textureDescription;
    textureDescription.width = baked.width;
    textureDescription.height = baked.height;
    textureDescription.format = TextureFormat::Rgba8Unorm;
    textureDescription.pixels = baked.pixels.data();
    textureDescription.dataSize = baked.pixels.size();
    const TextureHandle texture = createTexture2D(textureDescription);
    if (!texture.valid()) {
        return invalidFontHandle;
    }

    Impl::FontResource resource;
    resource.texture = texture;
    resource.atlas = baked.atlas;
    resource.atlas.texture = texture;
    const std::uint64_t id = impl_->nextFontId++;
    impl_->fonts[id] = std::move(resource);
    return FontHandle{id};
}

FontHandle Graphics::createFontFromMsdf(const std::string& jsonPath, const std::string& pngPath) {
    if (!impl_) {
        return invalidFontHandle;
    }
    if (!initialized()) {
        impl_->setError("Graphics is not initialized");
        return invalidFontHandle;
    }
    const std::string json = graphics_internal::readFileText(jsonPath);
    const std::vector<std::uint8_t> png = graphics_internal::readFileBytes(pngPath);
    if (json.empty() || png.empty()) {
        impl_->setError("MSDF atlas files could not be read");
        return invalidFontHandle;
    }
    graphics_internal::BakedFontAtlas baked = graphics_internal::loadMsdfAtlas(json, png);
    if (baked.pixels.empty() || baked.atlas.glyphs.empty()) {
        impl_->setError("MSDF atlas loading failed");
        return invalidFontHandle;
    }

    TextureDesc textureDescription;
    textureDescription.width = baked.width;
    textureDescription.height = baked.height;
    textureDescription.format = TextureFormat::Rgba8Unorm;
    textureDescription.pixels = baked.pixels.data();
    textureDescription.dataSize = baked.pixels.size();
    const TextureHandle texture = createTexture2D(textureDescription);
    if (!texture.valid()) {
        return invalidFontHandle;
    }

    Impl::FontResource resource;
    resource.texture = texture;
    resource.atlas = baked.atlas;
    resource.atlas.texture = texture;
    const std::uint64_t id = impl_->nextFontId++;
    impl_->fonts[id] = std::move(resource);
    return FontHandle{id};
}

void Graphics::destroyFont(FontHandle handle) noexcept {
    if (!impl_ || !handle.valid()) {
        return;
    }
    const auto fontIt = impl_->fonts.find(handle.value());
    if (fontIt == impl_->fonts.end()) {
        return;
    }
    destroyTexture2D(fontIt->second.texture);
    impl_->fonts.erase(fontIt);
}

bool Graphics::fontValid(FontHandle handle) const noexcept {
    return impl_ != nullptr && handle.valid() && impl_->fonts.count(handle.value()) != 0;
}


bool Graphics::render(const CameraState& camera, const std::vector<RenderItem>& items) {
    return render(camera, EnvironmentState{}, items);
}

bool Graphics::render(const CameraState& camera, const EnvironmentState& environment, const std::vector<RenderItem>& items) {
    if (!impl_) {
        return false;
    }
    if (!initialized()) {
        impl_->setError("Graphics is not initialized");
        return false;
    }
    impl_->pendingCamera = camera;
    impl_->environment = environment;
    Impl::multiplyMat4(camera.projectionMatrix, camera.viewMatrix, impl_->pendingViewProj);
    std::memcpy(impl_->pendingProjection, camera.projectionMatrix, sizeof(impl_->pendingProjection));
    impl_->pendingItems = items;
    impl_->drawDebugTriangle = items.empty();
    return renderFrame();
}

bool Graphics::readbackPixels(std::vector<std::uint8_t>& pixels) const {
    if (!impl_ || !initialized()) {
        return false;
    }
    if (!impl_->headless || impl_->colorImage == VK_NULL_HANDLE) {
        return false;
    }

    const VkDeviceSize byteCount =
        static_cast<VkDeviceSize>(impl_->renderWidth) * static_cast<VkDeviceSize>(impl_->renderHeight) * 4;

    pixels.assign(static_cast<std::size_t>(byteCount), 0);

    VkBuffer dstBuffer = VK_NULL_HANDLE;
    VkDeviceMemory dstMemory = VK_NULL_HANDLE;
    auto destroyReadback = [&]() noexcept {
        if (dstBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(impl_->device, dstBuffer, nullptr);
        }
        if (dstMemory != VK_NULL_HANDLE) {
            vkFreeMemory(impl_->device, dstMemory, nullptr);
        }
    };

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = byteCount;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(impl_->device, &bufferInfo, nullptr, &dstBuffer) != VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(impl_->device, dstBuffer, &requirements);
    std::uint32_t memoryType = 0;
    if (!findMemoryType(
            impl_->memoryProperties,
            requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            memoryType)) {
        destroyReadback();
        return false;
    }

    VkMemoryAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = requirements.size;
    allocateInfo.memoryTypeIndex = memoryType;
    if (vkAllocateMemory(impl_->device, &allocateInfo, nullptr, &dstMemory) != VK_SUCCESS) {
        destroyReadback();
        return false;
    }
    if (vkBindBufferMemory(impl_->device, dstBuffer, dstMemory, 0) != VK_SUCCESS) {
        destroyReadback();
        return false;
    }

    if (vkResetCommandBuffer(impl_->commandBuffer, 0) != VK_SUCCESS) {
        destroyReadback();
        return false;
    }
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(impl_->commandBuffer, &beginInfo) != VK_SUCCESS) {
        destroyReadback();
        return false;
    }

    VkImageMemoryBarrier toTransfer{};
    toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toTransfer.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.image = impl_->colorImage;
    toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toTransfer.subresourceRange.levelCount = 1;
    toTransfer.subresourceRange.layerCount = 1;
    toTransfer.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(
        impl_->commandBuffer,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &toTransfer);

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {impl_->renderWidth, impl_->renderHeight, 1};

    vkCmdCopyImageToBuffer(impl_->commandBuffer, impl_->colorImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dstBuffer, 1, &region);

    VkImageMemoryBarrier backToAttachment{};
    backToAttachment.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    backToAttachment.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    backToAttachment.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    backToAttachment.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    backToAttachment.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    backToAttachment.image = impl_->colorImage;
    backToAttachment.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    backToAttachment.subresourceRange.levelCount = 1;
    backToAttachment.subresourceRange.layerCount = 1;
    backToAttachment.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    backToAttachment.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    vkCmdPipelineBarrier(
        impl_->commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &backToAttachment);

    if (vkEndCommandBuffer(impl_->commandBuffer) != VK_SUCCESS) {
        destroyReadback();
        return false;
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &impl_->commandBuffer;
    if (vkQueueSubmit(impl_->queue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
        destroyReadback();
        return false;
    }
    if (vkQueueWaitIdle(impl_->queue) != VK_SUCCESS) {
        destroyReadback();
        return false;
    }

    void* mapped = nullptr;
    if (vkMapMemory(impl_->device, dstMemory, 0, byteCount, 0, &mapped) != VK_SUCCESS || mapped == nullptr) {
        destroyReadback();
        return false;
    }
    std::memcpy(pixels.data(), mapped, static_cast<std::size_t>(byteCount));
    vkUnmapMemory(impl_->device, dstMemory);
    destroyReadback();
    return true;
}

GraphicsStats Graphics::stats() const noexcept {
    GraphicsStats result;
    if (!impl_) {
        return result;
    }
    result.frameCount = impl_->frameCount;
    result.presentCount = impl_->presentCount;
    result.textureCount = static_cast<std::uint64_t>(impl_->textures.size());
    result.boundTextureId = impl_->boundTexture.value();
    result.textureUploadCount = impl_->textureUploadCount;
    result.headless = impl_->headless;
    result.nativeWindow = !impl_->headless;
    result.surfaceWidth = impl_->swapchainExtent.width;
    result.surfaceHeight = impl_->swapchainExtent.height;
    if (impl_->physicalDevice != VK_NULL_HANDLE) {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(impl_->physicalDevice, &properties);
        result.apiVersion = properties.apiVersion;
        result.driverVersion = properties.driverVersion;
    }
    result.memoryTypeCount = impl_->memoryProperties.memoryTypeCount;
    result.validationEnabled = impl_->validationEnabled;
    result.validationMessageCount = impl_->validationMessageCount;
    result.deviceName = impl_->deviceName;
    result.quality = static_cast<std::uint32_t>(impl_->activeQuality);
    result.hdrEnabled = impl_->hdrEnabled;
    result.reflectionsEnabled = impl_->reflectionsEnabled;
    result.shadowMapSize = impl_->shadowMapSize;
    result.textureScalePercent = impl_->textureScalePercent;
    result.antiAliasing = impl_->antiAliasing;
    result.avgFrameMs = impl_->avgFrameMs;
    for (std::uint32_t heap = 0; heap < impl_->memoryProperties.memoryHeapCount; ++heap) {
        if ((impl_->memoryProperties.memoryHeaps[heap].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0) {
            result.deviceLocalMemory += impl_->memoryProperties.memoryHeaps[heap].size;
        }
    }
    return result;
}

const std::string& Graphics::lastError() const noexcept {
    static const std::string emptyError;
    return impl_ ? impl_->lastError : emptyError;
}

}


