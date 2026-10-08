#include "Graphics/GlobalDrawer/GraphicsImpl.hpp"

#include <string>
#include <vector>

namespace Aporia {

MeshHandle Graphics::createLodMesh(
    const std::vector<MeshData>& levels,
    const std::vector<float>& maxDistances,
    LodBoundary boundary) {
    if (!impl_) {
        return invalidMeshHandle;
    }
    if (!initialized()) {
        impl_->setError("Graphics is not initialized");
        return invalidMeshHandle;
    }
    std::string error;
    const MeshHandle handle = impl_->meshStore.createLod(levels, maxDistances, boundary, error);
    if (!handle.valid()) {
        impl_->setError(error);
    }
    return handle;
}

void Graphics::destroyLodMesh(MeshHandle handle) noexcept {
    if (!impl_) {
        return;
    }
    impl_->meshStore.destroyLod(handle);
}

bool Graphics::lodMeshValid(MeshHandle handle) const noexcept {
    return impl_ != nullptr && impl_->meshStore.lodValid(handle);
}

std::uint32_t Graphics::lodLevelCount(MeshHandle handle) const noexcept {
    if (!impl_) {
        return 0;
    }
    return impl_->meshStore.lodLevelCount(handle);
}

} // namespace Aporia
