#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "Graphics/GraphicsImpl.hpp"

#include <algorithm>
#include <cstdint>
#include <string>

namespace Aporia {

bool Graphics::renderFrame() noexcept {
    if (!impl_ || !initialized()) {
        if (impl_) {
            impl_->lastError = "Graphics is not initialized";
        }
        return false;
    }

    LARGE_INTEGER startCounter{};
    LARGE_INTEGER endCounter{};
    LARGE_INTEGER frequency{};
    const bool timing = QueryPerformanceCounter(&startCounter) != 0 &&
                        QueryPerformanceFrequency(&frequency) != 0 && frequency.QuadPart > 0;

    // The time-driven sun overrides the host environment before the frame is
    // recorded: sunrise east, noon zenith, sunset west.
    Aporia::EnvironmentState environment = impl_->environment;
    if (impl_->timeDrivenSun) {
        float sunDirection[3] = {0.0f, 0.0f, 1.0f};
        impl_->timeManager.sunDirection(sunDirection);
        environment.sunDirection[0] = sunDirection[0];
        environment.sunDirection[1] = sunDirection[1];
        environment.sunDirection[2] = sunDirection[2];
        // Daylight ramps smoothly with the sun elevation so the intensity never
        // snaps: dim at the horizon, full by ~15 degrees, dark below.
        const float elevation = sunDirection[2];
        float t = (elevation + 0.05f) / 0.30f;
        t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        environment.sunIntensity = t * t * (3.0f - 2.0f * t);
        // The sky darkens at night, so the hemisphere ambient must follow it:
        // otherwise a sunless scene stays lit like an overcast afternoon.
        impl_->nightAmbientScale = 0.08f + 0.92f * environment.sunIntensity;
    }
    environment.ambient = impl_->effectiveAmbient() * impl_->nightAmbientScale;

    const bool submitted = impl_->directX->renderFrame(
        impl_->pendingCamera, environment, impl_->pendingItems);
    if (!submitted) {
        const std::string& error = impl_->directX->lastError();
        if (!error.empty()) {
            impl_->lastError = error;
        }
    }

    double frameSeconds = 1.0 / 60.0;
    if (timing && QueryPerformanceCounter(&endCounter) != 0) {
        const double milliseconds = static_cast<double>(endCounter.QuadPart - startCounter.QuadPart) *
                                    1000.0 / static_cast<double>(frequency.QuadPart);
        constexpr double smoothing = 0.9;
        impl_->avgFrameMs = impl_->avgFrameMs <= 0.0
            ? milliseconds
            : impl_->avgFrameMs * smoothing + milliseconds * (1.0 - smoothing);

        // The simulation clock follows the real time between two frames. The
        // render duration above is a cost metric only: feeding it to the clock
        // made the sun run slower than real time and jump with the frame load.
        if (impl_->hasLastFrameCounter) {
            const double sinceLastSeconds =
                static_cast<double>(startCounter.QuadPart - impl_->lastFrameCounter) /
                static_cast<double>(frequency.QuadPart);
            // A host that was paused (debugger, window drag) must not fast
            // forward the whole day at once.
            frameSeconds = std::clamp(sinceLastSeconds, 0.0, 0.25);
        } else {
            frameSeconds = milliseconds / 1000.0;
        }
        impl_->lastFrameCounter = startCounter.QuadPart;
        impl_->hasLastFrameCounter = true;
    }
    impl_->frameDeltaSeconds = static_cast<float>(frameSeconds);
    impl_->timeManager.update(frameSeconds);
    ++impl_->frameCount;
    if (!impl_->headless) {
        ++impl_->presentCount;
    }
    return submitted;
}

bool Graphics::renderHeadlessFrame() noexcept {
    if (!impl_ || !initialized()) {
        if (impl_) {
            impl_->lastError = "Graphics is not initialized";
        }
        return false;
    }
    // One renderer, one frame path: the Direct3D backend presents to the
    // swapchain (native) or to the offscreen target (headless).
    return renderFrame();
}

bool Graphics::renderNativeFrame() noexcept {
    return renderFrame();
}

void Graphics::setVsync(bool enabled) noexcept {
    if (impl_ && impl_->directX) {
        impl_->directX->setVsync(enabled);
    }
}

bool Graphics::vsync() const noexcept {
    if (!impl_ || !impl_->directX) {
        return false;
    }
    return impl_->directX->vsync();
}

#if defined(APORIA_EXPERIMENTAL)
void Graphics::setMultithreadedRender(bool enabled) noexcept {
    if (impl_) {
        impl_->multithreadedRender = enabled;
    }
}

bool Graphics::multithreadedRender() const noexcept {
    return impl_ != nullptr && impl_->multithreadedRender;
}
#endif

} // namespace Aporia
