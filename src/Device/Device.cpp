#include "Device.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

namespace Aporia::graphics_internal {

namespace {

VKAPI_ATTR VkBool32 VKAPI_CALL validationCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT,
        const VkDebugUtilsMessengerCallbackDataEXT* data,
        void* userData) {
    if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0 ||
        (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0) {
        if (userData != nullptr) {
            ++(*static_cast<std::uint32_t*>(userData));
        }
        std::fprintf(
            stderr,
            "[Aporia validation] %s: %s\n",
            (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0 ? "ERROR" : "WARN",
            data != nullptr && data->pMessage != nullptr ? data->pMessage : "<no message>");
        std::fflush(stderr);
    }
    return VK_FALSE;
}

} // namespace

bool Device::hasLayer(const char* name) noexcept {
    std::uint32_t count = 0;
    if (vkEnumerateInstanceLayerProperties(&count, nullptr) != VK_SUCCESS || count == 0) {
        return false;
    }
    std::vector<VkLayerProperties> layers(count);
    if (vkEnumerateInstanceLayerProperties(&count, layers.data()) != VK_SUCCESS) {
        return false;
    }
    for (const VkLayerProperties& layer : layers) {
        if (std::strcmp(layer.layerName, name) == 0) {
            return true;
        }
    }
    return false;
}

bool Device::hasInstanceExtension(const char* name) noexcept {
    std::uint32_t count = 0;
    if (vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr) != VK_SUCCESS || count == 0) {
        return false;
    }
    std::vector<VkExtensionProperties> extensions(count);
    if (vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data()) != VK_SUCCESS) {
        return false;
    }
    for (const VkExtensionProperties& extension : extensions) {
        if (std::strcmp(extension.extensionName, name) == 0) {
            return true;
        }
    }
    return false;
}

bool Device::createInstance(
        bool headless,
        bool requestValidation,
        std::uint32_t* validationMessageCount,
        InstanceResult& result) noexcept {
    result.validationEnabled = requestValidation && hasLayer("VK_LAYER_KHRONOS_validation");

    VkApplicationInfo applicationInfo{};
    applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    applicationInfo.pApplicationName = "Aporia Engine";
    applicationInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    applicationInfo.pEngineName = "Aporia Engine";
    applicationInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    applicationInfo.apiVersion = VK_API_VERSION_1_3;

    const char* layerName = "VK_LAYER_KHRONOS_validation";
    VkInstanceCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pApplicationInfo = &applicationInfo;
    instanceInfo.enabledLayerCount = result.validationEnabled ? 1u : 0u;
    instanceInfo.ppEnabledLayerNames = result.validationEnabled ? &layerName : nullptr;

    result.debugUtils = hasInstanceExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    std::vector<const char*> instanceExtensions;
    if (!headless) {
#if defined(_WIN32)
        if (!hasInstanceExtension(VK_KHR_SURFACE_EXTENSION_NAME) ||
            !hasInstanceExtension(VK_KHR_WIN32_SURFACE_EXTENSION_NAME)) {
            return false;
        }
        instanceExtensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
        instanceExtensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#else
        return false;
#endif
    }
    if (result.validationEnabled && result.debugUtils) {
        instanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    instanceInfo.enabledExtensionCount = static_cast<std::uint32_t>(instanceExtensions.size());
    instanceInfo.ppEnabledExtensionNames = instanceExtensions.empty() ? nullptr : instanceExtensions.data();

    if (vkCreateInstance(&instanceInfo, nullptr, &result.instance) != VK_SUCCESS) {
        return false;
    }

    if (result.validationEnabled && result.debugUtils) {
        auto createMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(result.instance, "vkCreateDebugUtilsMessengerEXT"));
        if (createMessenger != nullptr) {
            VkDebugUtilsMessengerCreateInfoEXT messengerInfo{};
            messengerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
            messengerInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            messengerInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            messengerInfo.pfnUserCallback = validationCallback;
            messengerInfo.pUserData = validationMessageCount;
            if (createMessenger(result.instance, &messengerInfo, nullptr, &result.messenger) != VK_SUCCESS) {
                result.messenger = VK_NULL_HANDLE;
            }
        }
        if (result.messenger != VK_NULL_HANDLE) {
            result.destroyMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(result.instance, "vkDestroyDebugUtilsMessengerEXT"));
        }
    }
    return true;
}

