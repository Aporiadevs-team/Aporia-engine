#include "Graphics/GraphicsImpl.hpp"

namespace Aporia {

MeshHandle Graphics::createMesh(const MeshData& data) {
    if (!impl_) {
        return invalidMeshHandle;
    }
    if (!initialized()) {
        impl_->setError("Graphics is not initialized");
        return invalidMeshHandle;
    }
    const std::uint64_t id = impl_->directX->createMesh(data);
    if (id == 0) {
        impl_->setError("Direct3D mesh creation failed");
        return invalidMeshHandle;
    }
    return MeshHandle(id);
}

void Graphics::destroyMesh(MeshHandle handle) noexcept {
    if (!impl_ || !handle.valid()) {
        return;
    }
    if (!initialized()) {
        impl_->lastError = "Graphics is not initialized";
        return;
    }
    impl_->directX->destroyMesh(handle.value());
}

std::uint64_t Graphics::meshVertexCount(MeshHandle handle) const noexcept {
    if (!impl_ || !impl_->directX) {
        return 0;
    }
    return impl_->directX->meshVertexCount(handle.value());
}

} // namespace Aporia
