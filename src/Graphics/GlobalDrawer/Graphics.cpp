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
#include "Device/MemoryManager/MemoryManager.hpp"
#include "Device/SceneManager/Color/ColorTarget.hpp"
#include "Device/SceneManager/Depth/DepthTarget.hpp"
#include "Device/SceneManager/HeadlessTarget/HeadlessTarget.hpp"
#include "Device/SceneManager/SceneBindings/SceneBindings.hpp"
#include "Device/Init/RenderPass.hpp"
#include "Device/Swapchain/Swapchain.hpp"
#include "Support/Support.hpp"
#include "Text/FontAtlasBuilder.hpp"
#include "Hdr/HdrRenderer.hpp"
#include "Hdr/TaaRenderer.hpp"
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
#include <cmath>
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
    graphics_internal::SceneBindings scene;
#if defined(APORIA_EXPERIMENTAL)
    // EXPERIMENTAL per-item multi-layer materials (set = 1).
    VkDescriptorSetLayout materialDescriptorLayout = VK_NULL_HANDLE;
    VkDescriptorPool materialDescriptorPool = VK_NULL_HANDLE;
    std::unordered_map<std::uint64_t, VkDescriptorSet> materialSets;
    std::unordered_map<std::uint64_t, VkBuffer> materialBuffers;
    std::unordered_map<std::uint64_t, VkDeviceMemory> materialMemory;
    VkDescriptorSet defaultMaterialSet = VK_NULL_HANDLE;
    VkBuffer defaultMaterialBuffer = VK_NULL_HANDLE;
    VkDeviceMemory defaultMaterialMemory = VK_NULL_HANDLE;
    std::uint64_t nextMaterialId = 1;
