#include "Graphics/GraphicsImpl.hpp"

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
    const std::uint64_t id = impl_->directX->createTexture(description);
    if (id == 0) {
        impl_->setError("Direct3D texture creation failed");
        return invalidTextureHandle;
    }
    return TextureHandle(id);
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
    if (!impl_->directX->updateTexture(handle.value(), pixels, dataSize)) {
        impl_->setError("Direct3D texture update failed");
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
    if (!impl_->directX->setFrameTexture(handle.value())) {
        impl_->lastError = "Direct3D texture bind failed";
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
    impl_->directX->destroyTexture(handle.value());
}

} // namespace Aporia
