#include "Graphics/GlobalDrawer/GraphicsImpl.hpp"

#include <string>

namespace Aporia {

TextureHandle Graphics::createTexture2D(const TextureDesc& description) {
    if (!impl_) {
        return invalidTextureHandle;
    }
    if (!initialized()) {
        impl_->setError("Graphics is not initialized");
        return invalidTextureHandle;
    }
    std::string error;
    const TextureHandle handle = impl_->textureStore.create(description, impl_->scene, error);
    if (!handle.valid()) {
        impl_->setError(error);
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
    std::string error;
    if (!impl_->textureStore.update(handle, pixels, dataSize, error)) {
        impl_->setError(error);
        return false;
    }
    return true;
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
    std::string error;
    if (!impl_->textureStore.bind(handle, impl_->scene, error)) {
        impl_->lastError = error;
        return false;
    }
    return true;
}

void Graphics::destroyTexture2D(TextureHandle handle) noexcept {
    if (!impl_ || !handle.valid()) {
        return;
    }
    if (!initialized()) {
        impl_->lastError = "Graphics is not initialized";
        return;
    }
    std::string error;
    impl_->textureStore.destroy(handle, impl_->scene, error);
    if (!error.empty()) {
        impl_->lastError = error;
    }
}

} // namespace Aporia
