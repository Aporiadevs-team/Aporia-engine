#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "Graphics/GlobalDrawer/GraphicsImpl.hpp"

#include <algorithm>
#include <string>
#include <utility>

#ifndef APORIA_VULKAN_SHADER_DIR
#define APORIA_VULKAN_SHADER_DIR "vulkan-shaders"
#endif

namespace {
using Aporia::graphics_internal::chooseColorFormat;
using Aporia::graphics_internal::chooseHdrFormat;
using Aporia::graphics_internal::engineLog;
}

namespace Aporia {

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
    // The quality preset above already produced shadowConfig. Only an explicit
    // GraphicsConfig.shadows override may replace it; the default value must not
    // clobber the preset (otherwise High's 4096 map never applies).
    const Aporia::ShadowConfig defaultShadows{};
    const Aporia::ShadowConfig& requested = config.shadows;
    const bool customShadows =
        requested.enabled != defaultShadows.enabled ||
        requested.mapSize != defaultShadows.mapSize ||
        requested.cascadeCount != defaultShadows.cascadeCount ||
        requested.passCount != defaultShadows.passCount ||
        requested.pcfRadius != defaultShadows.pcfRadius ||
        requested.bias != defaultShadows.bias ||
        requested.normalBias != defaultShadows.normalBias ||
        requested.strength != defaultShadows.strength ||
        requested.distance != defaultShadows.distance ||
        requested.filter != defaultShadows.filter ||
        requested.blockerSamples != defaultShadows.blockerSamples ||
        requested.filterSamples != defaultShadows.filterSamples ||
        requested.contactShadows != defaultShadows.contactShadows ||
        requested.contactDistance != defaultShadows.contactDistance ||
        requested.cascadeSplitLambda != defaultShadows.cascadeSplitLambda;
    if (customShadows) {
        impl_->shadowConfig = requested;
        impl_->shadowConfig.mapSize = std::max(1u, requested.mapSize);
        impl_->shadowConfig.cascadeCount = std::max(1u, requested.cascadeCount);
        impl_->shadowConfig.passCount = std::max(1u, requested.passCount);
        impl_->shadowConfig.pcfRadius = std::min(8u, requested.pcfRadius);
    }
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
    // QualityTargets is used (render passes, shadow resources) before the
    // command buffer exists; it only needs the device/format here.
    impl_->QualityTargets::setDevice({impl_->device, impl_->physicalDevice, impl_->memoryProperties,
                                      VK_NULL_HANDLE, VK_NULL_HANDLE});
#if defined(APORIA_EXPERIMENTAL)
    impl_->jobs = std::make_unique<graphics_internal::JobSystem>(0);
    engineLog("worker pool: %u threads", impl_->jobs->threadCount());
#endif

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
    // Per-frame-slot render command pools/buffers so in-flight frames no longer
    // share one command buffer (and therefore no longer serialize).
    for (std::uint32_t slot = 0; slot < graphics_internal::Device::MaxFramesInFlight; ++slot) {
        if (!graphics_internal::Device::createCommandPool(impl_->device, impl_->graphicsFamily, impl_->renderCommandPools[slot]) ||
            !graphics_internal::Device::createCommandBuffer(impl_->device, impl_->renderCommandPools[slot], impl_->renderCommandBuffers[slot])) {
            impl_->setError("render command pool/buffer creation failed");
            shutdown();
            return false;
        }
    }
    // One secondary command buffer (own pool) per renderer/pass and frame slot.
    for (std::uint32_t slot = 0; slot < graphics_internal::Device::MaxFramesInFlight; ++slot) {
        for (std::uint32_t kind = 0; kind < Impl::kSecondaryCount; ++kind) {
            if (!graphics_internal::Device::createCommandPool(impl_->device, impl_->graphicsFamily, impl_->secondaryPools[slot][kind]) ||
                !graphics_internal::Device::createSecondaryCommandBuffer(impl_->device, impl_->secondaryPools[slot][kind], impl_->secondaryBuffers[slot][kind])) {
                impl_->setError("secondary command pool/buffer creation failed");
                shutdown();
                return false;
            }
        }
    }
#if defined(APORIA_EXPERIMENTAL)
    for (std::uint32_t slot = 0; slot < graphics_internal::Device::MaxFramesInFlight; ++slot) {
        if (!graphics_internal::Device::createCommandPool(impl_->device, impl_->graphicsFamily, impl_->shadowCommandPools[slot]) ||
            !graphics_internal::Device::createCommandBuffer(impl_->device, impl_->shadowCommandPools[slot], impl_->shadowCommandBuffers[slot])) {
            impl_->setError("shadow command pool/buffer creation failed");
            shutdown();
            return false;
        }
    }
#endif
    const graphics_internal::MeshStore::Device meshDevice{
        impl_->device, impl_->memoryProperties, impl_->commandBuffer, impl_->queue};
    impl_->meshStore.setDevice(meshDevice);
    
    impl_->textureStore.setDevice({impl_->device, impl_->physicalDevice, impl_->memoryProperties,
                                   impl_->commandBuffer, impl_->queue});
    impl_->QualityTargets::setDevice({impl_->device, impl_->physicalDevice, impl_->memoryProperties,
                                      impl_->commandBuffer, impl_->queue});
#if defined(APORIA_EXPERIMENTAL)
    impl_->materialStore.setDevice({impl_->device, impl_->memoryProperties});
#endif


