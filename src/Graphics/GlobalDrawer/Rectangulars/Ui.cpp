#include "Graphics/GlobalDrawer/GraphicsImpl.hpp"

#include <utility>

namespace Aporia {

void Graphics::setUiDrawList(UiDrawList drawList) {
    if (impl_) {
        impl_->uiDrawList = std::move(drawList);
    }
}

void Graphics::clearUiDrawList() noexcept {
    if (impl_) {
        impl_->uiDrawList = {};
    }
}

} // namespace Aporia
