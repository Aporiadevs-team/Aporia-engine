#pragma once

#if defined(_WIN32) && !defined(VK_USE_PLATFORM_WIN32_KHR)
#define VK_USE_PLATFORM_WIN32_KHR
#endif

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Aporia::graphics_internal {

// Surface + swapchain state owned by Graphics::Impl. A bundle of references so
// the swapchain logic can live outside Graphics without renaming the members it
// manipulates.
struct SwapchainState {
    VkSurfaceKHR& surface;
    VkFormat& colorFormat;
    VkColorSpaceKHR& colorSpace;
    VkPresentModeKHR& presentMode;
    VkSwapchainKHR& swapchain;
    std::vector<VkImage>& images;
    std::vector<VkImageView>& views;
    std::vector<VkFramebuffer>& framebuffers;
    VkExtent2D& extent;
    std::vector<VkImageView>& depthViews;
    std::vector<VkImage>& depthImages;
    std::vector<VkDeviceMemory>& depthMemory;
};

// Vulkan entry points the swapchain needs (resolved by Graphics).
struct SwapchainApi {
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR getSupport = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR getCapabilities = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR getFormats = nullptr;
    PFN_vkGetPhysicalDeviceSurfacePresentModesKHR getPresentModes = nullptr;
    PFN_vkCreateSwapchainKHR createSwapchain = nullptr;
    PFN_vkDestroySwapchainKHR destroySwapchain = nullptr;
    PFN_vkGetSwapchainImagesKHR getSwapchainImages = nullptr;
};

struct SwapchainContext {
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    std::uint32_t graphicsFamily = 0;
    bool vsync = true;
    void* window = nullptr;              // native HWND
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;
    SwapchainApi api;
    SwapchainState* state = nullptr;
    std::string* error = nullptr;
};

// Validates queue support and picks the color format / present mode.
bool configureSurface(const SwapchainContext& context);
// Creates the swapchain, image views, depth images and framebuffers. Updates
// context.state->extent.
bool createSwapchain(const SwapchainContext& context);
// Destroys everything the swapchain owns (safe on null handles).
void destroySwapchain(const SwapchainContext& context) noexcept;

} // namespace Aporia::graphics_internal
