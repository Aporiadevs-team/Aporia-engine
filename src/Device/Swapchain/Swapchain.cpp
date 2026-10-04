#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "Swapchain.hpp"

#include "Device/SceneManager/Depth/DepthTarget.hpp"
#include "Graphics/GlobalDrawer/Support/Support.hpp"

#include <algorithm>
#include <array>
#include <limits>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace Aporia::graphics_internal {

namespace {

void setError(const SwapchainContext& context, const char* message) {
    if (context.error != nullptr) {
        *context.error = message;
    }
}

} // namespace

bool configureSurface(const SwapchainContext& context) {
    if (context.state == nullptr) {
        return false;
    }
    SwapchainState& state = *context.state;
    if (state.surface == VK_NULL_HANDLE || context.window == nullptr) {
        setError(context, "Native Vulkan surface is not configured");
        return false;
    }
    if (context.api.getSupport == nullptr || context.api.getCapabilities == nullptr ||
        context.api.getFormats == nullptr || context.api.getPresentModes == nullptr) {
        setError(context, "Vulkan surface extension entry points are unavailable");
        return false;
    }

    VkBool32 supported = VK_FALSE;
    if (context.api.getSupport(context.physicalDevice, context.graphicsFamily, state.surface, &supported) != VK_SUCCESS ||
        supported != VK_TRUE) {
        setError(context, "Vulkan graphics queue does not support the native surface");
        return false;
    }

    VkSurfaceFormatKHR selectedFormat{};
    if (!chooseSurfaceFormat(context.physicalDevice, state.surface, selectedFormat)) {
        setError(context, "No supported Vulkan surface format");
        return false;
    }
    state.colorFormat = selectedFormat.format;
    state.colorSpace = selectedFormat.colorSpace;
    state.presentMode = choosePresentMode(context.physicalDevice, state.surface, context.vsync);
    return true;
}

bool createSwapchain(const SwapchainContext& context) {
    if (context.state == nullptr) {
        return false;
    }
    SwapchainState& state = *context.state;
    if (state.surface == VK_NULL_HANDLE || context.window == nullptr || context.api.getCapabilities == nullptr) {
        setError(context, "Native Vulkan swapchain is not configured");
        return false;
    }

    VkSurfaceCapabilitiesKHR capabilities{};
    if (context.api.getCapabilities(context.physicalDevice, state.surface, &capabilities) != VK_SUCCESS) {
        setError(context, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed");
        return false;
    }

    VkExtent2D clientExtent{};
#if defined(_WIN32)
    if (!getWindowExtent(static_cast<HWND>(context.window), clientExtent)) {
        setError(context, "Native window has no drawable client area");
        return false;
    }
#else
    (void)clientExtent;
    setError(context, "Native swapchains are supported only on Windows");
    return false;
#endif

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
        setError(context, "Native window has zero-sized swapchain extent");
        return false;
    }

    VkSwapchainCreateInfoKHR swapchainInfo{};
    swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchainInfo.surface = state.surface;
    swapchainInfo.minImageCount = capabilities.minImageCount;
    swapchainInfo.imageFormat = state.colorFormat;
    swapchainInfo.imageColorSpace = state.colorSpace;
    swapchainInfo.imageExtent = desiredExtent;
    swapchainInfo.imageArrayLayers = 1;
    swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchainInfo.preTransform = capabilities.currentTransform;
    swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchainInfo.presentMode = state.presentMode;
    swapchainInfo.clipped = VK_TRUE;
    swapchainInfo.oldSwapchain = state.swapchain;
    if (context.api.createSwapchain == nullptr || context.api.getSwapchainImages == nullptr) {
        setError(context, "Vulkan swapchain entry points are unavailable");
        return false;
    }
    if (context.api.createSwapchain(context.device, &swapchainInfo, nullptr, &state.swapchain) != VK_SUCCESS) {
        setError(context, "vkCreateSwapchainKHR failed");
        return false;
    }

    std::uint32_t imageCount = 0;
    if (context.api.getSwapchainImages(context.device, state.swapchain, &imageCount, nullptr) != VK_SUCCESS ||
        imageCount == 0) {
        setError(context, "vkGetSwapchainImagesKHR failed");
        return false;
    }
    state.images.resize(imageCount);
    if (context.api.getSwapchainImages(context.device, state.swapchain, &imageCount, state.images.data()) != VK_SUCCESS) {
        setError(context, "vkGetSwapchainImagesKHR failed");
        return false;
    }

    state.views.resize(state.images.size());
    for (std::size_t index = 0; index < state.images.size(); ++index) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = state.images[index];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = state.colorFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        if (vkCreateImageView(context.device, &viewInfo, nullptr, &state.views[index]) != VK_SUCCESS) {
            setError(context, "vkCreateImageView(swapchain) failed");
            return false;
        }
    }

    VkFormat depthFormat = context.depthFormat;
    if (depthFormat == VK_FORMAT_UNDEFINED) {
        depthFormat = chooseDepthFormat(context.physicalDevice);
        if (depthFormat == VK_FORMAT_UNDEFINED) {
            setError(context, "No supported depth attachment format");
            return false;
        }
    }
    state.depthViews.resize(state.images.size());
    state.depthImages.resize(state.images.size());
    state.depthMemory.resize(state.images.size());
    for (std::size_t index = 0; index < state.images.size(); ++index) {
        if (!DepthTarget::create(context.device, context.memoryProperties,
                                 depthFormat, desiredExtent, state.depthImages[index], state.depthMemory[index], state.depthViews[index])) {
            setError(context, "Depth target creation for swapchain failed");
            return false;
        }
    }

    state.framebuffers.resize(state.images.size());
    for (std::size_t index = 0; index < state.views.size(); ++index) {
        std::array<VkImageView, 2> attachments{state.views[index], state.depthViews[index]};
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = context.renderPass;
        framebufferInfo.attachmentCount = static_cast<std::uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = desiredExtent.width;
        framebufferInfo.height = desiredExtent.height;
        framebufferInfo.layers = 1;
        if (vkCreateFramebuffer(context.device, &framebufferInfo, nullptr, &state.framebuffers[index]) != VK_SUCCESS) {
            setError(context, "vkCreateFramebuffer(swapchain) failed");
            return false;
        }
    }

    state.extent = desiredExtent;
    return true;
}

void destroySwapchain(const SwapchainContext& context) noexcept {
    if (context.state == nullptr) {
        return;
    }
    SwapchainState& state = *context.state;
    VkDevice device = context.device;
    if (device != VK_NULL_HANDLE) {
        for (VkFramebuffer framebuffer : state.framebuffers) {
            vkDestroyFramebuffer(device, framebuffer, nullptr);
        }
        for (VkImageView view : state.views) {
            vkDestroyImageView(device, view, nullptr);
        }
        if (state.swapchain != VK_NULL_HANDLE && context.api.destroySwapchain != nullptr) {
            context.api.destroySwapchain(device, state.swapchain, nullptr);
        }
        for (VkImageView view : state.depthViews) {
            vkDestroyImageView(device, view, nullptr);
        }
        for (VkImage image : state.depthImages) {
            vkDestroyImage(device, image, nullptr);
        }
        for (VkDeviceMemory memory : state.depthMemory) {
            vkFreeMemory(device, memory, nullptr);
        }
    }
    state.framebuffers.clear();
    state.views.clear();
    state.images.clear();
    state.depthViews.clear();
    state.depthImages.clear();
    state.depthMemory.clear();
    state.swapchain = VK_NULL_HANDLE;
    state.extent = {0, 0};
}

} // namespace Aporia::graphics_internal
