#include "Graphics/GlobalDrawer/GraphicsImpl.hpp"

namespace Aporia {

void Graphics::setLights(const std::vector<LightDesc>& lights) {
    if (impl_) {
        impl_->lightStore.setLights(lights);
    }
}

void Graphics::clearLights() noexcept {
    if (impl_) {
        impl_->lightStore.clear();
    }
}

std::size_t Graphics::lightCount() const noexcept {
    return impl_ ? impl_->lightStore.count() : 0;
}

} // namespace Aporia
