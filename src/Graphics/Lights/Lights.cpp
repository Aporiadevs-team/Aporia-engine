#include "Graphics/GraphicsImpl.hpp"

namespace Aporia {

void Graphics::setLights(const std::vector<LightDesc>& lights) {
    if (!impl_ || !impl_->directX) {
        return;
    }
    impl_->directX->setLights(lights);
}

void Graphics::clearLights() noexcept {
    if (!impl_ || !impl_->directX) {
        return;
    }
    impl_->directX->clearLights();
}

std::size_t Graphics::lightCount() const noexcept {
    if (!impl_ || !impl_->directX) {
        return 0;
    }
    return impl_->directX->lightCount();
}

} // namespace Aporia