bool Device::createDevice(
        VkInstance instance,
        bool headless,
        DeviceCreateResult& result) noexcept {
    std::uint32_t physicalDeviceCount = 0;
    if (vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, nullptr) != VK_SUCCESS ||
        physicalDeviceCount == 0) {
        return false;
    }
    std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
    if (vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, physicalDevices.data()) != VK_SUCCESS) {
        return false;
    }

    int bestScore = -1;
    for (const VkPhysicalDevice candidate : physicalDevices) {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(candidate, &properties);
        int score = 0;
        if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            score += 2;
        } else if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
            score += 1;
        }
        if (score > bestScore) {
            bestScore = score;
            result.physicalDevice = candidate;
            result.properties = properties;
            result.deviceName = properties.deviceName;
        }
    }
    if (result.physicalDevice == VK_NULL_HANDLE) {
        return false;
    }

    std::uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(result.physicalDevice, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(result.physicalDevice, &familyCount, families.data());
    bool graphicsFamilyFound = false;
    for (std::uint32_t index = 0; index < familyCount; ++index) {
        if ((families[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
            result.graphicsFamily = index;
            graphicsFamilyFound = true;
            break;
        }
    }
    if (!graphicsFamilyFound) {
        return false;
    }

    const float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = result.graphicsFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &queuePriority;

    const char* swapchainExtension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    if (!headless) {
        deviceInfo.enabledExtensionCount = 1;
        deviceInfo.ppEnabledExtensionNames = &swapchainExtension;
    }
    if (vkCreateDevice(result.physicalDevice, &deviceInfo, nullptr, &result.device) != VK_SUCCESS) {
        return false;
    }
    vkGetDeviceQueue(result.device, result.graphicsFamily, 0, &result.queue);
    vkGetPhysicalDeviceMemoryProperties(result.physicalDevice, &result.memoryProperties);
    return true;
}

bool Device::createCommandPool(
        VkDevice device,
        std::uint32_t queueFamily,
        VkCommandPool& outPool) noexcept {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamily;
    return vkCreateCommandPool(device, &poolInfo, nullptr, &outPool) == VK_SUCCESS;
}

bool Device::createCommandBuffer(
        VkDevice device,
        VkCommandPool pool,
        VkCommandBuffer& outBuffer) noexcept {
    VkCommandBufferAllocateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    info.commandPool = pool;
    info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    info.commandBufferCount = 1;
    return vkAllocateCommandBuffers(device, &info, &outBuffer) == VK_SUCCESS;
}

bool Device::createSecondaryCommandBuffer(
        VkDevice device,
        VkCommandPool pool,
        VkCommandBuffer& outBuffer) noexcept {
    VkCommandBufferAllocateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    info.commandPool = pool;
    info.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
    info.commandBufferCount = 1;
    return vkAllocateCommandBuffers(device, &info, &outBuffer) == VK_SUCCESS;
}

bool Device::createFrameSync(VkDevice device, FrameSync& sync) noexcept {
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (std::uint32_t slot = 0; slot < MaxFramesInFlight; ++slot) {
        if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &sync.acquire[slot]) != VK_SUCCESS ||
            vkCreateSemaphore(device, &semaphoreInfo, nullptr, &sync.renderFinished[slot]) != VK_SUCCESS ||
            vkCreateFence(device, &fenceInfo, nullptr, &sync.fences[slot]) != VK_SUCCESS) {
            return false;
        }
    }
    return true;
}

void Device::destroyFrameSync(VkDevice device, FrameSync& sync) noexcept {
    if (device != VK_NULL_HANDLE) {
        for (std::uint32_t slot = 0; slot < MaxFramesInFlight; ++slot) {
            if (sync.acquire[slot] != VK_NULL_HANDLE) vkDestroySemaphore(device, sync.acquire[slot], nullptr);
            if (sync.renderFinished[slot] != VK_NULL_HANDLE) vkDestroySemaphore(device, sync.renderFinished[slot], nullptr);
            if (sync.fences[slot] != VK_NULL_HANDLE) vkDestroyFence(device, sync.fences[slot], nullptr);
        }
    }
    sync = {};
}

} // namespace Aporia::graphics_internal
