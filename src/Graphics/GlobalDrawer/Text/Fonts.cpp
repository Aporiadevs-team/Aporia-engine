#include "Graphics/GlobalDrawer/GraphicsImpl.hpp"

#include <string>

namespace Aporia {

FontHandle Graphics::createFontFromTtf(const std::string& ttfPath, float pixelSize) {
    if (!impl_) {
        return invalidFontHandle;
    }
    if (!initialized()) {
        impl_->setError("Graphics is not initialized");
        return invalidFontHandle;
    }
    std::string error;
    const FontHandle handle = impl_->fontStore.createFromTtf(
        ttfPath, pixelSize, impl_->textureStore, impl_->scene, error);
    if (!handle.valid()) {
        impl_->setError(error);
    }
    return handle;
}

FontHandle Graphics::createFontFromMsdf(const std::string& jsonPath, const std::string& pngPath) {
    if (!impl_) {
        return invalidFontHandle;
    }
    if (!initialized()) {
        impl_->setError("Graphics is not initialized");
        return invalidFontHandle;
    }
    std::string error;
    const FontHandle handle = impl_->fontStore.createFromMsdf(
        jsonPath, pngPath, impl_->textureStore, impl_->scene, error);
    if (!handle.valid()) {
        impl_->setError(error);
    }
    return handle;
}

void Graphics::destroyFont(FontHandle handle) noexcept {
    if (!impl_ || !handle.valid()) {
        return;
    }
    impl_->fontStore.destroy(handle, impl_->textureStore, impl_->scene);
}

bool Graphics::fontValid(FontHandle handle) const noexcept {
    return impl_ != nullptr && impl_->fontStore.valid(handle);
}

} // namespace Aporia
