#include "Graphics/GlobalDrawer/GraphicsImpl.hpp"

#include <string>

namespace Aporia {

#if defined(APORIA_EXPERIMENTAL)
MaterialHandle Graphics::createMaterial(const MaterialDesc& description) {
    if (!impl_ || !initialized()) {
        if (impl_) impl_->setError("Graphics is not initialized");
        return invalidMaterialHandle;
    }
    std::string error;
    const MaterialHandle handle = impl_->materialStore.create(description, impl_->textureStore, error);
    if (!handle.valid()) {
        impl_->setError(error);
    }
    return handle;
}

bool Graphics::updateMaterial(MaterialHandle handle, const MaterialDesc& description) {
    if (!impl_ || !initialized()) {
        return false;
    }
    return impl_->materialStore.update(handle, description, impl_->textureStore);
}

void Graphics::destroyMaterial(MaterialHandle handle) noexcept {
    if (!impl_) {
        return;
    }
    impl_->materialStore.destroy(handle);
}

bool Graphics::materialValid(MaterialHandle handle) const noexcept {
    return impl_ != nullptr && impl_->materialStore.valid(handle);
}
#endif

} // namespace Aporia