    // Frame synchronization is needed for both paths now: headless uses fences
    // to keep frames in flight instead of serializing on vkQueueWaitIdle.
    if (!graphics_internal::Device::createFrameSync(impl_->device, impl_->frameSync)) {
        impl_->setError("vkCreateSemaphore/vkCreateFence failed");
        shutdown();
        return false;
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

    {
        std::string textureError;
        if (!impl_->textureStore.initialize(impl_->scene, textureError)) {
            impl_->setError(textureError);
            shutdown();
            return false;
        }
    }
#if defined(APORIA_EXPERIMENTAL)
    {
        std::string materialError;
        if (!impl_->materialStore.createDefault(impl_->textureStore, materialError)) {
            impl_->setError(materialError);
            shutdown();
            return false;
        }
    }
#endif
    // Scene layout must exist before the mesh/shadow pipelines referencing it.
    if (!impl_->scene.createLayout(impl_->device)) {
        impl_->setError("vkCreateDescriptorSetLayout(scene) failed");
        shutdown();
        return false;
    }
    const VkDescriptorSetLayout sceneLayout = impl_->scene.layout();

    const std::string shaderDirectory = APORIA_VULKAN_SHADER_DIR;
    graphics_internal::PipelineContext pipelineContext;
    pipelineContext.device = impl_->device;
    pipelineContext.renderPass = impl_->renderPass;
    pipelineContext.hdrRenderPass = impl_->hdrRenderPass;
    pipelineContext.shadowRenderPass = impl_->shadow.renderPass;
    pipelineContext.triangleLayout = impl_->textureStore.descriptorSetLayout();
    pipelineContext.sceneLayout = sceneLayout;
#if defined(APORIA_EXPERIMENTAL)
    pipelineContext.materialLayout = impl_->materialStore.descriptorSetLayout();
#endif
    pipelineContext.shaderDirectory = shaderDirectory;
    pipelineContext.reflectionsEnabled = impl_->reflectionsEnabled;
    pipelineContext.shadowTaps = impl_->shadowTaps;
    pipelineContext.maxLights = static_cast<std::int32_t>(impl_->maxLights);
    std::string pipelineError;
    if (!graphics_internal::createPipelines(pipelineContext, impl_->pipelines, pipelineError)) {
        impl_->setError(pipelineError);
        shutdown();
        return false;
    }

    if (config.sky) {
        std::string skyError;
        if (!impl_->sky.initialize(impl_->device, impl_->memoryProperties, impl_->renderPass,
                                   impl_->hdrRenderPass, shaderDirectory, skyError)) {
            impl_->setError(skyError);
            shutdown();
            return false;
        }
    }

#if defined(APORIA_EXPERIMENTAL)
    {
        std::string volumetricError;
        if (!impl_->volumetric.initialize(
                {impl_->device, impl_->physicalDevice, impl_->memoryProperties,
                 impl_->commandBuffer, impl_->queue},
                shaderDirectory, impl_->shadow.view, impl_->shadow.sampler, volumetricError)) {
            impl_->setError(volumetricError);
            shutdown();
            return false;
        }
    }
#endif

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

void Graphics::Impl::destroy() noexcept {
#if defined(APORIA_EXPERIMENTAL)
    jobs.reset();
#endif
    if (device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);
    }

    textRenderer.destroy();

    if (device != VK_NULL_HANDLE) {
        graphics_internal::Device::destroyFrameSync(device, frameSync);
        textureStore.release(device);
        meshStore.release(device);
        pendingItems.clear();
        destroyHeadlessTarget();
        destroyQualityTargets();
        destroyDepthResources();
        destroySwapchainResources();
        graphics_internal::destroyPipelines(device, pipelines);
        sky.shutdown(device);
        taa.shutdown();
        scene.destroyLayout(device);
#if defined(APORIA_EXPERIMENTAL)
        volumetric.destroy();
        materialStore.release(device);
#endif
        if (renderPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(device, renderPass, nullptr);
            renderPass = VK_NULL_HANDLE;
        }
        if (hdrRenderPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(device, hdrRenderPass, nullptr);
            hdrRenderPass = VK_NULL_HANDLE;
        }
        for (std::uint32_t slot = 0; slot < graphics_internal::Device::MaxFramesInFlight; ++slot) {
            if (renderCommandPools[slot] != VK_NULL_HANDLE) {
                vkDestroyCommandPool(device, renderCommandPools[slot], nullptr);
                renderCommandPools[slot] = VK_NULL_HANDLE;
            }
            renderCommandBuffers[slot] = VK_NULL_HANDLE;
        }
        for (std::uint32_t slot = 0; slot < graphics_internal::Device::MaxFramesInFlight; ++slot) {
            for (std::uint32_t kind = 0; kind < Impl::kSecondaryCount; ++kind) {
                if (secondaryPools[slot][kind] != VK_NULL_HANDLE) {
                    vkDestroyCommandPool(device, secondaryPools[slot][kind], nullptr);
                    secondaryPools[slot][kind] = VK_NULL_HANDLE;
                }
                secondaryBuffers[slot][kind] = VK_NULL_HANDLE;
            }
        }
#if defined(APORIA_EXPERIMENTAL)
        for (std::uint32_t slot = 0; slot < graphics_internal::Device::MaxFramesInFlight; ++slot) {
            if (shadowCommandPools[slot] != VK_NULL_HANDLE) {
                vkDestroyCommandPool(device, shadowCommandPools[slot], nullptr);
                shadowCommandPools[slot] = VK_NULL_HANDLE;
            }
            shadowCommandBuffers[slot] = VK_NULL_HANDLE;
        }
#endif
        if (commandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device, commandPool, nullptr);
            commandPool = VK_NULL_HANDLE;
        }
        vkDestroyDevice(device, nullptr);
        device = VK_NULL_HANDLE;
    } else {
        destroySwapchainResources();
        textureStore.release(device);
        meshStore.release(device);
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
    textureStore.setTextureScalePercent(100);
    activeQuality = Aporia::GraphicsQuality::Medium;
    environment = Aporia::EnvironmentState{};
    hdrFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
}

} // namespace Aporia
