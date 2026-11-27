#include "Graphics/GraphicsImpl.hpp"

#include <utility>

namespace Aporia {

void Graphics::setUiDrawList(UiDrawList drawList) {
    if (!impl_ || !impl_->directX) {
        return;
    }
    impl_->directX->setUiDrawList(std::move(drawList));
}

void Graphics::clearUiDrawList() noexcept {
    if (!impl_ || !impl_->directX) {
        return;
    }
    impl_->directX->clearUiDrawList();
}

} // namespace Aporia
