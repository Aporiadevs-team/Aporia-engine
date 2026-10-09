#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "Graphics/GlobalDrawer/GraphicsImpl.hpp"

#include "Device/SceneManager/HeadlessTarget/HeadlessTarget.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

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

using Aporia::graphics_internal::engineLog;
using Aporia::graphics_internal::getWindowExtent;

namespace Aporia {

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
    double frameSeconds = 1.0 / 60.0;
    if (timing && QueryPerformanceCounter(&endCounter) != 0) {
        const double milliseconds = static_cast<double>(endCounter.QuadPart - startCounter.QuadPart) *
                                    1000.0 / static_cast<double>(frequency.QuadPart);
        constexpr double smoothing = 0.9;
        impl_->avgFrameMs = impl_->avgFrameMs <= 0.0
            ? milliseconds
            : impl_->avgFrameMs * smoothing + milliseconds * (1.0 - smoothing);
        frameSeconds = milliseconds / 1000.0;
    }
    impl_->timeManager.update(frameSeconds);
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

    // Keep up to MaxFramesInFlight frames in flight: wait only this slot's
    // fence instead of draining the whole queue every frame.
    const std::uint32_t frameSlot = impl_->currentFrameSlot;
    if (vkWaitForFences(impl_->device, 1, &impl_->frameSync.fences[frameSlot], VK_TRUE,
                        std::numeric_limits<std::uint64_t>::max()) != VK_SUCCESS) {
        impl_->lastError = "vkWaitForFences failed";
        return false;
    }
    if (!impl_->renderFrameCommands(impl_->framebuffer, impl_->renderWidth, impl_->renderHeight)) {
        return false;
    }

    VkCommandBuffer frameBuffers[2]{};
    std::uint32_t frameBufferCount = 0;
#if defined(APORIA_EXPERIMENTAL)
    if (impl_->shadowRecordedThisFrame && impl_->shadowCommandBuffers[frameSlot] != VK_NULL_HANDLE) {
        frameBuffers[frameBufferCount++] = impl_->shadowCommandBuffers[frameSlot];
    }
#endif
    frameBuffers[frameBufferCount++] = impl_->renderCommandBuffers[frameSlot];

    if (vkResetFences(impl_->device, 1, &impl_->frameSync.fences[frameSlot]) != VK_SUCCESS) {
        impl_->lastError = "vkResetFences failed";
        return false;
    }
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = frameBufferCount;
    submitInfo.pCommandBuffers = frameBuffers;
    if (vkQueueSubmit(impl_->queue, 1, &submitInfo, impl_->frameSync.fences[frameSlot]) != VK_SUCCESS) {
        impl_->lastError = "vkQueueSubmit failed";
        return false;
    }

    ++impl_->frameCount;
    impl_->currentFrameSlot = (frameSlot + 1) % Impl::maxFramesInFlight;
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
    // Per-slot command buffers mean the slot fence is enough: no queue drain.

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
    VkCommandBuffer frameBuffers[2]{};
    std::uint32_t frameBufferCount = 0;
#if defined(APORIA_EXPERIMENTAL)
    if (impl_->shadowRecordedThisFrame && impl_->shadowCommandBuffers[frameSlot] != VK_NULL_HANDLE) {
        frameBuffers[frameBufferCount++] = impl_->shadowCommandBuffers[frameSlot];
    }
#endif
    frameBuffers[frameBufferCount++] = impl_->renderCommandBuffers[frameSlot];
    submitInfo.commandBufferCount = frameBufferCount;
    submitInfo.pCommandBuffers = frameBuffers;
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

#if defined(APORIA_EXPERIMENTAL)
void Graphics::setMultithreadedRender(bool enabled) noexcept {
    if (impl_) {
        impl_->multithreadedRender = enabled;
    }
}

bool Graphics::multithreadedRender() const noexcept {
    return impl_ != nullptr && impl_->multithreadedRender;
}
#endif

bool Graphics::Impl::renderFrameCommands(VkFramebuffer targetFramebuffer, std::uint32_t width, std::uint32_t height) noexcept {
    const std::uint32_t slotIndex = currentFrameSlot % maxFramesInFlight;
    VkCommandBuffer cmd = renderCommandBuffers[slotIndex];
    if (cmd == VK_NULL_HANDLE) {
        setError("render command buffer is unavailable");
        return false;
    }
    if (vkResetCommandBuffer(cmd, 0) != VK_SUCCESS) {
        setError("vkResetCommandBuffer failed");
        return false;
    }

    VkCommandBufferBeginInfo commandBeginInfo{};
    commandBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    commandBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cmd, &commandBeginInfo) != VK_SUCCESS) {
        setError("vkBeginCommandBuffer failed");
        return false;
    }

    const std::uint32_t uniformSlot = currentFrameSlot % graphics_internal::SceneBindings::kSlots;
