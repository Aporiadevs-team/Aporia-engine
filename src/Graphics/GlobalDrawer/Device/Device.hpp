#pragma once

#if defined(_WIN32) && !defined(VK_USE_PLATFORM_WIN32_KHR)
#define VK_USE_PLATFORM_WIN32_KHR
#endif

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>

namespace Aporia::graphics_internal {

// Low-level Vulkan ownership boundary. Higher renderers should depend on this
// service instead of creating devices, queues, or command buffers themselves.
class Device {
public:
    static constexpr std::uint32_t MaxFramesInFlight = 2;

    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    std::uint32_t graphicsFamily = 0;

    [[nodiscard]] bool valid() const noexcept {
        return instance != VK_NULL_HANDLE && device != VK_NULL_HANDLE &&
               graphicsQueue != VK_NULL_HANDLE && commandBuffer != VK_NULL_HANDLE;
    }

    static bool hasLayer(const char* name) noexcept;
    static bool hasInstanceExtension(const char* name) noexcept;

    struct InstanceResult {
        VkInstance instance = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
        PFN_vkDestroyDebugUtilsMessengerEXT destroyMessenger = nullptr;
        bool validationEnabled = false;
        bool debugUtils = false;
    };

    // Creates the Vulkan instance, enables the validation layer when available
    // and requested, and installs the debug messenger.
    static bool createInstance(
        bool headless,
        bool requestValidation,
        std::uint32_t* validationMessageCount,
        InstanceResult& result) noexcept;

    struct DeviceCreateResult {
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        VkQueue queue = VK_NULL_HANDLE;
        std::uint32_t graphicsFamily = 0;
        VkPhysicalDeviceMemoryProperties memoryProperties{};
        std::string deviceName;
        VkPhysicalDeviceProperties properties{};
    };

    // Selects a physical device, creates the logical device and graphics queue,
    // and reads the memory properties.
    static bool createDevice(
        VkInstance instance,
        bool headless,
        DeviceCreateResult& result) noexcept;

    static bool createCommandPool(
        VkDevice device,
        std::uint32_t queueFamily,
        VkCommandPool& outPool) noexcept;

    static bool createCommandBuffer(
        VkDevice device,
        VkCommandPool pool,
        VkCommandBuffer& outBuffer) noexcept;

    struct FrameSync {
        VkSemaphore acquire[MaxFramesInFlight] = {};
        VkSemaphore renderFinished[MaxFramesInFlight] = {};
        VkFence fences[MaxFramesInFlight] = {};
    };

    static bool createFrameSync(VkDevice device, FrameSync& sync) noexcept;
    static void destroyFrameSync(VkDevice device, FrameSync& sync) noexcept;
};

} // namespace Aporia::graphics_internal
