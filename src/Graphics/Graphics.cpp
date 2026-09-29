#include "Aporia/Graphics/Graphics.hpp"
#include "Aporia/Graphics/MeshPrimitives.hpp"

#if defined(_WIN32) && !defined(VK_USE_PLATFORM_WIN32_KHR)
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <vulkan/vulkan.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

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
    static const bool enabled = std::getenv("APORIA_GRAPHICS_DEBUG") != nullptr;
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

constexpr std::uint32_t renderWidth = 64;
constexpr std::uint32_t renderHeight = 64;

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

bool hasLayer(const char* name) {
    std::uint32_t count = 0;
    if (vkEnumerateInstanceLayerProperties(&count, nullptr) != VK_SUCCESS || count == 0) {
        return false;
    }

    std::vector<VkLayerProperties> layers(count);
    if (vkEnumerateInstanceLayerProperties(&count, layers.data()) != VK_SUCCESS) {
        return false;
    }

    for (const VkLayerProperties& layer : layers) {
        if (std::string(layer.layerName) == name) {
            return true;
        }
    }
    return false;
}

bool hasInstanceExtension(const char* name) {
    std::uint32_t count = 0;
    if (vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr) != VK_SUCCESS || count == 0) {
        return false;
    }

    std::vector<VkExtensionProperties> extensions(count);
    if (vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data()) != VK_SUCCESS) {
        return false;
    }

    for (const VkExtensionProperties& extension : extensions) {
        if (std::string(extension.extensionName) == name) {
            return true;
        }
    }
    return false;
}

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

bool chooseColorFormat(VkPhysicalDevice physicalDevice, VkFormat& format) {
    constexpr std::array<VkFormat, 4> candidates{
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_B8G8R8A8_UNORM,
        VK_FORMAT_A8B8G8R8_UNORM_PACK32,
        VK_FORMAT_R8G8B8A8_SRGB,
    };

    for (const VkFormat candidate : candidates) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(physicalDevice, candidate, &properties);
        if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) != 0) {
            format = candidate;
            return true;
        }
    }
    return false;
}

std::vector<std::uint32_t> readSpirv(const std::string& path, bool& success) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        success = false;
        return {};
    }

    const std::streamoff byteCount = file.tellg();
    if (byteCount <= 0 || byteCount % static_cast<std::streamoff>(sizeof(std::uint32_t)) != 0) {
        success = false;
        return {};
    }

    std::vector<std::uint32_t> words(static_cast<std::size_t>(byteCount) / sizeof(std::uint32_t));
    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(words.data()), byteCount)) {
        success = false;
        return {};
    }

    success = true;
    return words;
}

#if defined(_WIN32)
bool getWindowExtent(HWND window, VkExtent2D& extent) noexcept {
    RECT clientRect{};
    if (window == nullptr || !GetClientRect(window, &clientRect)) {
        return false;
    }

    const int width = clientRect.right - clientRect.left;
    const int height = clientRect.bottom - clientRect.top;
    if (width <= 0 || height <= 0) {
        return false;
    }

    extent.width = static_cast<std::uint32_t>(width);
    extent.height = static_cast<std::uint32_t>(height);
    return true;
}
#endif

bool chooseSurfaceFormat(
    VkPhysicalDevice physicalDevice,
    VkSurfaceKHR surface,
    VkSurfaceFormatKHR& selectedFormat) {
    std::uint32_t count = 0;
    if (vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &count, nullptr) != VK_SUCCESS || count == 0) {
        return false;
    }

    std::vector<VkSurfaceFormatKHR> formats(count);
    if (vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &count, formats.data()) != VK_SUCCESS) {
        return false;
    }

    for (const VkSurfaceFormatKHR& format : formats) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format.format, &properties);
        if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) != 0) {
            selectedFormat = format;
            return true;
        }
    }
    return false;
}

VkPresentModeKHR choosePresentMode(
    VkPhysicalDevice physicalDevice,
    VkSurfaceKHR surface) {
    std::uint32_t count = 0;
    if (vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &count, nullptr) != VK_SUCCESS || count == 0) {
        return VK_PRESENT_MODE_IMMEDIATE_KHR;
    }

    std::vector<VkPresentModeKHR> modes(count);
    if (vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &count, modes.data()) != VK_SUCCESS) {
        return VK_PRESENT_MODE_IMMEDIATE_KHR;
    }

    for (const VkPresentModeKHR mode : modes) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return mode;
        }
    }
    for (const VkPresentModeKHR mode : modes) {
        if (mode == VK_PRESENT_MODE_FIFO_KHR) {
            return mode;
        }
    }
    return modes.front();
}

