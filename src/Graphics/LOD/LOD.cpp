#include "Graphics/GraphicsImpl.hpp"

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
    if (!impl_ || !impl_->directX) {
        return invalidMeshHandle;
    }
    const std::uint64_t id = impl_->directX->createLodMesh(levels, maxDistances, boundary);
    if (id == 0) {
        impl_->setError("Direct3D LOD group creation failed");
        return invalidMeshHandle;
    }
    return MeshHandle(id);
}

void Graphics::destroyLodMesh(MeshHandle handle) noexcept {
    if (!impl_ || !impl_->directX) {
        return;
    }
    impl_->directX->destroyLodMesh(handle.value());
}

bool Graphics::lodMeshValid(MeshHandle handle) const noexcept {
    if (!impl_ || !impl_->directX) {
        return false;
    }
    return impl_->directX->lodMeshValid(handle.value());
}

std::uint32_t Graphics::lodLevelCount(MeshHandle handle) const noexcept {
    if (!impl_ || !impl_->directX) {
        return 0;
    }
    return impl_->directX->lodLevelCount(handle.value());
}

} // namespace Aporia
