#include "Graphics/GraphicsImpl.hpp"

#include <string>

namespace Aporia {

#if defined(APORIA_EXPERIMENTAL)
MaterialHandle Graphics::createMaterial(const MaterialDesc& description) {
    if (!impl_ || !initialized()) {
        if (impl_) impl_->setError("Graphics is not initialized");
        return invalidMaterialHandle;
    }
    std::string error;
    const std::uint64_t id = impl_->directX->createMaterial(description);
    if (id == 0) {
        impl_->setError("Direct3D material creation failed");
        return invalidMaterialHandle;
    }
    return MaterialHandle(id);
}

bool Graphics::updateMaterial(MaterialHandle handle, const MaterialDesc& description) {
    if (!impl_ || !initialized()) {
        return false;
    }
    return impl_->directX->updateMaterial(handle.value(), description);
}

void Graphics::destroyMaterial(MaterialHandle handle) noexcept {
    if (!impl_ || !impl_->directX) {
        return;
    }
    impl_->directX->destroyMaterial(handle.value());
}

bool Graphics::materialValid(MaterialHandle handle) const noexcept {
    if (!impl_ || !impl_->directX) {
        return false;
    }
    return impl_->directX->materialValid(handle.value());
}
#endif

} // namespace Aporia