bool textureFormatInfo(
    Aporia::TextureFormat format,
    VkFormat& vulkanFormat,
    std::uint32_t& bytesPerPixel) {
    switch (format) {
    case Aporia::TextureFormat::Rgba8Unorm:
        vulkanFormat = VK_FORMAT_R8G8B8A8_UNORM;
        bytesPerPixel = 4;
        return true;
    case Aporia::TextureFormat::Bgra8Unorm:
        vulkanFormat = VK_FORMAT_B8G8R8A8_UNORM;
        bytesPerPixel = 4;
        return true;
    case Aporia::TextureFormat::Rgba8Srgb:
        vulkanFormat = VK_FORMAT_R8G8B8A8_SRGB;
        bytesPerPixel = 4;
        return true;
    }
    return false;
}

}

namespace Aporia {

struct Graphics::Impl {
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
    static constexpr std::uint32_t maxFramesInFlight = 2;
    std::uint32_t currentFrameSlot = 0;
    VkSemaphore acquireSemaphores[maxFramesInFlight] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkSemaphore renderFinishedSemaphores[maxFramesInFlight] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkFence frameFences[maxFramesInFlight] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkSemaphore acquireSemaphore = VK_NULL_HANDLE; // legacy single-slot, headless path
    VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE;

    // Mesh pipeline: vertex data in buffers, camera in push constants.
    VkPipelineLayout meshPipelineLayout = VK_NULL_HANDLE;
    VkPipeline meshPipeline = VK_NULL_HANDLE;
    VkShaderModule meshVertexShader = VK_NULL_HANDLE;
    VkShaderModule meshFragmentShader = VK_NULL_HANDLE;

    VkImage depthImage = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory = VK_NULL_HANDLE;
    VkImageView depthView = VK_NULL_HANDLE;
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;
    std::uint32_t depthWidth = 0;
    std::uint32_t depthHeight = 0;
    std::vector<VkImageView> swapchainDepthViews;
    std::vector<VkDeviceMemory> swapchainDepthMemory;
    std::vector<VkImage> swapchainDepthImages;

    struct MeshResource {
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
        VkBuffer indexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory indexMemory = VK_NULL_HANDLE;
        std::uint32_t vertexCount = 0;
        std::uint32_t indexCount = 0;
    };

    struct PushConstants {
        float viewProj[16];
        float model[16];
    };

    // Column-major 4x4 multiply: out = a * b.
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
    std::vector<RenderItem> pendingItems;
    bool drawDebugTriangle = true;

    struct TextureResource {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
        VkFormat format = VK_FORMAT_UNDEFINED;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::size_t dataSize = 0;
    };

    std::unordered_map<std::uint64_t, TextureResource> textures;
    TextureHandle defaultTexture;
    TextureHandle boundTexture;
    std::uint64_t nextTextureId = 1;
    std::uint64_t textureUploadCount = 0;
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

    void destroyTextureResource(TextureResource& texture) noexcept {
        if (device != VK_NULL_HANDLE) {
            if (texture.sampler != VK_NULL_HANDLE) {
                vkDestroySampler(device, texture.sampler, nullptr);
            }
            if (texture.view != VK_NULL_HANDLE) {
                vkDestroyImageView(device, texture.view, nullptr);
            }
            if (texture.image != VK_NULL_HANDLE) {
                vkDestroyImage(device, texture.image, nullptr);
            }
            if (texture.memory != VK_NULL_HANDLE) {
                vkFreeMemory(device, texture.memory, nullptr);
            }
        }
        texture = TextureResource{};
    }

    bool createBufferWithData(
        VkDeviceSize size,
        const void* data,
        VkBufferUsageFlags usage,
        VkBuffer& buffer,
        VkDeviceMemory& memory) {
        buffer = VK_NULL_HANDLE;
        memory = VK_NULL_HANDLE;
        if (size == 0 || data == nullptr) {
            setError("Buffer upload data is empty");
            return false;
        }

        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
        auto destroyStaging = [&]() noexcept {
            if (stagingBuffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(device, stagingBuffer, nullptr);
            }
            if (stagingMemory != VK_NULL_HANDLE) {
                vkFreeMemory(device, stagingMemory, nullptr);
            }
        };

        VkBufferCreateInfo stagingInfo{};
        stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        stagingInfo.size = size;
        stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device, &stagingInfo, nullptr, &stagingBuffer) != VK_SUCCESS) {
            setError("vkCreateBuffer(staging) failed");
            return false;
        }

        VkMemoryRequirements stagingRequirements{};
        vkGetBufferMemoryRequirements(device, stagingBuffer, &stagingRequirements);
        std::uint32_t stagingType = 0;
        if (!findMemoryType(
                memoryProperties,
                stagingRequirements.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                stagingType)) {
            destroyStaging();
            setError("No host-visible memory for staging buffer");
            return false;
        }

