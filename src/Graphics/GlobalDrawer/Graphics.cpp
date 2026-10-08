#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "Graphics/GlobalDrawer/GraphicsImpl.hpp"

#include <cstring>
#include <utility>

namespace Aporia {

Graphics::Graphics()
    : impl_(std::make_unique<Impl>()) {
}

Graphics::~Graphics() = default;
Graphics::Graphics(Graphics&&) noexcept = default;

Graphics& Graphics::operator=(Graphics&& other) noexcept {
    if (this != &other) {
        shutdown();
        impl_ = std::move(other.impl_);
    }
    return *this;
}

bool Graphics::initialized() const noexcept {
    return impl_ && impl_->instance != VK_NULL_HANDLE && impl_->device != VK_NULL_HANDLE && impl_->pipelines.pipeline != VK_NULL_HANDLE;
}

void Graphics::waitIdle() noexcept {
    if (impl_ && impl_->device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(impl_->device);
    }
}

bool Graphics::render(const CameraState& camera, const std::vector<RenderItem>& items) {
    return render(camera, EnvironmentState{}, items);
}

bool Graphics::render(const CameraState& camera, const EnvironmentState& environment, const std::vector<RenderItem>& items) {
    if (!impl_) {
        return false;
    }
    if (!initialized()) {
        impl_->setError("Graphics is not initialized");
        return false;
    }
    impl_->pendingCamera = camera;
    impl_->environment = environment;

    float projection[16];
    std::memcpy(projection, camera.projectionMatrix, sizeof(projection));
    const bool taaActive = static_cast<AntiAliasingMode>(impl_->antiAliasing) == AntiAliasingMode::TAA &&
                           impl_->hdrEnabled;
    if (taaActive) {
        // Sub-pixel jitter (Halton 2,3) gives TAA real new samples to accumulate.
        const std::uint32_t targetWidth = impl_->hdrWidth != 0 ? impl_->hdrWidth : impl_->renderWidth;
        const std::uint32_t targetHeight = impl_->hdrHeight != 0 ? impl_->hdrHeight : impl_->renderHeight;
        if (targetWidth > 0 && targetHeight > 0) {
            const double jitterX = Impl::halton(impl_->taaFrameIndex + 1, 2) - 0.5;
            const double jitterY = Impl::halton(impl_->taaFrameIndex + 1, 3) - 0.5;
            projection[8] += static_cast<float>(jitterX * 2.0 / static_cast<double>(targetWidth));
            projection[9] += static_cast<float>(jitterY * 2.0 / static_cast<double>(targetHeight));
        }
        ++impl_->taaFrameIndex;
    }

    std::memcpy(impl_->taaPrevViewProj, impl_->taaViewProj, sizeof(impl_->taaPrevViewProj));
    Impl::multiplyMat4(projection, camera.viewMatrix, impl_->pendingViewProj);
    std::memcpy(impl_->pendingProjection, projection, sizeof(impl_->pendingProjection));
    std::memcpy(impl_->taaViewProj, impl_->pendingViewProj, sizeof(impl_->taaViewProj));
    Impl::invertMat4(impl_->pendingViewProj, impl_->taaInvViewProj);

    impl_->pendingItems = items;
    impl_->drawDebugTriangle = items.empty();
    impl_->meshStore.resolve(impl_->pendingCamera, impl_->pendingItems);
    return renderFrame();
}

bool Graphics::Impl::invertMat4(const float* m, float* out) noexcept {
    const double inv[16] = {
        m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10],
        -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10],
        m[1]*m[6]*m[15] - m[1]*m[7]*m[14] - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7] - m[13]*m[3]*m[6],
        -m[1]*m[6]*m[11] + m[1]*m[7]*m[10] + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7] + m[9]*m[3]*m[6],
        -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10],
        m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10],
        -m[0]*m[6]*m[15] + m[0]*m[7]*m[14] + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7] + m[12]*m[3]*m[6],
        m[0]*m[6]*m[11] - m[0]*m[7]*m[10] - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7] - m[8]*m[3]*m[6],
        m[4]*m[9]*m[15] - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9],
        -m[0]*m[9]*m[15] + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9],
        m[0]*m[5]*m[15] - m[0]*m[7]*m[13] - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7] - m[12]*m[3]*m[5],
        -m[0]*m[5]*m[11] + m[0]*m[7]*m[9] + m[4]*m[1]*m[11] - m[4]*m[3]*m[9] - m[8]*m[1]*m[7] + m[8]*m[3]*m[5],
        -m[4]*m[9]*m[14] + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9],
        m[0]*m[9]*m[14] - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9],
        -m[0]*m[5]*m[14] + m[0]*m[6]*m[13] + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6] + m[12]*m[2]*m[5],
        m[0]*m[5]*m[10] - m[0]*m[6]*m[9] - m[4]*m[1]*m[10] + m[4]*m[2]*m[9] + m[8]*m[1]*m[6] - m[8]*m[2]*m[5],
    };
    const double determinant = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
    if (std::abs(determinant) < 1.0e-12) {
        return false;
    }
    const double scale = 1.0 / determinant;
    for (int index = 0; index < 16; ++index) {
        out[index] = static_cast<float>(inv[index] * scale);
    }
    return true;
}

Mat4 Graphics::Impl::reflectViewMatrix() const noexcept {
    const double mirrorZ = static_cast<double>(environment.mirrorZ);
    const auto reflect = [mirrorZ](double z) { return 2.0 * mirrorZ - z; };
    const float forwardX = -pendingViewProj[8];
    const float forwardY = -pendingViewProj[9];
    const float forwardZ = -pendingViewProj[10];
    const Vec3 reflectedEye(
        pendingCamera.position[0],
        pendingCamera.position[1],
        reflect(static_cast<double>(pendingCamera.position[2])));
    const Vec3 target(
        pendingCamera.position[0] + forwardX * 100.0,
        pendingCamera.position[1] + forwardY * 100.0,
        reflect(static_cast<double>(pendingCamera.position[2]) + forwardZ * 100.0));
    const Vec3 up(0.0, 0.0, 1.0);
    Mat4 view = Mat4::lookAt(reflectedEye, target, up);
    // Mirrored view flips handedness; negate the x basis column.
    view.m[0][0] = -view.m[0][0];
    view.m[1][0] = -view.m[1][0];
    view.m[2][0] = -view.m[2][0];
    view.m[3][0] = -view.m[3][0];
    return view;
}

} // namespace Aporia
