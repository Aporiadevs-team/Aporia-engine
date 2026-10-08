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
    VkCommandBuffer frameBuffers[2]{};
    std::uint32_t frameBufferCount = 0;
#if defined(APORIA_EXPERIMENTAL)
    if (impl_->shadowRecordedThisFrame && impl_->shadowCommandBuffer != VK_NULL_HANDLE) {
        frameBuffers[frameBufferCount++] = impl_->shadowCommandBuffer;
    }
#endif
    frameBuffers[frameBufferCount++] = impl_->commandBuffer;
    submitInfo.commandBufferCount = frameBufferCount;
    submitInfo.pCommandBuffers = frameBuffers;
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
    VkCommandBuffer frameBuffers[2]{};
    std::uint32_t frameBufferCount = 0;
#if defined(APORIA_EXPERIMENTAL)
    if (impl_->shadowRecordedThisFrame && impl_->shadowCommandBuffer != VK_NULL_HANDLE) {
        frameBuffers[frameBufferCount++] = impl_->shadowCommandBuffer;
    }
#endif
    frameBuffers[frameBufferCount++] = impl_->commandBuffer;
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

bool Graphics::Impl::renderFrameCommands(VkFramebuffer targetFramebuffer, std::uint32_t width, std::uint32_t height) noexcept {
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
    const auto* materialSetsArg = materialStore.sets();
    const VkDescriptorSet defaultMaterialArg = materialStore.defaultSet();
#else
    const std::unordered_map<std::uint64_t, VkDescriptorSet>* materialSetsArg = nullptr;
    const VkDescriptorSet defaultMaterialArg = VK_NULL_HANDLE;
#endif
    float sunMatrix[16];
    buildSunMatrix(sunMatrix);
    writeSceneUniforms(uniformSlot, sunMatrix);

    const bool recordShadow = shadowsEnabled && pipelines.shadowPipeline != VK_NULL_HANDLE && !pendingItems.empty();
#if defined(APORIA_EXPERIMENTAL)
    shadowRecordedThisFrame = false;
    if (recordShadow && jobs && shadowCommandBuffer != VK_NULL_HANDLE) {
        // Record the shadow pass on a worker while the main thread records the
        // reflection/main/tonemap passes below. Both command buffers come from
        // the same pool but are reset/begun on their own thread.
        float shadowSunMatrix[16];
        std::memcpy(shadowSunMatrix, sunMatrix, sizeof(shadowSunMatrix));
        jobs->submit([this, shadowSunMatrix]() {
            vkResetCommandBuffer(shadowCommandBuffer, 0);
            VkCommandBufferBeginInfo info{};
            info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            if (vkBeginCommandBuffer(shadowCommandBuffer, &info) == VK_SUCCESS) {
                renderShadowMap(shadowCommandBuffer, shadowSunMatrix);
                vkEndCommandBuffer(shadowCommandBuffer);
            } else {
                shadowRecordedThisFrame = false;
            }
        });
        shadowRecordedThisFrame = true;
    } else if (recordShadow) {
        renderShadowMap(commandBuffer, sunMatrix);
    }
#else
    if (recordShadow) {
        renderShadowMap(commandBuffer, sunMatrix);
    }
#endif

#if defined(APORIA_EXPERIMENTAL)
    if (shadowRecordedThisFrame) {
        // The shadow pass ran in its own command buffer; make its depth writes
        // visible to the fragment shaders that sample the map below.
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
            commandBuffer,
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &shadowBarrier);
    }
#endif

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
            commandBuffer, pipelines.meshPipeline, pipelines.meshPipelineLayout,
            scene.set(uniformSlot), reflectViewProj,
            pendingItems, meshStore.resources(), VK_NULL_HANDLE, materialSetsArg, defaultMaterialArg);
        vkCmdEndRenderPass(commandBuffer);
    }

    // Main pass: meshes render into the HDR target at Medium/High, or
    // straight into the final target at Low.
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
            const VkDescriptorSet debugDescriptor = textureStore.descriptorSet();
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.pipeline);
            vkCmdBindDescriptorSets(
                commandBuffer,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                pipelines.pipelineLayout,
                0,
                1,
                &debugDescriptor,
                0,
                nullptr);
            vkCmdDraw(commandBuffer, 3, 1, 0, 0);
        }

        if (mainMeshPipeline != VK_NULL_HANDLE && !pendingItems.empty()) {
            graphics_internal::MeshRenderer::renderMeshes(
                commandBuffer, pipelines.meshPipeline, pipelines.meshPipelineLayout,
                scene.set(uniformSlot), pendingViewProj,
                pendingItems, meshStore.resources(), mainMeshPipeline, materialSetsArg, defaultMaterialArg);
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
