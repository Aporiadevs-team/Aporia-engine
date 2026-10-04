#pragma once

#if defined(_WIN32) && !defined(VK_USE_PLATFORM_WIN32_KHR)
#define VK_USE_PLATFORM_WIN32_KHR
#endif

#include <vulkan/vulkan.h>

#include <cstdint>
#include <cstddef>

namespace Aporia::graphics_internal {

// Owns buffer allocation/upload policy. Other renderers ask this service for
// device-local vertex/index/uniform buffers instead of talking to Vulkan
// memory directly.
class MemoryManager {
public:
    struct Context {
        VkDevice device = VK_NULL_HANDLE;
        VkPhysicalDeviceMemoryProperties memoryProperties{};
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkQueue queue = VK_NULL_HANDLE;
    };

    // Uploads data through a staging buffer into a device-local buffer and
    // leaves it ready for the given usage (VERTEX/INDEX/UNIFORM/...).
    static bool uploadBuffer(
        const Context& context,
        VkDeviceSize size,
        const void* data,
        VkBufferUsageFlags usage,
        VkBuffer& buffer,
        VkDeviceMemory& memory) noexcept;

    // Frees a buffer/memory pair (safe on null handles).
    static void destroyBuffer(VkDevice device, VkBuffer& buffer, VkDeviceMemory& memory) noexcept;
};

} // namespace Aporia::graphics_internal