#if defined(APORIA_EXPERIMENTAL)
    const auto* materialSetsArg = materialStore.sets();
    const VkDescriptorSet defaultMaterialArg = materialStore.defaultSet();
#else
    const std::unordered_map<std::uint64_t, VkDescriptorSet>* materialSetsArg = nullptr;
    const VkDescriptorSet defaultMaterialArg = VK_NULL_HANDLE;
#endif
    if (timeDrivenSun) {
        // Sun follows the time-of-day clock: sunrise east, noon zenith, sunset west.
        float sunDir[3];
        timeManager.sunDirection(sunDir);
        environment.sunDirection[0] = sunDir[0];
        environment.sunDirection[1] = sunDir[1];
        environment.sunDirection[2] = sunDir[2];
        // Daylight ramps smoothly with the sun elevation so the intensity never
        // snaps: dim at the horizon, full by ~15 degrees, dark below.
        const float e = sunDir[2];
        float t = (e + 0.05f) / 0.30f;
        t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        environment.sunIntensity = t * t * (3.0f - 2.0f * t);
    }

    float sunMatrix[16];
    buildSunMatrix(sunMatrix);
    writeSceneUniforms(uniformSlot, sunMatrix);

    {
        graphics_internal::SkyRenderer::Uniforms skyUniforms{};
        const float* view = pendingCamera.viewMatrix;
        const float* proj = pendingProjection;
        // Column-major view: right = column 0, up = column 1, back = column 2.
        skyUniforms.cameraRight[0] = view[0];
        skyUniforms.cameraRight[1] = view[4];
        skyUniforms.cameraRight[2] = view[8];
        skyUniforms.cameraUp[0] = view[1];
        skyUniforms.cameraUp[1] = view[5];
        skyUniforms.cameraUp[2] = view[9];
        skyUniforms.cameraForward[0] = -view[2];
        skyUniforms.cameraForward[1] = -view[6];
        skyUniforms.cameraForward[2] = -view[10];
        skyUniforms.cameraPosition[0] = static_cast<float>(pendingCamera.position[0]);
        skyUniforms.cameraPosition[1] = static_cast<float>(pendingCamera.position[1]);
        skyUniforms.cameraPosition[2] = static_cast<float>(pendingCamera.position[2]);
        skyUniforms.sunDirection[0] = environment.sunDirection[0];
        skyUniforms.sunDirection[1] = environment.sunDirection[1];
        skyUniforms.sunDirection[2] = environment.sunDirection[2];
        skyUniforms.sunDirection[3] = environment.sunIntensity;
        skyUniforms.sunColor[0] = 1.0f;
        skyUniforms.sunColor[1] = 0.94f;
        skyUniforms.sunColor[2] = 0.82f;
        skyUniforms.sunColor[3] = 1.0f;
        skyUniforms.skyParams[0] = 0.5f;  // horizon exponent
        skyUniforms.skyParams[1] = 0.45f; // haze
        skyUniforms.skyParams[2] = 1.0f;  // glow
        // tan(halfFovY) and aspect derived from the projection matrix.
        const float focal = proj[5] != 0.0f ? -1.0f / proj[5] : 0.7f;
        skyUniforms.skyParams[3] = focal > 0.0f ? focal : 0.7f;
        skyUniforms.renderParams[0] = (proj[0] != 0.0f && focal > 0.0f) ? (-proj[5] / proj[0]) : 1.0f;

        // Twilight glow: warm band around the sun near the horizon. Dawn is pale
        // yellow, dusk pale pink; it fades slowly across ~nautical twilight so
        // the colour lingers after (or before) the sun crosses the horizon.
        const auto smoothstep = [](float a, float b, float x) {
            float t = (x - a) / (b - a);
            t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
            return t * t * (3.0f - 2.0f * t);
        };
        const float elevation = environment.sunDirection[2];
        skyUniforms.twilight[3] =
            smoothstep(-0.30f, -0.02f, elevation) * (1.0f - smoothstep(0.02f, 0.35f, elevation));
        const double rise = timeManager.sunriseHour();
        const double set = timeManager.sunsetHour();
        const bool morning = timeDrivenSun && timeManager.totalHours() < (rise + set) * 0.5;
        skyUniforms.twilight[0] = morning ? 0.98f : 0.97f;
        skyUniforms.twilight[1] = morning ? 0.86f : 0.62f;
        skyUniforms.twilight[2] = morning ? 0.55f : 0.52f;
        sky.writeUniforms(device, uniformSlot, skyUniforms);
    }

    const bool recordShadow = shadowsEnabled && pipelines.shadowPipeline != VK_NULL_HANDLE && !pendingItems.empty();