#endif
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
    // UI pass (SDF rects + blur): separate pipeline from tonemap, alpha blended.
    VkShaderModule uiFragmentShader = VK_NULL_HANDLE;
    VkPipeline uiPipeline = VK_NULL_HANDLE;
    VkDescriptorPool tonemapDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout tonemapDescriptorLayout = VK_NULL_HANDLE;
    VkDescriptorSet tonemapDescriptorSet = VK_NULL_HANDLE;

    // Temporal anti-aliasing: a resolve pass reprojects the previous frame with
    // the scene depth. The scene renders with a sub-pixel jitter so the history
    // accumulates into a supersampled image. Resources live in TaaRenderer.
    VkShaderModule taaFragmentShader = VK_NULL_HANDLE;
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

    // Level-of-detail groups. A group handle resolves to one level mesh per
    // frame, picked by camera distance and the boundary shape.
    struct LodGroup {
        std::vector<std::uint64_t> meshes;  // level mesh ids, near -> far
        std::vector<float> maxDistances;    // range for every level except the last
        LodBoundary boundary = LodBoundary::Sphere;
        float center[3] = {0.0f, 0.0f, 0.0f};      // bounding volume center (mesh-local)
        float halfExtents[3] = {0.5f, 0.5f, 0.5f}; // bounding volume (mesh-local)
        float boundingRadius = 0.5f;
        std::uint32_t lastLevel = 0;
    };
    std::unordered_map<std::uint64_t, LodGroup> lodGroups;
    std::uint64_t lodDrawTriangles = 0;
    std::uint64_t lodFullTriangles = 0;
    std::uint64_t lodSwitchCount = 0;

    // Rewrites pendingItems in place: every LOD group id becomes the level id
    // for the current camera distance. Non-LOD items are left alone.
    void resolveLodGroups() noexcept {
        lodDrawTriangles = 0;
        lodFullTriangles = 0;
        if (lodGroups.empty()) {
            return;
        }
        const double cameraX = pendingCamera.position[0];
        const double cameraY = pendingCamera.position[1];
        const double cameraZ = pendingCamera.position[2];
        for (RenderItem& item : pendingItems) {
            const auto groupIt = lodGroups.find(item.meshId);
            if (groupIt == lodGroups.end()) {
                continue;
            }
            LodGroup& group = groupIt->second;
            const float* m = item.modelMatrix;
            const std::array<double, 3> axisX{m[0], m[1], m[2]};
            const std::array<double, 3> axisY{m[4], m[5], m[6]};
            const std::array<double, 3> axisZ{m[8], m[9], m[10]};
            const auto lengthOf = [](const std::array<double, 3>& v) {
                return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
            };
            const double scaleX = lengthOf(axisX);
            const double scaleY = lengthOf(axisY);
            const double scaleZ = lengthOf(axisZ);
            const double centerX = m[12] + axisX[0] * group.center[0] + axisY[0] * group.center[1] + axisZ[0] * group.center[2];
            const double centerY = m[13] + axisX[1] * group.center[0] + axisY[1] * group.center[1] + axisZ[1] * group.center[2];
            const double centerZ = m[14] + axisX[2] * group.center[0] + axisY[2] * group.center[1] + axisZ[2] * group.center[2];
            const double deltaX = cameraX - centerX;
            const double deltaY = cameraY - centerY;
            const double deltaZ = cameraZ - centerZ;

            double distance = 0.0;
            if (group.boundary == LodBoundary::Box && scaleX > 1.0e-6 && scaleY > 1.0e-6 && scaleZ > 1.0e-6) {
                const auto unit = [&](const std::array<double, 3>& v, double length) {
                    return std::array<double, 3>{v[0] / length, v[1] / length, v[2] / length};
                };
                const std::array<double, 3> ux = unit(axisX, scaleX);
                const std::array<double, 3> uy = unit(axisY, scaleY);
                const std::array<double, 3> uz = unit(axisZ, scaleZ);
                const double extentX = scaleX * group.halfExtents[0];
                const double extentY = scaleY * group.halfExtents[1];
                const double extentZ = scaleZ * group.halfExtents[2];
                const auto project = [&](const std::array<double, 3>& u) {
                    return deltaX * u[0] + deltaY * u[1] + deltaZ * u[2];
                };
                const double px = std::clamp(project(ux), -extentX, extentX);
                const double py = std::clamp(project(uy), -extentY, extentY);
                const double pz = std::clamp(project(uz), -extentZ, extentZ);
                const double closestX = ux[0] * px + uy[0] * py + uz[0] * pz;
                const double closestY = ux[1] * px + uy[1] * py + uz[1] * pz;
                const double closestZ = ux[2] * px + uy[2] * py + uz[2] * pz;
                const double ox = deltaX - closestX;
                const double oy = deltaY - closestY;
                const double oz = deltaZ - closestZ;
                distance = std::sqrt(ox * ox + oy * oy + oz * oz);
            } else {
                const double radius = group.boundingRadius * std::max({scaleX, scaleY, scaleZ});
                const double centerDistance = std::sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
                distance = std::max(0.0, centerDistance - radius);
            }

            std::uint32_t level = static_cast<std::uint32_t>(group.meshes.size() - 1);
            for (std::uint32_t candidate = 0; candidate + 1 < group.meshes.size(); ++candidate) {
                if (distance <= group.maxDistances[candidate]) {
                    level = candidate;
                    break;
                }
            }
            if (level != group.lastLevel) {
                ++lodSwitchCount;
                group.lastLevel = level;
            }

            const std::uint64_t fullId = group.meshes.front();
            const std::uint64_t drawId = group.meshes[level];
            item.meshId = drawId;
            const auto drawIt = meshes.find(drawId);
            const auto fullIt = meshes.find(fullId);
            lodDrawTriangles += drawIt != meshes.end() ? drawIt->second.indexCount / 3u : 0u;
            lodFullTriangles += fullIt != meshes.end() ? fullIt->second.indexCount / 3u : 0u;
        }
    }


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

    // Rebuilds the mesh pipelines with the current shadow-tap / reflection
    // specialization. Quality presets change these, so the pipelines must be
    // recompiled on setQuality (otherwise High never actually kicks in).
    bool rebuildMeshPipelines() {
        if (device == VK_NULL_HANDLE || meshPipelineLayout == VK_NULL_HANDLE ||
            meshVertexShader == VK_NULL_HANDLE || meshFragmentShader == VK_NULL_HANDLE) {
            return false;
        }
        vkDeviceWaitIdle(device);
        if (meshPipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, meshPipeline, nullptr);
            meshPipeline = VK_NULL_HANDLE;
        }
        if (hdrMeshPipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, hdrMeshPipeline, nullptr);
            hdrMeshPipeline = VK_NULL_HANDLE;
        }

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = meshVertexShader;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = meshFragmentShader;
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

        VkSpecializationMapEntry specEntries[2]{};
        specEntries[0].constantID = 0;
        specEntries[0].offset = 0;
        specEntries[0].size = sizeof(std::int32_t);
        specEntries[1].constantID = 1;
        specEntries[1].offset = sizeof(std::int32_t);
        specEntries[1].size = sizeof(float);
        float specReflection = reflectionsEnabled ? 1.0f : 0.0f;
        std::int32_t specTaps = shadowTaps;
        std::int32_t specData[2] = {specTaps, 0};
        std::memcpy(specData + 1, &specReflection, sizeof(float));
        VkSpecializationInfo specInfo{};
        specInfo.mapEntryCount = 2;
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
        pipelineInfo.layout = meshPipelineLayout;
        pipelineInfo.renderPass = renderPass;
        pipelineInfo.subpass = 0;
        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &meshPipeline) != VK_SUCCESS) {
            setError("vkCreateGraphicsPipelines(mesh, preset) failed");
            return false;
        }
        if (hdrRenderPass != VK_NULL_HANDLE) {
            pipelineInfo.renderPass = hdrRenderPass;
            if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &hdrMeshPipeline) != VK_SUCCESS) {
                setError("vkCreateGraphicsPipelines(hdr mesh, preset) failed");
                return false;
            }
        }
        return true;
    }


    bool createBufferWithData(
        VkDeviceSize size,
        const void* data,
        VkBufferUsageFlags usage,
        VkBuffer& buffer,
        VkDeviceMemory& memory) {
        graphics_internal::MemoryManager::Context context;
        context.device = device;
        context.memoryProperties = memoryProperties;
        context.commandBuffer = commandBuffer;
        context.queue = queue;
        if (!graphics_internal::MemoryManager::uploadBuffer(
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
        if (!graphics_internal::DepthTarget::create(
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

    // Column-major 4x4 inverse (gluInvertMatrix); returns false if singular.
    static bool invertMat4(const float* m, float* out) noexcept {
        const double inv[16] = {
            m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10],
            -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10],
            m[1]*m[6]*m[15] - m[1]*m[7]*m[14] - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7] - m[13]*m[3]*m[6],
            -m[1]*m[6]*m[11] + m[1]*m[7]*m[10] + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7] + m[9]*m[3]*m[6],
            -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10],
            m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10],
            -m[0]*m[6]*m[15] + m[0]*m[7]*m[14] + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7] + m[12]*m[3]*m[6],
            m[0]*m[6]*m[11] - m[0]*m[7]*m[10] - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7] - m[8]*m[3]*m[6],
            m[4]*m[9]*m[15] - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9],
            -m[0]*m[9]*m[15] + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9],
            m[0]*m[5]*m[15] - m[0]*m[7]*m[13] - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7] - m[12]*m[3]*m[5],
            -m[0]*m[5]*m[11] + m[0]*m[7]*m[9] + m[4]*m[1]*m[11] - m[4]*m[3]*m[9] - m[8]*m[1]*m[7] + m[8]*m[3]*m[5],
            -m[4]*m[9]*m[14] + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9],
            m[0]*m[9]*m[14] - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9],
            -m[0]*m[5]*m[14] + m[0]*m[6]*m[13] + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6] + m[12]*m[2]*m[5],
            m[0]*m[5]*m[10] - m[0]*m[6]*m[9] - m[4]*m[1]*m[10] + m[4]*m[2]*m[9] + m[8]*m[1]*m[6] - m[8]*m[2]*m[5],
        };
        const double determinant = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
        if (std::abs(determinant) < 1.0e-12) {
            return false;
        }
        const double scale = 1.0 / determinant;
        for (int index = 0; index < 16; ++index) {
            out[index] = static_cast<float>(inv[index] * scale);
        }
        return true;
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

    void destroyQualityTargets() noexcept {
        taa.shutdown();
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
            scene.releaseResources(device);
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
        if (!graphics_internal::ColorTarget::create(
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
        if (!graphics_internal::RenderPassFactory::create(
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
        graphics_internal::TextureManager::CreateContext context;
        context.device = device;
        context.physicalDevice = physicalDevice;
        context.memoryProperties = memoryProperties;
        context.commandBuffer = commandBuffer;
        context.queue = queue;

        TextureResource texture;
        if (!graphics_internal::TextureManager::createSolid(context, 255, 255, 255, 255, texture)) {
            setError("Default texture creation failed");
            return false;
        }
        ++textureUploadCount;
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

#if defined(APORIA_EXPERIMENTAL)
    bool createMaterialLayout() {
        if (materialDescriptorLayout != VK_NULL_HANDLE) {
            return true;
        }
        VkDescriptorSetLayoutBinding bindings[7]{};
        for (int index = 0; index < 6; ++index) {
            bindings[index].binding = static_cast<std::uint32_t>(index);
            bindings[index].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            bindings[index].descriptorCount = 1;
            bindings[index].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        bindings[6].binding = 6;
        bindings[6].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[6].descriptorCount = 1;
        bindings[6].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 7;
        layoutInfo.pBindings = bindings;
        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &materialDescriptorLayout) != VK_SUCCESS) {
            setError("material descriptor layout creation failed");
            return false;
        }
        return true;
    }

    bool createMaterialPool() {
        if (materialDescriptorPool != VK_NULL_HANDLE) {
            return true;
        }
        VkDescriptorPoolSize poolSizes[2]{};
        poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSizes[0].descriptorCount = 64 * 6;
        poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSizes[1].descriptorCount = 64;
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        poolInfo.maxSets = 64;
        poolInfo.poolSizeCount = 2;
        poolInfo.pPoolSizes = poolSizes;
        if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &materialDescriptorPool) != VK_SUCCESS) {
            setError("material descriptor pool creation failed");
            return false;
        }
        return true;
    }

    VkDescriptorSet buildMaterialSet(const MaterialDesc& description, VkBuffer& outBuffer, VkDeviceMemory& outMemory) {
        outBuffer = VK_NULL_HANDLE;
        outMemory = VK_NULL_HANDLE;
        VkDescriptorSet set = VK_NULL_HANDLE;
        VkDescriptorSetAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocateInfo.descriptorPool = materialDescriptorPool;
        allocateInfo.descriptorSetCount = 1;
        allocateInfo.pSetLayouts = &materialDescriptorLayout;
        if (vkAllocateDescriptorSets(device, &allocateInfo, &set) != VK_SUCCESS) {
            return VK_NULL_HANDLE;
        }

        // Per-layer UV transform UBO (binding 6).
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = 32; // 2 vec4
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device, &bufferInfo, nullptr, &outBuffer) != VK_SUCCESS) {
            return VK_NULL_HANDLE;
        }
        VkMemoryRequirements bufferRequirements{};
        vkGetBufferMemoryRequirements(device, outBuffer, &bufferRequirements);
        std::uint32_t memoryType = 0;
        if (!findMemoryType(memoryProperties, bufferRequirements.memoryTypeBits,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, memoryType)) {
            return VK_NULL_HANDLE;
        }
        VkMemoryAllocateInfo bufferAllocate{};
        bufferAllocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        bufferAllocate.allocationSize = bufferRequirements.size;
        bufferAllocate.memoryTypeIndex = memoryType;
        if (vkAllocateMemory(device, &bufferAllocate, nullptr, &outMemory) != VK_SUCCESS ||
            vkBindBufferMemory(device, outBuffer, outMemory, 0) != VK_SUCCESS) {
            return VK_NULL_HANDLE;
        }
        void* mapped = nullptr;
        if (vkMapMemory(device, outMemory, 0, 32, 0, &mapped) == VK_SUCCESS && mapped != nullptr) {
            float uv[8] = {1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f};
            for (int index = 0; index < 4; ++index) {
                uv[index] = description.uvScale[index];
                uv[4 + index] = description.uvOffset[index];
            }
            std::memcpy(mapped, uv, sizeof(uv));
            vkUnmapMemory(device, outMemory);
        }

        const auto defaultIt = textures.find(defaultTexture.value());
        const auto& fallback = defaultIt->second;
        VkDescriptorImageInfo imageInfos[6]{};
        std::array<VkWriteDescriptorSet, 7> writes{};
        for (int index = 0; index < 6; ++index) {
            const TextureHandle handle = index < 4 ? description.diffuse[index] : description.mask[index - 4];
            const auto it = textures.find(handle.value());
            const auto& resource = it != textures.end() ? it->second : fallback;
            imageInfos[index].sampler = resource.sampler;
            imageInfos[index].imageView = resource.view;
            imageInfos[index].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            writes[index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[index].dstSet = set;
            writes[index].dstBinding = static_cast<std::uint32_t>(index);
            writes[index].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[index].descriptorCount = 1;
            writes[index].pImageInfo = &imageInfos[index];
        }
        VkDescriptorBufferInfo layerDescriptor{};
        layerDescriptor.buffer = outBuffer;
        layerDescriptor.offset = 0;
        layerDescriptor.range = 32;
        writes[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[6].dstSet = set;
        writes[6].dstBinding = 6;
        writes[6].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[6].descriptorCount = 1;
        writes[6].pBufferInfo = &layerDescriptor;
        vkUpdateDescriptorSets(device, 7, writes.data(), 0, nullptr);
        return set;
    }

    bool createDefaultMaterial() {
        if (!createMaterialLayout() || !createMaterialPool()) {
            return false;
        }
        defaultMaterialSet = buildMaterialSet(MaterialDesc{}, defaultMaterialBuffer, defaultMaterialMemory);
        if (defaultMaterialSet == VK_NULL_HANDLE) {
            setError("default material creation failed");
            return false;
        }
        return true;
    }

    void destroyMaterialResources() noexcept {
        if (device != VK_NULL_HANDLE) {
            for (const auto& entry : materialBuffers) {
                if (entry.second != VK_NULL_HANDLE) vkDestroyBuffer(device, entry.second, nullptr);
            }
            for (const auto& entry : materialMemory) {
                if (entry.second != VK_NULL_HANDLE) vkFreeMemory(device, entry.second, nullptr);
            }
            if (defaultMaterialBuffer != VK_NULL_HANDLE) vkDestroyBuffer(device, defaultMaterialBuffer, nullptr);
            if (defaultMaterialMemory != VK_NULL_HANDLE) vkFreeMemory(device, defaultMaterialMemory, nullptr);
            if (materialDescriptorPool != VK_NULL_HANDLE) {
                vkDestroyDescriptorPool(device, materialDescriptorPool, nullptr);
            }
            if (materialDescriptorLayout != VK_NULL_HANDLE) {
                vkDestroyDescriptorSetLayout(device, materialDescriptorLayout, nullptr);
            }
        }
        materialDescriptorPool = VK_NULL_HANDLE;
        materialDescriptorLayout = VK_NULL_HANDLE;
        materialSets.clear();
        materialBuffers.clear();
        materialMemory.clear();
        defaultMaterialSet = VK_NULL_HANDLE;
        defaultMaterialBuffer = VK_NULL_HANDLE;
        defaultMaterialMemory = VK_NULL_HANDLE;
    }
#endif

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
        scene.bindAlbedo(textureIt->second.view, textureIt->second.sampler);
        return true;
    }

    // Sun view-projection keeps the light camera aimed at the scene center;
    // the light travels opposite to the direction toward the sun.
    void buildSunMatrix(float outMatrix[16]) const noexcept {
        graphics_internal::ShadowRenderer::buildSunMatrix(
            environment.sunDirection, pendingCamera.position, shadowExtent, shadowMapSize, outMatrix);
    }


    void writeSceneUniforms(std::uint32_t slot, const float sunMatrix[16]) {
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
        uniforms.shadingParams[1] = shadowsEnabled ? shadowStrength : 1.0f;
        uniforms.shadingParams[2] = environment.mirrorZ;
        uniforms.shadingParams[3] = shadowMapSize > 0 ? 1.0f / static_cast<float>(shadowMapSize) : 0.0f;
        uniforms.screenParams[0] = static_cast<float>(hdrEnabled ? hdrWidth : (headless ? renderWidth : swapchainExtent.width));
        uniforms.screenParams[1] = static_cast<float>(hdrEnabled ? hdrHeight : (headless ? renderHeight : swapchainExtent.height));
        uniforms.shadowParams[0] = static_cast<float>(shadowConfig.filter);
        uniforms.shadowParams[1] = static_cast<float>(shadowConfig.blockerSamples);
        uniforms.shadowParams[2] = static_cast<float>(shadowConfig.filterSamples);
        uniforms.shadowParams[3] = shadowConfig.contactShadows ? shadowConfig.contactDistance : 0.0f;
        scene.writeUniforms(slot, &uniforms, sizeof(uniforms));
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

        if (hdrEnabled && static_cast<AntiAliasingMode>(antiAliasing) == AntiAliasingMode::TAA) {
            graphics_internal::TaaRenderer::Context taaContext;
            taaContext.device = device;
            taaContext.memoryProperties = memoryProperties;
            taaContext.commandBuffer = commandBuffer;
            taaContext.queue = queue;
            taaContext.renderPass = hdrRenderPass;
            taaContext.hdrFormat = hdrFormat;
            taaContext.depthFormat = depthFormat;
            taaContext.vertexShader = tonemapVertexShader;
            taaContext.fragmentShader = taaFragmentShader;
            taaContext.colorView = hdrView;
            taaContext.depthView = hdrDepthView;
            taaContext.colorSampler = hdrSampler;
            if (!taa.initialize(taaContext, hdrWidth, hdrHeight)) {
                setError("TAA initialization failed");
                return false;
            }
            // Tonemap samples the resolved image while TAA is active.
            if (tonemapDescriptorSet != VK_NULL_HANDLE) {
                VkDescriptorImageInfo resolveInfo{};
                resolveInfo.sampler = hdrSampler;
                resolveInfo.imageView = taa.resolveView();
                resolveInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                VkWriteDescriptorSet write{};
                write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.dstSet = tonemapDescriptorSet;
                write.dstBinding = 0;
                write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                write.descriptorCount = 1;
                write.pImageInfo = &resolveInfo;
                vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
            }
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
        const auto defaultIt = textures.find(defaultTexture.value());
        const auto textureIt = textures.find(boundTexture.value());
        const auto& albedoResource = textureIt != textures.end() ? textureIt->second : defaultIt->second;
        if (!scene.allocate(device, memoryProperties, sizeof(SceneUniforms),
                            albedoResource.view, albedoResource.sampler,
                            shadow.view, shadow.sampler)) {
            return false;
        }
        if (reflectionsEnabled && reflectView != VK_NULL_HANDLE) {
            scene.bindReflection(reflectView, reflectSampler);
        }
        return true;
    }

    bool configureNativeSurface() {
        graphics_internal::SwapchainState state{
            surface, colorFormat, swapchainColorSpace, presentMode, swapchain,
            swapchainImages, swapchainViews, swapchainFramebuffers, swapchainExtent,
            swapchainDepthViews, swapchainDepthImages, swapchainDepthMemory};
        graphics_internal::SwapchainContext context;
        context.device = device;
        context.physicalDevice = physicalDevice;
        context.memoryProperties = memoryProperties;
        context.graphicsFamily = graphicsFamily;
        context.vsync = deviceService.vsync;
        context.window = window;
        context.api = {getPhysicalDeviceSurfaceSupport, getPhysicalDeviceSurfaceCapabilities,
                       getPhysicalDeviceSurfaceFormats, getPhysicalDeviceSurfacePresentModes,
                       createSwapchain, destroySwapchain, getSwapchainImages};
        context.state = &state;
        context.error = &lastError;
        return graphics_internal::configureSurface(context);
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
        if (!graphics_internal::RenderPassFactory::create(
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
        if (!graphics_internal::RenderPassFactory::create(
                device, hdrFormat, depthFormat, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, hdrRenderPass)) {
            setError("vkCreateRenderPass(hdr) failed");
            return false;
        }
        return true;
    }


    void destroyHeadlessTarget() noexcept {
        graphics_internal::HeadlessTarget::destroy(device, framebuffer, colorView, colorImage, colorMemory);
    }

    bool createHeadlessTarget() {
        if (!ensureDepthForExtent(renderWidth, renderHeight)) {
            return false;
        }
        if (!graphics_internal::HeadlessTarget::create(
                device, memoryProperties, colorFormat, {renderWidth, renderHeight}, renderPass, depthView,
                colorImage, colorMemory, colorView, framebuffer)) {
            setError("Headless target creation failed");
            return false;
        }
        return true;
    }

    void destroySwapchainResources() noexcept {
        graphics_internal::SwapchainState state{
            surface, colorFormat, swapchainColorSpace, presentMode, swapchain,
            swapchainImages, swapchainViews, swapchainFramebuffers, swapchainExtent,
            swapchainDepthViews, swapchainDepthImages, swapchainDepthMemory};
        graphics_internal::SwapchainContext context;
        context.device = device;
        context.api.destroySwapchain = destroySwapchain;
        context.state = &state;
        graphics_internal::destroySwapchain(context);
    }

    bool createSwapchainResources() {
        if (depthFormat == VK_FORMAT_UNDEFINED) {
            depthFormat = chooseDepthFormat();
            if (depthFormat == VK_FORMAT_UNDEFINED) {
                setError("No supported depth attachment format");
                return false;
            }
        }
        graphics_internal::SwapchainState state{
            surface, colorFormat, swapchainColorSpace, presentMode, swapchain,
            swapchainImages, swapchainViews, swapchainFramebuffers, swapchainExtent,
            swapchainDepthViews, swapchainDepthImages, swapchainDepthMemory};
        graphics_internal::SwapchainContext context;
        context.device = device;
        context.physicalDevice = physicalDevice;
        context.memoryProperties = memoryProperties;
        context.graphicsFamily = graphicsFamily;
        context.window = window;
        context.renderPass = renderPass;
        context.depthFormat = depthFormat;
        context.api = {getPhysicalDeviceSurfaceSupport, getPhysicalDeviceSurfaceCapabilities,
                       getPhysicalDeviceSurfaceFormats, getPhysicalDeviceSurfacePresentModes,
                       createSwapchain, destroySwapchain, getSwapchainImages};
        context.state = &state;
        context.error = &lastError;
        if (!graphics_internal::createSwapchain(context)) {
            return false;
        }
        engineLog("swapchain created: %ux%u images=%zu fmt=%d present=%d",
                  state.extent.width, state.extent.height, state.images.size(),
                  static_cast<int>(colorFormat), static_cast<int>(presentMode));
        return true;
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
        context.descriptorSet = scene.set(0);
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
        context.uiPipeline = uiPipeline;
        context.pipelineLayout = tonemapPipelineLayout;
        context.descriptorSet = tonemapDescriptorSet;
        context.antiAliasingStrength = antiAliasingStrength;
        context.antiAliasingMode = static_cast<std::int32_t>(antiAliasing);
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

        const std::uint32_t uniformSlot = currentFrameSlot % graphics_internal::SceneBindings::kSlots;
#if defined(APORIA_EXPERIMENTAL)
        const auto* materialSetsArg = &materialSets;
        const VkDescriptorSet defaultMaterialArg = defaultMaterialSet;
#else
        const std::unordered_map<std::uint64_t, VkDescriptorSet>* materialSetsArg = nullptr;
        const VkDescriptorSet defaultMaterialArg = VK_NULL_HANDLE;
#endif
        float sunMatrix[16];
        buildSunMatrix(sunMatrix);
        writeSceneUniforms(uniformSlot, sunMatrix);

        if (shadowsEnabled && shadowPipeline != VK_NULL_HANDLE && !pendingItems.empty()) {
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
                scene.set(uniformSlot), reflectViewProj,
                pendingItems, meshes, VK_NULL_HANDLE, materialSetsArg, defaultMaterialArg);
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
                    scene.set(uniformSlot), pendingViewProj,
                    pendingItems, meshes, mainMeshPipeline, materialSetsArg, defaultMaterialArg);
            }
            vkCmdEndRenderPass(commandBuffer);
        }

        if (useHdr && static_cast<AntiAliasingMode>(antiAliasing) == AntiAliasingMode::TAA && taa.valid()) {
            taa.record(commandBuffer, hdrDepthImage, taaPrevViewProj, taaInvViewProj, antiAliasingStrength);
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
            lodGroups.clear();
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
            scene.destroyLayout(device);
#if defined(APORIA_EXPERIMENTAL)
            destroyMaterialResources();
#endif
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
            taa.shutdown();
            if (taaFragmentShader != VK_NULL_HANDLE) {
                vkDestroyShaderModule(device, taaFragmentShader, nullptr);
                taaFragmentShader = VK_NULL_HANDLE;
            }
            if (uiPipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(device, uiPipeline, nullptr);
                uiPipeline = VK_NULL_HANDLE;
            }
            if (uiFragmentShader != VK_NULL_HANDLE) {
                vkDestroyShaderModule(device, uiFragmentShader, nullptr);
                uiFragmentShader = VK_NULL_HANDLE;
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
            lodGroups.clear();
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
    impl_->antiAliasingStrength = std::clamp(config.antiAliasingStrength, 0.0f, 1.0f);
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
    impl_->shadowsEnabled = impl_->shadowConfig.enabled;
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
#if defined(APORIA_EXPERIMENTAL)
    if (!impl_->createDefaultMaterial()) {
        shutdown();
        return false;
    }
#endif
    if (!impl_->createDescriptorResources()) {
        shutdown();
        return false;
    }
    // Scene layout must exist before the mesh/shadow pipelines referencing it.
    if (!impl_->scene.createLayout(impl_->device)) {
        impl_->setError("vkCreateDescriptorSetLayout(scene) failed");
        shutdown();
        return false;
    }
    const VkDescriptorSetLayout sceneLayout = impl_->scene.layout();

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
#if defined(APORIA_EXPERIMENTAL)
    VkDescriptorSetLayout meshSetLayouts[2] = {sceneLayout, impl_->materialDescriptorLayout};
    meshLayoutInfo.setLayoutCount = 2;
    meshLayoutInfo.pSetLayouts = meshSetLayouts;
#else
    meshLayoutInfo.setLayoutCount = 1;
    meshLayoutInfo.pSetLayouts = &sceneLayout;
#endif
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
    shadowLayoutInfo.pSetLayouts = &sceneLayout;
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

    const std::vector<std::uint32_t> taaFragmentCode = readSpirv(shaderDirectory + "/taa.frag.spv", spirvLoaded);
    if (!spirvLoaded) {
        impl_->setError("Failed to load compiled TAA shader");
        shutdown();
        return false;
    }
    VkShaderModuleCreateInfo taaFragmentModuleInfo{};
    taaFragmentModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    taaFragmentModuleInfo.codeSize = taaFragmentCode.size() * sizeof(std::uint32_t);
    taaFragmentModuleInfo.pCode = taaFragmentCode.data();
    if (vkCreateShaderModule(impl_->device, &taaFragmentModuleInfo, nullptr, &impl_->taaFragmentShader) != VK_SUCCESS) {
        impl_->setError("vkCreateShaderModule(taa fragment) failed");
        shutdown();
        return false;
    }

    const std::vector<std::uint32_t> uiFragmentCode = readSpirv(shaderDirectory + "/ui.frag.spv", spirvLoaded);
    if (!spirvLoaded) {
        impl_->setError("Failed to load compiled UI shader");
        shutdown();
        return false;
    }
    VkShaderModuleCreateInfo uiFragmentModuleInfo{};
    uiFragmentModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    uiFragmentModuleInfo.codeSize = uiFragmentCode.size() * sizeof(std::uint32_t);
    uiFragmentModuleInfo.pCode = uiFragmentCode.data();
    if (vkCreateShaderModule(impl_->device, &uiFragmentModuleInfo, nullptr, &impl_->uiFragmentShader) != VK_SUCCESS) {
        impl_->setError("vkCreateShaderModule(ui fragment) failed");
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
    tonemapPushRange.size = sizeof(graphics_internal::RectRenderer::PushConstants) + 16;
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

    // UI pipeline: same layout/render pass and vertex shader as tonemap, but the
    // SDF/blur fragment shader composited with straight alpha over the frame.
    VkPipelineShaderStageCreateInfo uiStages[2]{};
    uiStages[0] = tonemapStages[0];
    uiStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    uiStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    uiStages[1].module = impl_->uiFragmentShader;
    uiStages[1].pName = "main";
    VkPipelineColorBlendAttachmentState uiBlendAttachment{};
    uiBlendAttachment.blendEnable = VK_TRUE;
    uiBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    uiBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    uiBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    uiBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    uiBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    uiBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    uiBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                       VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo uiColorBlend{};
    uiColorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    uiColorBlend.attachmentCount = 1;
    uiColorBlend.pAttachments = &uiBlendAttachment;
    VkGraphicsPipelineCreateInfo uiPipelineInfo = tonemapPipelineInfo;
    uiPipelineInfo.pStages = uiStages;
    uiPipelineInfo.pColorBlendState = &uiColorBlend;
    if (vkCreateGraphicsPipelines(impl_->device, VK_NULL_HANDLE, 1, &uiPipelineInfo, nullptr, &impl_->uiPipeline) != VK_SUCCESS) {
        impl_->setError("vkCreateGraphicsPipelines(ui) failed");
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
    if (!impl_->rebuildQualityTargets()) {
        return false;
    }
    return impl_->rebuildMeshPipelines();
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
    impl_->shadowsEnabled = impl_->shadowConfig.enabled;
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

void Graphics::setAntiAliasing(AntiAliasingMode mode) {
    if (!impl_) {
        return;
    }
    impl_->antiAliasing = static_cast<std::uint32_t>(mode);
    impl_->hdrRenderer.setAntiAliasing(mode);
    if (initialized()) {
        impl_->rebuildQualityTargets();
    }
}

AntiAliasingMode Graphics::antiAliasing() const noexcept {
    if (!impl_) {
        return AntiAliasingMode::None;
    }
    return static_cast<AntiAliasingMode>(impl_->antiAliasing);
}

void Graphics::setAntiAliasingStrength(float strength) noexcept {
    if (!impl_) {
        return;
    }
    impl_->antiAliasingStrength = std::clamp(strength, 0.0f, 1.0f);
}

float Graphics::antiAliasingStrength() const noexcept {
    return impl_ != nullptr ? impl_->antiAliasingStrength : 0.0f;
}


bool Graphics::initialized() const noexcept {
    return impl_ && impl_->instance != VK_NULL_HANDLE && impl_->device != VK_NULL_HANDLE && impl_->pipeline != VK_NULL_HANDLE;
}

void Graphics::waitIdle() noexcept {
    if (impl_ && impl_->device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(impl_->device);
    }
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

#if defined(APORIA_EXPERIMENTAL)
MaterialHandle Graphics::createMaterial(const MaterialDesc& description) {
    if (!impl_ || !initialized()) {
        if (impl_) impl_->setError("Graphics is not initialized");
        return invalidMaterialHandle;
    }
    if (!impl_->createMaterialLayout() || !impl_->createMaterialPool()) {
        return invalidMaterialHandle;
    }
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    const VkDescriptorSet set = impl_->buildMaterialSet(description, buffer, memory);
    if (set == VK_NULL_HANDLE) {
        impl_->setError("material set allocation failed");
        return invalidMaterialHandle;
    }
    const std::uint64_t id = impl_->nextMaterialId++;
    impl_->materialSets[id] = set;
    impl_->materialBuffers[id] = buffer;
    impl_->materialMemory[id] = memory;
    return MaterialHandle{id};
}

bool Graphics::updateMaterial(MaterialHandle handle, const MaterialDesc& description) {
    if (!impl_ || !initialized() || !handle.valid()) {
        return false;
    }
    const auto it = impl_->materialSets.find(handle.value());
    if (it == impl_->materialSets.end()) {
        return false;
    }
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    const VkDescriptorSet set = impl_->buildMaterialSet(description, buffer, memory);
    if (set == VK_NULL_HANDLE) {
        return false;
    }
    if (impl_->device != VK_NULL_HANDLE) {
        vkFreeDescriptorSets(impl_->device, impl_->materialDescriptorPool, 1, &it->second);
        if (impl_->materialBuffers[handle.value()] != VK_NULL_HANDLE) {
            vkDestroyBuffer(impl_->device, impl_->materialBuffers[handle.value()], nullptr);
        }
        if (impl_->materialMemory[handle.value()] != VK_NULL_HANDLE) {
            vkFreeMemory(impl_->device, impl_->materialMemory[handle.value()], nullptr);
        }
    }
    it->second = set;
    impl_->materialBuffers[handle.value()] = buffer;
    impl_->materialMemory[handle.value()] = memory;
    return true;
}

void Graphics::destroyMaterial(MaterialHandle handle) noexcept {
    if (!impl_ || !handle.valid()) {
        return;
    }
    const auto it = impl_->materialSets.find(handle.value());
    if (it == impl_->materialSets.end()) {
        return;
    }
    if (impl_->device != VK_NULL_HANDLE && impl_->materialDescriptorPool != VK_NULL_HANDLE) {
        VkDescriptorSet set = it->second;
        vkFreeDescriptorSets(impl_->device, impl_->materialDescriptorPool, 1, &set);
        if (impl_->materialBuffers[handle.value()] != VK_NULL_HANDLE) {
            vkDestroyBuffer(impl_->device, impl_->materialBuffers[handle.value()], nullptr);
        }
        if (impl_->materialMemory[handle.value()] != VK_NULL_HANDLE) {
            vkFreeMemory(impl_->device, impl_->materialMemory[handle.value()], nullptr);
        }
    }
    impl_->materialSets.erase(it);
    impl_->materialBuffers.erase(handle.value());
    impl_->materialMemory.erase(handle.value());
}

bool Graphics::materialValid(MaterialHandle handle) const noexcept {
    return impl_ != nullptr && handle.valid() && impl_->materialSets.count(handle.value()) != 0;
}
#endif

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
    const auto boundIt = impl_->textures.find(impl_->boundTexture.value());
    if (boundIt != impl_->textures.end()) {
        impl_->scene.bindAlbedo(boundIt->second.view, boundIt->second.sampler);
    }
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
    if (impl_->lodGroups.find(handle.value()) != impl_->lodGroups.end()) {
        destroyLodMesh(handle);
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
    const auto groupIt = impl_->lodGroups.find(handle.value());
    if (groupIt != impl_->lodGroups.end()) {
        if (groupIt->second.meshes.empty()) {
            return 0;
        }
        const auto meshIt = impl_->meshes.find(groupIt->second.meshes.front());
        return meshIt == impl_->meshes.end() ? 0 : meshIt->second.vertexCount;
    }
    const auto meshIt = impl_->meshes.find(handle.value());
    return meshIt == impl_->meshes.end() ? 0 : meshIt->second.vertexCount;
}

MeshHandle Graphics::createLodMesh(
    const std::vector<MeshData>& levels,
    const std::vector<float>& maxDistances,
    LodBoundary boundary) {
    if (!impl_) {
        return invalidMeshHandle;
    }
    if (!initialized()) {
        impl_->setError("Graphics is not initialized");
        return invalidMeshHandle;
    }
    if (levels.size() < 2) {
        impl_->setError("LOD group needs at least two levels");
        return invalidMeshHandle;
    }
    if (maxDistances.size() + 1 != levels.size()) {
        impl_->setError("LOD group needs one range per level except the last");
        return invalidMeshHandle;
    }
    for (std::size_t index = 0; index < maxDistances.size(); ++index) {
        if (!std::isfinite(maxDistances[index]) || maxDistances[index] < 0.0f) {
            impl_->setError("LOD range must be finite and non-negative");
            return invalidMeshHandle;
        }
        if (index > 0 && maxDistances[index] <= maxDistances[index - 1]) {
            impl_->setError("LOD ranges must be strictly ascending");
            return invalidMeshHandle;
        }
    }

    // Bounds come from level 0 so the boundary is the same for every level.
    Impl::LodGroup group;
    for (int axis = 0; axis < 3; ++axis) {
        group.center[axis] = 0.0f;
        group.halfExtents[axis] = 0.0f;
    }
    float minimum[3] = {
        std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::infinity()};
    float maximum[3] = {
        -std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity()};
    for (const MeshVertex& vertex : levels.front().vertices) {
        for (int axis = 0; axis < 3; ++axis) {
            minimum[axis] = std::min(minimum[axis], vertex.position[axis]);
            maximum[axis] = std::max(maximum[axis], vertex.position[axis]);
        }
    }
    for (int axis = 0; axis < 3; ++axis) {
        group.center[axis] = (minimum[axis] + maximum[axis]) * 0.5f;
        group.halfExtents[axis] = std::max((maximum[axis] - minimum[axis]) * 0.5f, 1.0e-3f);
    }
    group.boundingRadius = std::sqrt(
        group.halfExtents[0] * group.halfExtents[0] +
        group.halfExtents[1] * group.halfExtents[1] +
        group.halfExtents[2] * group.halfExtents[2]);
    group.boundary = boundary;
    group.maxDistances = maxDistances;

    for (const MeshData& level : levels) {
        const MeshHandle levelHandle = createMesh(level);
        if (!levelHandle.valid()) {
            for (std::uint64_t created : group.meshes) {
                destroyMesh(MeshHandle{created});
            }
            return invalidMeshHandle;
        }
        group.meshes.push_back(levelHandle.value());
    }

    const std::uint64_t id = impl_->nextMeshId++;
    try {
        const auto result = impl_->lodGroups.emplace(id, std::move(group));
        if (!result.second) {
            impl_->setError("LOD group handle allocation failed");
            return invalidMeshHandle;
        }
    } catch (...) {
        impl_->setError("LOD group allocation failed");
        return invalidMeshHandle;
    }
    engineLog("lod group created: id=%llu levels=%zu",
              static_cast<unsigned long long>(id),
              levels.size());
    return MeshHandle{id};
}

void Graphics::destroyLodMesh(MeshHandle handle) noexcept {
    if (!impl_ || !handle.valid()) {
        return;
    }
    const auto groupIt = impl_->lodGroups.find(handle.value());
    if (groupIt == impl_->lodGroups.end()) {
        return;
    }
    const std::vector<std::uint64_t> levels = groupIt->second.meshes;
    impl_->lodGroups.erase(groupIt);
    for (std::uint64_t level : levels) {
        destroyMesh(MeshHandle{level});
    }
}

bool Graphics::lodMeshValid(MeshHandle handle) const noexcept {
    return impl_ != nullptr && handle.valid() && impl_->lodGroups.count(handle.value()) != 0;
}

std::uint32_t Graphics::lodLevelCount(MeshHandle handle) const noexcept {
    if (!impl_ || !handle.valid()) {
        return 0;
    }
    const auto groupIt = impl_->lodGroups.find(handle.value());
    return groupIt == impl_->lodGroups.end()
        ? 0
        : static_cast<std::uint32_t>(groupIt->second.meshes.size());
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

    float projection[16];
    std::memcpy(projection, camera.projectionMatrix, sizeof(projection));
    const bool taaActive = static_cast<AntiAliasingMode>(impl_->antiAliasing) == AntiAliasingMode::TAA &&
                           impl_->hdrEnabled;
    if (taaActive) {
        // Sub-pixel jitter (Halton 2,3) gives TAA real new samples to accumulate.
        const std::uint32_t targetWidth = impl_->hdrWidth != 0 ? impl_->hdrWidth : impl_->renderWidth;
        const std::uint32_t targetHeight = impl_->hdrHeight != 0 ? impl_->hdrHeight : impl_->renderHeight;
        if (targetWidth > 0 && targetHeight > 0) {
            const double jitterX = Impl::halton(impl_->taaFrameIndex + 1, 2) - 0.5;
            const double jitterY = Impl::halton(impl_->taaFrameIndex + 1, 3) - 0.5;
            projection[8] += static_cast<float>(jitterX * 2.0 / static_cast<double>(targetWidth));
            projection[9] += static_cast<float>(jitterY * 2.0 / static_cast<double>(targetHeight));
        }
        ++impl_->taaFrameIndex;
    }

    std::memcpy(impl_->taaPrevViewProj, impl_->taaViewProj, sizeof(impl_->taaPrevViewProj));
    Impl::multiplyMat4(projection, camera.viewMatrix, impl_->pendingViewProj);
    std::memcpy(impl_->pendingProjection, projection, sizeof(impl_->pendingProjection));
    std::memcpy(impl_->taaViewProj, impl_->pendingViewProj, sizeof(impl_->taaViewProj));
    Impl::invertMat4(impl_->pendingViewProj, impl_->taaInvViewProj);

    impl_->pendingItems = items;
    impl_->drawDebugTriangle = items.empty();
    impl_->resolveLodGroups();
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
    result.lodGroupCount = static_cast<std::uint64_t>(impl_->lodGroups.size());
    result.lodDrawTriangles = impl_->lodDrawTriangles;
    result.lodFullTriangles = impl_->lodFullTriangles;
    result.lodSwitchCount = impl_->lodSwitchCount;
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
    result.antiAliasingStrength = impl_->antiAliasingStrength;
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


