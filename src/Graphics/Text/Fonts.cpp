#include "Graphics/GraphicsImpl.hpp"

#include <cstdio>
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
    const std::uint64_t id = impl_->directX->createFontFromTtf(ttfPath, pixelSize, error);
    if (id == 0) {
        impl_->setError(error.empty() ? "Direct3D font creation failed" : error);
        return invalidFontHandle;
    }
    return FontHandle(id);
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
    const std::uint64_t id = impl_->directX->createFontFromMsdf(jsonPath, pngPath, error);
    if (id == 0) {
        impl_->setError(error.empty() ? "Direct3D font creation failed" : error);
        return invalidFontHandle;
    }
    return FontHandle(id);
}

void Graphics::destroyFont(FontHandle handle) noexcept {
    if (!impl_ || !handle.valid()) {
        return;
    }
    if (impl_->directX) {
        impl_->directX->destroyFont(handle.value());
    }
}

bool Graphics::fontValid(FontHandle handle) const noexcept {
    if (!impl_ || !impl_->directX) {
        return false;
    }
    return impl_->directX->fontValid(handle.value());
}

float Graphics::measureText(FontHandle handle, std::string_view text, float size) const {
    if (impl_ == nullptr || !impl_->directX || !handle.valid()) {
        return 0.0f;
    }
    return impl_->directX->measureText(handle.value(), text, size);
}

} // namespace Aporia