#if defined(APORIA_EXPERIMENTAL)
    shadowRecordedThisFrame = false;
    VkCommandBuffer shadowCmd = shadowCommandBuffers[slotIndex];
    if (recordShadow && multithreadedRender && jobs && shadowCmd != VK_NULL_HANDLE) {
        float shadowSunMatrix[16];
        std::memcpy(shadowSunMatrix, sunMatrix, sizeof(shadowSunMatrix));
        jobs->submit([this, shadowCmd, shadowSunMatrix]() {
            vkResetCommandBuffer(shadowCmd, 0);
            VkCommandBufferBeginInfo info{};
            info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            if (vkBeginCommandBuffer(shadowCmd, &info) == VK_SUCCESS) {
                renderShadowMap(shadowCmd, shadowSunMatrix);
                vkEndCommandBuffer(shadowCmd);
            } else {
                shadowRecordedThisFrame = false;
            }
        });
        shadowRecordedThisFrame = true;
    } else if (recordShadow) {
        renderShadowMap(cmd, sunMatrix);
    }
#else
    if (recordShadow) {
        renderShadowMap(cmd, sunMatrix);
    }
#endif

#if defined(APORIA_EXPERIMENTAL)
    if (shadowRecordedThisFrame) {
        VkImageMemoryBarrier shadowBarrier{};
        shadowBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        shadowBarrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        shadowBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        shadowBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        shadowBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        shadowBarrier.image = shadow.image;
        shadowBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        shadowBarrier.subresourceRange.levelCount = 1;
        shadowBarrier.subresourceRange.layerCount = 1;
        shadowBarrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        shadowBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(
            cmd,
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &shadowBarrier);
    }
