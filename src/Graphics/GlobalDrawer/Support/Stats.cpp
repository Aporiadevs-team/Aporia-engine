#include "Graphics/GlobalDrawer/GraphicsImpl.hpp"

#include <cstdint>
#include <string>

namespace Aporia {

GraphicsStats Graphics::stats() const noexcept {
    GraphicsStats result;
    if (!impl_) {
        return result;
    }
    result.frameCount = impl_->frameCount;
    result.presentCount = impl_->presentCount;
    result.textureCount = static_cast<std::uint64_t>(impl_->textureStore.count());
    result.boundTextureId = impl_->textureStore.boundHandle().value();
    result.textureUploadCount = impl_->textureStore.uploadCount();
    result.lodGroupCount = static_cast<std::uint64_t>(impl_->meshStore.lodGroupCount());
    result.lodDrawTriangles = impl_->meshStore.lodDrawTriangles();
    result.lodFullTriangles = impl_->meshStore.lodFullTriangles();
    result.lodSwitchCount = impl_->meshStore.lodSwitchCount();
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
    result.textureScalePercent = impl_->textureStore.textureScalePercent();
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

} // namespace Aporia
