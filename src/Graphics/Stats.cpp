#include "Graphics/GraphicsImpl.hpp"

#include <cstdint>
#include <string>

namespace Aporia {

GraphicsStats Graphics::stats() const noexcept {
    GraphicsStats result;
    if (!impl_ || !impl_->directX) {
        return result;
    }
    // The Direct3D renderer keeps its own counters; the facade adds the
    // values it owns (the CPU frame time lives here).
    result = impl_->directX->stats();
    result.avgFrameMs = impl_->avgFrameMs;
    result.quality = static_cast<std::uint32_t>(impl_->activeQuality);
    result.shadowMapSize = impl_->shadowMapSize;
    result.textureScalePercent = 100;
    result.antiAliasing = impl_->antiAliasing;
    result.antiAliasingStrength = impl_->antiAliasingStrength;
    result.reflectionsEnabled = impl_->reflectionsEnabled;
    result.hdrEnabled = impl_->hdrEnabled;
    result.validationEnabled = impl_->validationEnabled;
    // Live counters: they change between frames, so they are read from the
    // renderer instead of the end-of-frame snapshot.
    result.textureCount = impl_->directX->textureCount();
    result.boundTextureId = impl_->directX->boundTextureId();
    result.textureUploadCount = impl_->directX->textureUploadCount();
    result.frameCount = impl_->frameCount;
    result.presentCount = impl_->presentCount;
    result.headless = impl_->headless;
    result.nativeWindow = !impl_->headless;
    return result;
}

const std::string& Graphics::lastError() const noexcept {
    static const std::string emptyError;
    return impl_ ? impl_->lastError : emptyError;
}

} // namespace Aporia