#endif

    // Main target selection (HDR scene target or the final target at Low).
    VkFramebuffer mainFramebuffer = targetFramebuffer;
    std::uint32_t mainWidth = width;
    std::uint32_t mainHeight = height;
    VkRenderPass mainRenderPass = renderPass;
    VkPipeline mainMeshPipeline = pipelines.meshPipeline;
    const bool useHdr = hdrEnabled && hdrFramebuffer != VK_NULL_HANDLE && pipelines.hdrMeshPipeline != VK_NULL_HANDLE;
    if (useHdr) {
        mainFramebuffer = hdrFramebuffer;
        mainWidth = hdrWidth;
        mainHeight = hdrHeight;
        mainRenderPass = hdrRenderPass;
        mainMeshPipeline = pipelines.hdrMeshPipeline;
    }
    const bool useReflection = reflectionsEnabled && reflectFramebuffer != VK_NULL_HANDLE;
    const bool drawDebug = drawDebugTriangle && !useHdr;

    // ---- record one secondary command buffer per renderer/pass ----
    const auto beginSecondary = [](VkCommandBuffer cb, VkRenderPass renderPass, VkFramebuffer framebuffer) -> bool {
        if (cb == VK_NULL_HANDLE || renderPass == VK_NULL_HANDLE || framebuffer == VK_NULL_HANDLE) {
            return false;
        }
        vkResetCommandBuffer(cb, 0);
        VkCommandBufferInheritanceInfo inheritance{};
        inheritance.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
        inheritance.renderPass = renderPass;
        inheritance.framebuffer = framebuffer;
        inheritance.subpass = 0;
        VkCommandBufferBeginInfo info{};
        info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        info.flags = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT | VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        info.pInheritanceInfo = &inheritance;
        return vkBeginCommandBuffer(cb, &info) == VK_SUCCESS;
    };
    const auto setViewportScissor = [](VkCommandBuffer cb, std::uint32_t w, std::uint32_t h) {
        VkViewport viewport{};
        viewport.width = static_cast<float>(w);
        viewport.height = static_cast<float>(h);
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cb, 0, 1, &viewport);
        VkRect2D scissor{};
        scissor.extent = {w, h};
        vkCmdSetScissor(cb, 0, 1, &scissor);
    };

    VkCommandBuffer reflectMeshCb = secondaryBuffers[slotIndex][kSecondaryReflectMesh];
    VkCommandBuffer mainMeshCb = secondaryBuffers[slotIndex][kSecondaryMainMesh];
    VkCommandBuffer uiRectCb = secondaryBuffers[slotIndex][kSecondaryUiRect];
    VkCommandBuffer uiTextCb = secondaryBuffers[slotIndex][kSecondaryUiText];
    VkCommandBuffer tonemapCb = secondaryBuffers[slotIndex][kSecondaryTonemap];

    bool haveReflectMesh = false;
    if (useReflection && !pendingItems.empty() && beginSecondary(reflectMeshCb, renderPass, reflectFramebuffer)) {
        setViewportScissor(reflectMeshCb, reflectWidth, reflectHeight);
        const Mat4 mirroredView = reflectViewMatrix();
        float reflectViewProj[16];
        Impl::multiplyMat4(pendingProjection, mirroredView.m[0], reflectViewProj);
        graphics_internal::MeshRenderer::renderMeshes(
            reflectMeshCb, pipelines.meshPipeline, pipelines.meshPipelineLayout,
            scene.set(uniformSlot), reflectViewProj,
            pendingItems, meshStore.resources(), VK_NULL_HANDLE, materialSetsArg, defaultMaterialArg);
        vkEndCommandBuffer(reflectMeshCb);
        haveReflectMesh = true;
    }

    // Mesh pass secondary (debug triangle + scene meshes live in the same one).
    bool haveMainMesh = false;
    if ((!pendingItems.empty() || drawDebug) && beginSecondary(mainMeshCb, mainRenderPass, mainFramebuffer)) {
        setViewportScissor(mainMeshCb, mainWidth, mainHeight);
        if (drawDebug) {
            const VkDescriptorSet debugDescriptor = textureStore.descriptorSet();
            vkCmdBindPipeline(mainMeshCb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.pipeline);
            vkCmdBindDescriptorSets(mainMeshCb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.pipelineLayout,
                                    0, 1, &debugDescriptor, 0, nullptr);
            vkCmdDraw(mainMeshCb, 3, 1, 0, 0);
        }
        if (mainMeshPipeline != VK_NULL_HANDLE && !pendingItems.empty()) {
            std::vector<RenderItem> opaqueItems;
            std::vector<RenderItem> transparentItems;
            opaqueItems.reserve(pendingItems.size());
            for (const RenderItem& item : pendingItems) {
                if (item.transparent) {
                    transparentItems.push_back(item);
                } else {
                    opaqueItems.push_back(item);
                }
            }
            if (!opaqueItems.empty()) {
                graphics_internal::MeshRenderer::renderMeshes(
                    mainMeshCb, pipelines.meshPipeline, pipelines.meshPipelineLayout,
                    scene.set(uniformSlot), pendingViewProj,
                    opaqueItems, meshStore.resources(), mainMeshPipeline, materialSetsArg, defaultMaterialArg);
            }
            const VkPipeline transparentPipeline =
                useHdr ? pipelines.hdrMeshTransparentPipeline : pipelines.meshTransparentPipeline;
            if (!transparentItems.empty() && transparentPipeline != VK_NULL_HANDLE) {
                graphics_internal::MeshRenderer::renderMeshes(
                    mainMeshCb, pipelines.meshPipeline, pipelines.meshPipelineLayout,
                    scene.set(uniformSlot), pendingViewProj,
                    transparentItems, meshStore.resources(), transparentPipeline, materialSetsArg, defaultMaterialArg);
            }
        }
        vkEndCommandBuffer(mainMeshCb);
        haveMainMesh = true;
    }

    bool haveUiRect = false;
    if (beginSecondary(uiRectCb, renderPass, targetFramebuffer)) {
        setViewportScissor(uiRectCb, width, height);
        if (pipelines.uiPipeline != VK_NULL_HANDLE) {
            vkCmdBindPipeline(uiRectCb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.uiPipeline);
        }
        // uiPipeline shares the tonemap vertex shader and layout, which reads the
        // bindless scene descriptor set 0; without a bound set every UI rect draw
        // is invalid (missing descriptor) and nothing composites.
        if (tonemapDescriptorSet != VK_NULL_HANDLE) {
            const VkDescriptorSet uiRectSet = tonemapDescriptorSet;
            vkCmdBindDescriptorSets(uiRectCb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipelines.tonemapPipelineLayout, 0u, 1u, &uiRectSet, 0u,
                                    nullptr);
        }
        rectRenderer.draw(uiRectCb, pipelines.tonemapPipelineLayout, width, height, uiDrawList);
        vkEndCommandBuffer(uiRectCb);
        haveUiRect = true;
    }
    bool haveUiText = false;
    if (beginSecondary(uiTextCb, renderPass, targetFramebuffer)) {
        setViewportScissor(uiTextCb, width, height);
        renderUiText(uiTextCb, width, height);
        vkEndCommandBuffer(uiTextCb);
        haveUiText = true;
    }

    bool haveTonemap = false;
    if (useHdr && beginSecondary(tonemapCb, renderPass, targetFramebuffer)) {
        setViewportScissor(tonemapCb, width, height);
        vkCmdBindPipeline(tonemapCb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.tonemapPipeline);
        vkCmdBindDescriptorSets(tonemapCb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.tonemapPipelineLayout,
                                0, 1, &tonemapDescriptorSet, 0, nullptr);
        struct TonemapPush {
            graphics_internal::RectRenderer::PushConstants ui;
            float aa[4];
        };
        TonemapPush push{};
        push.aa[0] = antiAliasingStrength;
        push.aa[1] = static_cast<float>(antiAliasing);
        vkCmdPushConstants(tonemapCb, pipelines.tonemapPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(push), &push);
        vkCmdDraw(tonemapCb, 4, 1, 0, 0);
        vkEndCommandBuffer(tonemapCb);
        haveTonemap = true;
    }

    // Sky background: a fullscreen secondary drawn first in the main pass.
    VkCommandBuffer skyCb = secondaryBuffers[slotIndex][kSecondarySky];
    bool haveSky = false;
    if (sky.valid() && beginSecondary(skyCb, mainRenderPass, mainFramebuffer)) {
        setViewportScissor(skyCb, mainWidth, mainHeight);
        sky.record(skyCb, useHdr, uniformSlot);
        vkEndCommandBuffer(skyCb);
        haveSky = true;
    }

    // ---- primary: begin the render passes and execute the secondaries ----
    if (haveReflectMesh) {
        std::array<VkClearValue, 2> reflectClear{};
        reflectClear[0].color = {{0.02f, 0.03f, 0.05f, 1.0f}};
        reflectClear[1].depthStencil = {1.0f, 0};
        VkRenderPassBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        begin.renderPass = renderPass;
        begin.framebuffer = reflectFramebuffer;
        begin.renderArea.extent = {reflectWidth, reflectHeight};
        begin.clearValueCount = static_cast<std::uint32_t>(reflectClear.size());
        begin.pClearValues = reflectClear.data();
        vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
        vkCmdExecuteCommands(cmd, 1, &reflectMeshCb);
        vkCmdEndRenderPass(cmd);
    }

    {
        std::array<VkClearValue, 2> clearValues{};
        clearValues[0].color = {{0.02f, 0.03f, 0.05f, 1.0f}};
        clearValues[1].depthStencil = {1.0f, 0};
        VkRenderPassBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        begin.renderPass = mainRenderPass;
        begin.framebuffer = mainFramebuffer;
        begin.renderArea.extent = {mainWidth, mainHeight};
        begin.clearValueCount = static_cast<std::uint32_t>(clearValues.size());
        begin.pClearValues = clearValues.data();
        vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);

        VkCommandBuffer mainCommands[4]{};
        std::uint32_t mainCommandCount = 0;
        if (haveSky) {
            mainCommands[mainCommandCount++] = skyCb;
        }
        if (haveMainMesh) {
            mainCommands[mainCommandCount++] = mainMeshCb;
        }
        // UI must composite at every quality; without HDR the tonemap pass does
        // not run, so execute the UI secondaries straight into the main pass.
        if (!useHdr) {
            if (haveUiRect) {
                mainCommands[mainCommandCount++] = uiRectCb;
            }
            if (haveUiText) {
                mainCommands[mainCommandCount++] = uiTextCb;
            }
        }
        if (mainCommandCount > 0) {
            vkCmdExecuteCommands(cmd, mainCommandCount, mainCommands);
        }
        vkCmdEndRenderPass(cmd);
    }

    if (useHdr && static_cast<AntiAliasingMode>(antiAliasing) == AntiAliasingMode::TAA && taa.valid()) {
        taa.record(cmd, hdrDepthImage, taaPrevViewProj, taaInvViewProj, antiAliasingStrength);
    }

    if (useHdr && haveTonemap) {
        renderTonemap(cmd, targetFramebuffer, width, height);
    }

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        setError("vkEndCommandBuffer failed");
        return false;
    }