        VkMemoryAllocateInfo stagingAllocate{};
        stagingAllocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        stagingAllocate.allocationSize = stagingRequirements.size;
        stagingAllocate.memoryTypeIndex = stagingType;
        if (vkAllocateMemory(device, &stagingAllocate, nullptr, &stagingMemory) != VK_SUCCESS) {
            destroyStaging();
            setError("vkAllocateMemory(staging) failed");
            return false;
        }
        if (vkBindBufferMemory(device, stagingBuffer, stagingMemory, 0) != VK_SUCCESS) {
            destroyStaging();
            setError("vkBindBufferMemory(staging) failed");
            return false;
        }

        void* mapped = nullptr;
        if (vkMapMemory(device, stagingMemory, 0, size, 0, &mapped) != VK_SUCCESS || mapped == nullptr) {
            destroyStaging();
            setError("vkMapMemory(staging) failed");
            return false;
        }
        std::memcpy(mapped, data, static_cast<std::size_t>(size));
        vkUnmapMemory(device, stagingMemory);

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
            destroyStaging();
            setError("vkCreateBuffer(device local) failed");
            return false;
        }

        VkMemoryRequirements bufferRequirements{};
        vkGetBufferMemoryRequirements(device, buffer, &bufferRequirements);
        std::uint32_t bufferType = 0;
        if (!findMemoryType(memoryProperties, bufferRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, bufferType)) {
            destroyStaging();
            setError("No device-local memory for buffer");
            return false;
        }

        VkMemoryAllocateInfo bufferAllocate{};
        bufferAllocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        bufferAllocate.allocationSize = bufferRequirements.size;
        bufferAllocate.memoryTypeIndex = bufferType;
        if (vkAllocateMemory(device, &bufferAllocate, nullptr, &memory) != VK_SUCCESS) {
            destroyStaging();
            setError("vkAllocateMemory(device local) failed");
            return false;
        }
        if (vkBindBufferMemory(device, buffer, memory, 0) != VK_SUCCESS) {
            destroyStaging();
            setError("vkBindBufferMemory(device local) failed");
            return false;
        }

        vkResetCommandBuffer(commandBuffer, 0);
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
            destroyStaging();
            setError("vkBeginCommandBuffer(buffer upload) failed");
            return false;
        }

        VkBufferCopy copyRegion{};
        copyRegion.size = size;
        vkCmdCopyBuffer(commandBuffer, stagingBuffer, buffer, 1, &copyRegion);

        if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
            destroyStaging();
            setError("vkEndCommandBuffer(buffer upload) failed");
            return false;
        }

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;
        const VkResult submitResult = vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
        const VkResult waitResult = submitResult == VK_SUCCESS ? vkQueueWaitIdle(queue) : submitResult;
        destroyStaging();
        if (submitResult != VK_SUCCESS || waitResult != VK_SUCCESS) {
            setError("Buffer upload submission failed");
            return false;
        }
        return true;
    }

    void destroyMeshResource(MeshResource& mesh) noexcept {
        if (device != VK_NULL_HANDLE) {
            if (mesh.indexBuffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(device, mesh.indexBuffer, nullptr);
            }
            if (mesh.indexMemory != VK_NULL_HANDLE) {
                vkFreeMemory(device, mesh.indexMemory, nullptr);
            }
            if (mesh.vertexBuffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(device, mesh.vertexBuffer, nullptr);
            }
            if (mesh.vertexMemory != VK_NULL_HANDLE) {
                vkFreeMemory(device, mesh.vertexMemory, nullptr);
            }
        }
        mesh = MeshResource{};
    }

    VkFormat chooseDepthFormat() noexcept {
        const VkFormat candidates[] = {
            VK_FORMAT_D32_SFLOAT,
            VK_FORMAT_D32_SFLOAT_S8_UINT,
            VK_FORMAT_D24_UNORM_S8_UINT,
        };
        for (const VkFormat candidate : candidates) {
            VkFormatProperties properties{};
            vkGetPhysicalDeviceFormatProperties(physicalDevice, candidate, &properties);
            if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0) {
                return candidate;
            }
        }
        return VK_FORMAT_UNDEFINED;
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
        image = VK_NULL_HANDLE;
        memory = VK_NULL_HANDLE;
        view = VK_NULL_HANDLE;

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = depthFormat;
        imageInfo.extent = {extent.width, extent.height, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
            setError("vkCreateImage(depth) failed");
            return false;
        }

        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(device, image, &requirements);
        std::uint32_t memoryType = 0;
        if (!findMemoryType(memoryProperties, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memoryType)) {
            setError("No device-local memory for depth image");
            return false;
        }

        VkMemoryAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocateInfo.allocationSize = requirements.size;
        allocateInfo.memoryTypeIndex = memoryType;
        if (vkAllocateMemory(device, &allocateInfo, nullptr, &memory) != VK_SUCCESS) {
            setError("vkAllocateMemory(depth) failed");
            return false;
        }
        if (vkBindImageMemory(device, image, memory, 0) != VK_SUCCESS) {
            setError("vkBindImageMemory(depth) failed");
            return false;
        }

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = depthFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        if (vkCreateImageView(device, &viewInfo, nullptr, &view) != VK_SUCCESS) {
            setError("vkCreateImageView(depth) failed");
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

    bool uploadTextureData(
        TextureResource& texture,
        const void* pixels,
        std::size_t dataSize,
        VkImageLayout oldLayout) {
        if (device == VK_NULL_HANDLE || commandBuffer == VK_NULL_HANDLE || pixels == nullptr ||
            dataSize == 0 || dataSize != texture.dataSize) {
            setError("Texture upload data is invalid");
            return false;
        }

        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
        auto destroyStaging = [&]() noexcept {
            if (device != VK_NULL_HANDLE) {
                if (stagingBuffer != VK_NULL_HANDLE) {
                    vkDestroyBuffer(device, stagingBuffer, nullptr);
                }
                if (stagingMemory != VK_NULL_HANDLE) {
                    vkFreeMemory(device, stagingMemory, nullptr);
                }
            }
            stagingBuffer = VK_NULL_HANDLE;
            stagingMemory = VK_NULL_HANDLE;
        };

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = dataSize;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device, &bufferInfo, nullptr, &stagingBuffer) != VK_SUCCESS) {
            setError("vkCreateBuffer(texture staging) failed");
            return false;
        }

        VkMemoryRequirements bufferRequirements{};
        vkGetBufferMemoryRequirements(device, stagingBuffer, &bufferRequirements);
        std::uint32_t stagingMemoryType = 0;
        if (!findMemoryType(
                memoryProperties,
                bufferRequirements.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                stagingMemoryType)) {
            destroyStaging();
            setError("No host-visible coherent memory for texture staging");
            return false;
        }

        VkMemoryAllocateInfo stagingMemoryInfo{};
        stagingMemoryInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        stagingMemoryInfo.allocationSize = bufferRequirements.size;
        stagingMemoryInfo.memoryTypeIndex = stagingMemoryType;
        if (vkAllocateMemory(device, &stagingMemoryInfo, nullptr, &stagingMemory) != VK_SUCCESS) {
            destroyStaging();
            setError("vkAllocateMemory(texture staging) failed");
            return false;
        }
        if (vkBindBufferMemory(device, stagingBuffer, stagingMemory, 0) != VK_SUCCESS) {
            destroyStaging();
            setError("vkBindBufferMemory(texture staging) failed");
            return false;
        }

        void* mapped = nullptr;
        if (vkMapMemory(device, stagingMemory, 0, dataSize, 0, &mapped) != VK_SUCCESS || mapped == nullptr) {
            destroyStaging();
            setError("vkMapMemory(texture staging) failed");
            return false;
        }
        std::memcpy(mapped, pixels, dataSize);
        vkUnmapMemory(device, stagingMemory);

        vkDeviceWaitIdle(device);
        const VkResult resetResult = vkResetCommandBuffer(commandBuffer, 0);
        if (resetResult != VK_SUCCESS) {
            destroyStaging();
            setError("vkResetCommandBuffer(texture upload) failed");
            return false;
        }

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
            destroyStaging();
            setError("vkBeginCommandBuffer(texture upload) failed");
            return false;
        }

        VkImageMemoryBarrier toTransfer{};
        toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toTransfer.oldLayout = oldLayout;
        toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.image = texture.image;
        toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        toTransfer.subresourceRange.levelCount = 1;
        toTransfer.subresourceRange.layerCount = 1;
        toTransfer.srcAccessMask = oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            ? VK_ACCESS_SHADER_READ_BIT
            : 0;
        toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(
            commandBuffer,
            oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &toTransfer);

        VkBufferImageCopy copyRegion{};
        copyRegion.bufferOffset = 0;
        copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.imageSubresource.layerCount = 1;
        copyRegion.imageExtent = {texture.width, texture.height, 1};
        vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

        VkImageMemoryBarrier toRead{};
        toRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toRead.image = texture.image;
        toRead.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        toRead.subresourceRange.levelCount = 1;
        toRead.subresourceRange.layerCount = 1;
        toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &toRead);

        if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
            destroyStaging();
            setError("vkEndCommandBuffer(texture upload) failed");
            return false;
        }

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;
        const VkResult submitResult = vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
        const VkResult waitResult = submitResult == VK_SUCCESS ? vkQueueWaitIdle(queue) : submitResult;
        destroyStaging();
        if (submitResult != VK_SUCCESS || waitResult != VK_SUCCESS) {
            setError("Vulkan texture upload submission failed");
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

        struct CleanupGuard {
            Impl* impl = nullptr;
            TextureResource* texture = nullptr;
            bool committed = false;

            ~CleanupGuard() {
                if (!committed && impl != nullptr && texture != nullptr) {
                    impl->destroyTextureResource(*texture);
                }
            }
        } cleanup{this, &texture, false};

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = format;
        imageInfo.extent = {description.width, description.height, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(device, &imageInfo, nullptr, &texture.image) != VK_SUCCESS) {
            setError("vkCreateImage(texture) failed");
            return false;
        }

        VkMemoryRequirements imageRequirements{};
        vkGetImageMemoryRequirements(device, texture.image, &imageRequirements);
        std::uint32_t imageMemoryType = 0;
        if (!findMemoryType(memoryProperties, imageRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, imageMemoryType)) {
            setError("No device-local memory for texture");
            return false;
        }
        VkMemoryAllocateInfo imageMemoryInfo{};
        imageMemoryInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        imageMemoryInfo.allocationSize = imageRequirements.size;
        imageMemoryInfo.memoryTypeIndex = imageMemoryType;
        if (vkAllocateMemory(device, &imageMemoryInfo, nullptr, &texture.memory) != VK_SUCCESS) {
            setError("vkAllocateMemory(texture) failed");
            return false;
        }
        if (vkBindImageMemory(device, texture.image, texture.memory, 0) != VK_SUCCESS) {
            setError("vkBindImageMemory(texture) failed");
            return false;
        }

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = texture.image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        if (vkCreateImageView(device, &viewInfo, nullptr, &texture.view) != VK_SUCCESS) {
            setError("vkCreateImageView(texture) failed");
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
        samplerInfo.maxLod = 1.0f;
        if (vkCreateSampler(device, &samplerInfo, nullptr, &texture.sampler) != VK_SUCCESS) {
            setError("vkCreateSampler(texture) failed");
            return false;
        }

        texture.format = format;
        texture.width = description.width;
        texture.height = description.height;
        texture.dataSize = description.dataSize;
        if (!uploadTextureData(texture, description.pixels, description.dataSize, VK_IMAGE_LAYOUT_UNDEFINED)) {
            return false;
        }
        cleanup.committed = true;
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
        presentMode = choosePresentMode(physicalDevice, surface);
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

        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = colorFormat;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        // Swapchain images must end the pass in PRESENT_SRC_KHR, otherwise the
        // presented contents are undefined (black screen). Headless keeps the
        // attachment layout since nothing is presented.
        colorAttachment.finalLayout = headless
            ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
            : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentDescription depthAttachment{};
        depthAttachment.format = depthFormat;
        depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        std::array<VkAttachmentDescription, 2> attachments{colorAttachment, depthAttachment};

        VkAttachmentReference colorReference{};
        colorReference.attachment = 0;
        colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depthReference{};
        depthReference.attachment = 1;
        depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorReference;
        subpass.pDepthStencilAttachment = &depthReference;

        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = static_cast<std::uint32_t>(attachments.size());
        renderPassInfo.pAttachments = attachments.data();
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
            setError("vkCreateRenderPass failed");
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
            return createSwapchainResources();
        } catch (...) {
            destroySwapchainResources();
            setError("Vulkan swapchain recreation failed due to allocation failure");
            return false;
        }
    }

    bool recordFrame(VkFramebuffer targetFramebuffer, std::uint32_t width, std::uint32_t height) noexcept {
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

        std::array<VkClearValue, 2> clearValues{};
        clearValues[0].color = {{0.02f, 0.03f, 0.05f, 1.0f}};
        clearValues[1].depthStencil = {1.0f, 0};
        VkRenderPassBeginInfo renderPassBegin{};
        renderPassBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassBegin.renderPass = renderPass;
        renderPassBegin.framebuffer = targetFramebuffer;
        renderPassBegin.renderArea.extent = {width, height};
        renderPassBegin.clearValueCount = static_cast<std::uint32_t>(clearValues.size());
        renderPassBegin.pClearValues = clearValues.data();
        vkCmdBeginRenderPass(commandBuffer, &renderPassBegin, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(width);
        viewport.height = static_cast<float>(height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = {width, height};
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

        if (drawDebugTriangle) {
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

        if (meshPipeline != VK_NULL_HANDLE && !pendingItems.empty()) {
            PushConstants push{};
            std::memcpy(push.viewProj, pendingViewProj, sizeof(push.viewProj));
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, meshPipeline);
            vkCmdBindDescriptorSets(
                commandBuffer,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                meshPipelineLayout,
                0,
                1,
                &descriptorSet,
                0,
                nullptr);

            std::uint32_t drawnItems = 0;
            for (const RenderItem& item : pendingItems) {
                const auto meshIt = meshes.find(item.meshId);
                if (meshIt == meshes.end() || meshIt->second.vertexBuffer == VK_NULL_HANDLE) {
                    continue;
                }
                ++drawnItems;
                std::memcpy(push.model, item.modelMatrix, sizeof(push.model));
                vkCmdPushConstants(
                    commandBuffer,
                    meshPipelineLayout,
                    VK_SHADER_STAGE_VERTEX_BIT,
                    0,
                    sizeof(PushConstants),
                    &push);

                VkDeviceSize offset = 0;
                vkCmdBindVertexBuffers(commandBuffer, 0, 1, &meshIt->second.vertexBuffer, &offset);
                if (meshIt->second.indexBuffer != VK_NULL_HANDLE) {
                    vkCmdBindIndexBuffer(commandBuffer, meshIt->second.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
                    vkCmdDrawIndexed(commandBuffer, meshIt->second.indexCount, 1, 0, 0, 0);
                } else {
                    vkCmdDraw(commandBuffer, meshIt->second.vertexCount, 1, 0, 0);
                }
            }
            engineLog("frame drawn: drawn=%u/%zu", drawnItems, pendingItems.size());
        }

        vkCmdEndRenderPass(commandBuffer);

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
            for (std::uint32_t slot = 0; slot < maxFramesInFlight; ++slot) {
                if (acquireSemaphores[slot] != VK_NULL_HANDLE) {
                    vkDestroySemaphore(device, acquireSemaphores[slot], nullptr);
                    acquireSemaphores[slot] = VK_NULL_HANDLE;
                }
                if (renderFinishedSemaphores[slot] != VK_NULL_HANDLE) {
                    vkDestroySemaphore(device, renderFinishedSemaphores[slot], nullptr);
                    renderFinishedSemaphores[slot] = VK_NULL_HANDLE;
                }
                if (frameFences[slot] != VK_NULL_HANDLE) {
                    vkDestroyFence(device, frameFences[slot], nullptr);
                    frameFences[slot] = VK_NULL_HANDLE;
                }
            }
            destroyDescriptorResources();
            for (auto& entry : textures) {
                destroyTextureResource(entry.second);
            }
            textures.clear();
            defaultTexture = invalidTextureHandle;
            boundTexture = invalidTextureHandle;
            nextTextureId = 1;
            textureUploadCount = 0;
            for (auto& entry : meshes) {
                destroyMeshResource(entry.second);
            }
            meshes.clear();
            nextMeshId = 1;
            pendingItems.clear();
            destroyDepthResources();
            destroySwapchainResources();
            if (renderPass != VK_NULL_HANDLE) {
                vkDestroyRenderPass(device, renderPass, nullptr);
                renderPass = VK_NULL_HANDLE;
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
                destroyTextureResource(entry.second);
            }
            textures.clear();
            defaultTexture = invalidTextureHandle;
            boundTexture = invalidTextureHandle;
            nextTextureId = 1;
            textureUploadCount = 0;
            for (auto& entry : meshes) {
                destroyMeshResource(entry.second);
            }
            meshes.clear();
            nextMeshId = 1;
            pendingItems.clear();
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
        for (std::uint32_t slot = 0; slot < maxFramesInFlight; ++slot) {
            acquireSemaphores[slot] = VK_NULL_HANDLE;
            renderFinishedSemaphores[slot] = VK_NULL_HANDLE;
            frameFences[slot] = VK_NULL_HANDLE;
        }
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
    impl_->validationMessageCount = 0;

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

    const bool validationLayerAvailable = hasLayer("VK_LAYER_KHRONOS_validation");
    impl_->validationEnabled = config.enableValidation && validationLayerAvailable;
    engineLog("=== init headless=%d validation=%d ===", config.headless ? 1 : 0, impl_->validationEnabled ? 1 : 0);

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
    instanceInfo.enabledLayerCount = impl_->validationEnabled ? 1u : 0u;
    instanceInfo.ppEnabledLayerNames = impl_->validationEnabled ? &layerName : nullptr;

    const bool debugUtilsAvailable = hasInstanceExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    std::vector<const char*> instanceExtensions;
    if (!impl_->headless) {
        if (!hasInstanceExtension(VK_KHR_SURFACE_EXTENSION_NAME)) {
            impl_->setError("Vulkan surface extension is unavailable");
            return false;
        }
#if defined(_WIN32)
        if (!hasInstanceExtension(VK_KHR_WIN32_SURFACE_EXTENSION_NAME)) {
            impl_->setError("Vulkan Win32 surface extension is unavailable");
            return false;
        }
        instanceExtensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
        instanceExtensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#else
        impl_->setError("Vulkan Win32 surface extension is unavailable");
        return false;
#endif
    }
    if (impl_->validationEnabled && debugUtilsAvailable) {
        instanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    instanceInfo.enabledExtensionCount = static_cast<std::uint32_t>(instanceExtensions.size());
    instanceInfo.ppEnabledExtensionNames = instanceExtensions.empty() ? nullptr : instanceExtensions.data();

    if (vkCreateInstance(&instanceInfo, nullptr, &impl_->instance) != VK_SUCCESS) {
        impl_->setError("vkCreateInstance failed");
        return false;
    }

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

    if (impl_->validationEnabled && debugUtilsAvailable) {
        auto createMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(impl_->instance, "vkCreateDebugUtilsMessengerEXT"));
        if (createMessenger != nullptr) {
            VkDebugUtilsMessengerCreateInfoEXT messengerInfo{};
            messengerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
            messengerInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            messengerInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            messengerInfo.pfnUserCallback = validationCallback;
            messengerInfo.pUserData = &impl_->validationMessageCount;
            if (createMessenger(impl_->instance, &messengerInfo, nullptr, &impl_->debugMessenger) != VK_SUCCESS) {
                impl_->debugMessenger = VK_NULL_HANDLE;
            }
        }
        if (impl_->debugMessenger != VK_NULL_HANDLE) {
            impl_->destroyDebugMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(impl_->instance, "vkDestroyDebugUtilsMessengerEXT"));
        }
    }

    std::uint32_t physicalDeviceCount = 0;
    if (vkEnumeratePhysicalDevices(impl_->instance, &physicalDeviceCount, nullptr) != VK_SUCCESS || physicalDeviceCount == 0) {
        impl_->setError("No Vulkan physical device is available");
        shutdown();
        return false;
    }

    std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
    if (vkEnumeratePhysicalDevices(impl_->instance, &physicalDeviceCount, physicalDevices.data()) != VK_SUCCESS) {
        impl_->setError("vkEnumeratePhysicalDevices failed");
        shutdown();
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
            impl_->physicalDevice = candidate;
            impl_->deviceName = properties.deviceName;
        }
    }

    if (impl_->physicalDevice == VK_NULL_HANDLE) {
        impl_->setError("Vulkan physical device selection failed");
        shutdown();
        return false;
    }
    engineLog("device: %s headless=%d", impl_->deviceName.c_str(), impl_->headless ? 1 : 0);

    std::uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(impl_->physicalDevice, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(impl_->physicalDevice, &familyCount, families.data());
    bool graphicsFamilyFound = false;
    for (std::uint32_t index = 0; index < familyCount; ++index) {
        if ((families[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
            impl_->graphicsFamily = index;
            graphicsFamilyFound = true;
            break;
        }
    }
    if (!graphicsFamilyFound) {
        impl_->setError("Vulkan graphics queue family is unavailable");
        shutdown();
        return false;
    }

    const float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = impl_->graphicsFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &queuePriority;

    const char* swapchainExtension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    if (!impl_->headless) {
        deviceInfo.enabledExtensionCount = 1;
        deviceInfo.ppEnabledExtensionNames = &swapchainExtension;
    }
    if (vkCreateDevice(impl_->physicalDevice, &deviceInfo, nullptr, &impl_->device) != VK_SUCCESS) {
        impl_->setError("vkCreateDevice failed");
        shutdown();
        return false;
    }
    vkGetDeviceQueue(impl_->device, impl_->graphicsFamily, 0, &impl_->queue);
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

    vkGetPhysicalDeviceMemoryProperties(impl_->physicalDevice, &impl_->memoryProperties);
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

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = impl_->graphicsFamily;
    if (vkCreateCommandPool(impl_->device, &poolInfo, nullptr, &impl_->commandPool) != VK_SUCCESS) {
        impl_->setError("vkCreateCommandPool failed");
        shutdown();
        return false;
    }

    VkCommandBufferAllocateInfo commandBufferInfo{};
    commandBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandBufferInfo.commandPool = impl_->commandPool;
    commandBufferInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandBufferInfo.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(impl_->device, &commandBufferInfo, &impl_->commandBuffer) != VK_SUCCESS) {
        impl_->setError("vkAllocateCommandBuffers failed");
        shutdown();
        return false;
    }

    if (!impl_->headless) {
        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // first frame has nothing to wait on
        for (std::uint32_t slot = 0; slot < Impl::maxFramesInFlight; ++slot) {
            if (vkCreateSemaphore(impl_->device, &semaphoreInfo, nullptr, &impl_->acquireSemaphores[slot]) != VK_SUCCESS ||
                vkCreateSemaphore(impl_->device, &semaphoreInfo, nullptr, &impl_->renderFinishedSemaphores[slot]) != VK_SUCCESS ||
                vkCreateFence(impl_->device, &fenceInfo, nullptr, &impl_->frameFences[slot]) != VK_SUCCESS) {
                impl_->setError("vkCreateSemaphore/vkCreateFence failed");
                shutdown();
                return false;
            }
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
    meshLayoutInfo.pSetLayouts = &impl_->descriptorSetLayout;
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
    vertexBinding.stride = sizeof(MeshVertex);
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
    if (vkCreateGraphicsPipelines(impl_->device, VK_NULL_HANDLE, 1, &meshPipelineInfo, nullptr, &impl_->meshPipeline) != VK_SUCCESS) {
        impl_->setError("vkCreateGraphicsPipelines(mesh) failed");
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
    if (impl_->headless) {
        return renderHeadlessFrame();
    }
    return renderNativeFrame();
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
    if (!impl_->recordFrame(impl_->framebuffer, renderWidth, renderHeight)) {
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
    if (vkWaitForFences(impl_->device, 1, &impl_->frameFences[frameSlot], VK_TRUE, std::numeric_limits<std::uint64_t>::max()) != VK_SUCCESS) {
        impl_->lastError = "vkWaitForFences failed";
        return false;
    }

    std::uint32_t imageIndex = 0;
    VkResult acquireResult = VK_ERROR_OUT_OF_DATE_KHR;
    for (int attempt = 0; attempt < 2; ++attempt) {
        acquireResult = impl_->acquireNextImage(
            impl_->device,
            impl_->swapchain,
            std::numeric_limits<std::uint64_t>::max(),
            impl_->acquireSemaphores[frameSlot],
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

    if (!impl_->recordFrame(impl_->swapchainFramebuffers[imageIndex], impl_->swapchainExtent.width, impl_->swapchainExtent.height)) {
        return false;
    }

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    if (vkResetFences(impl_->device, 1, &impl_->frameFences[frameSlot]) != VK_SUCCESS) {
        impl_->lastError = "vkResetFences failed";
        return false;
    }
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &impl_->acquireSemaphores[frameSlot];
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &impl_->commandBuffer;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &impl_->renderFinishedSemaphores[frameSlot];
    if (vkQueueSubmit(impl_->queue, 1, &submitInfo, impl_->frameFences[frameSlot]) != VK_SUCCESS) {
        impl_->lastError = "vkQueueSubmit failed";
        return false;
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &impl_->renderFinishedSemaphores[frameSlot];
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
        impl_->destroyTextureResource(texture);
        impl_->setError("Texture handle allocation failed");
        return invalidTextureHandle;
    }
    try {
        const auto result = impl_->textures.emplace(id, std::move(texture));
        if (!result.second) {
            impl_->destroyTextureResource(texture);
            impl_->setError("Texture handle allocation failed");
            return invalidTextureHandle;
        }
    } catch (...) {
        const auto textureIt = impl_->textures.find(id);
        if (textureIt != impl_->textures.end()) {
            impl_->destroyTextureResource(textureIt->second);
            impl_->textures.erase(textureIt);
        } else {
            impl_->destroyTextureResource(texture);
        }
        impl_->setError("Texture resource allocation failed");
        return invalidTextureHandle;
    }

    const TextureHandle handle{id};
    if (!impl_->bindTextureDescriptor(handle)) {
        const auto textureIt = impl_->textures.find(id);
        if (textureIt != impl_->textures.end()) {
            impl_->destroyTextureResource(textureIt->second);
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
    impl_->destroyTextureResource(textureIt->second);
    impl_->textures.erase(textureIt);
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
        impl_->destroyMeshResource(mesh);
        return invalidMeshHandle;
    }
    if (!impl_->createBufferWithData(
            indexSize,
            data.indices.data(),
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            mesh.indexBuffer,
            mesh.indexMemory)) {
        impl_->destroyMeshResource(mesh);
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
    impl_->destroyMeshResource(meshIt->second);
    impl_->meshes.erase(meshIt);
}

std::uint64_t Graphics::meshVertexCount(MeshHandle handle) const noexcept {
    if (!impl_ || !handle.valid()) {
        return 0;
    }
    const auto meshIt = impl_->meshes.find(handle.value());
    return meshIt == impl_->meshes.end() ? 0 : meshIt->second.vertexCount;
}

bool Graphics::render(const CameraState& camera, const std::vector<RenderItem>& items) {
    if (!impl_) {
        return false;
    }
    if (!initialized()) {
        impl_->setError("Graphics is not initialized");
        return false;
    }
    impl_->pendingCamera = camera;
    Impl::multiplyMat4(camera.projectionMatrix, camera.viewMatrix, impl_->pendingViewProj);
    impl_->pendingItems = items;
    impl_->drawDebugTriangle = items.empty();
    engineLog(
        "render(): native=%d items=%zu meshes=%zu",
        impl_->headless ? 0 : 1,
        items.size(),
        impl_->meshes.size());
    return renderFrame();
}

bool Graphics::readbackPixels(std::vector<std::uint8_t>& pixels) const {
    if (!impl_ || !initialized()) {
        return false;
    }
    if (!impl_->headless || impl_->colorImage == VK_NULL_HANDLE) {
        return false;
    }

    constexpr VkDeviceSize byteCount =
        static_cast<VkDeviceSize>(renderWidth) * static_cast<VkDeviceSize>(renderHeight) * 4;
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
    region.imageExtent = {renderWidth, renderHeight, 1};
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