#if defined(APORIA_EXPERIMENTAL)
    if (jobs) {
        jobs->waitAll();
    }
#endif
    return true;
}

void Graphics::Impl::writeSceneUniforms(std::uint32_t slot, const float sunMatrix[16]) {
    SceneUniforms uniforms{};
    std::memcpy(uniforms.sunViewProj, sunMatrix, sizeof(uniforms.sunViewProj));
    uniforms.sunDirection[0] = environment.sunDirection[0];
    uniforms.sunDirection[1] = environment.sunDirection[1];
    uniforms.sunDirection[2] = environment.sunDirection[2];
    uniforms.sunDirection[3] = environment.sunIntensity;
    uniforms.cameraPosition[0] = static_cast<float>(pendingCamera.position[0]);
    uniforms.cameraPosition[1] = static_cast<float>(pendingCamera.position[1]);
    uniforms.cameraPosition[2] = static_cast<float>(pendingCamera.position[2]);
    uniforms.shadingParams[0] = environment.ambient;
    uniforms.shadingParams[1] = shadowsEnabled ? shadowStrength : 1.0f;
    uniforms.shadingParams[2] = environment.mirrorZ;
    uniforms.shadingParams[3] = shadowMapSize > 0 ? 1.0f / static_cast<float>(shadowMapSize) : 0.0f;
    uniforms.screenParams[0] = static_cast<float>(hdrEnabled ? hdrWidth : (headless ? renderWidth : swapchainExtent.width));
    uniforms.screenParams[1] = static_cast<float>(hdrEnabled ? hdrHeight : (headless ? renderHeight : swapchainExtent.height));
    uniforms.shadowParams[0] = static_cast<float>(shadowConfig.filter);
    uniforms.shadowParams[1] = static_cast<float>(shadowConfig.blockerSamples);
    uniforms.shadowParams[2] = static_cast<float>(shadowConfig.filterSamples);
    uniforms.shadowParams[3] = shadowConfig.contactShadows ? shadowConfig.contactDistance : 0.0f;

    const std::uint32_t lightCount = lightStore.count();
    const std::uint32_t activeLights = lightCount < maxLights ? lightCount : maxLights;
    uniforms.lightInfo[0] = static_cast<float>(activeLights);
    for (std::uint32_t index = 0; index < activeLights; ++index) {
        std::memcpy(uniforms.lightPosition[index], lightStore.position[index], sizeof(uniforms.lightPosition[index]));
        std::memcpy(uniforms.lightDirection[index], lightStore.direction[index], sizeof(uniforms.lightDirection[index]));
        std::memcpy(uniforms.lightColor[index], lightStore.color[index], sizeof(uniforms.lightColor[index]));
        std::memcpy(uniforms.lightParams[index], lightStore.params[index], sizeof(uniforms.lightParams[index]));
    }
    scene.writeUniforms(slot, &uniforms, sizeof(uniforms));
}

void Graphics::Impl::destroyDepthResources() noexcept {
    destroyDepth(device);
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
}

bool Graphics::Impl::configureNativeSurface() {
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

bool Graphics::Impl::createRenderPass() {
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

void Graphics::Impl::destroyHeadlessTarget() noexcept {
    graphics_internal::HeadlessTarget::destroy(device, framebuffer, colorView, colorImage, colorMemory);
}

bool Graphics::Impl::createHeadlessTarget() {
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

void Graphics::Impl::destroySwapchainResources() noexcept {
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

bool Graphics::Impl::createSwapchainResources() {
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

bool Graphics::Impl::recreateSwapchain() noexcept {
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

} // namespace Aporia
