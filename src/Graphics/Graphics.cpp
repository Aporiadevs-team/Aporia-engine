#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "Graphics/GraphicsImpl.hpp"

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
    return impl_ != nullptr && impl_->directX != nullptr && impl_->directX->initialized();
}

void Graphics::waitIdle() noexcept {
    if (impl_ && impl_->directX) {
        impl_->directX->waitIdle();
    }
}

void Graphics::setWeather(const WeatherState& weather) noexcept {
    if (impl_) {
        impl_->weather = weather;
    }
}

WeatherState Graphics::weather() const noexcept {
    return impl_ ? impl_->weather : WeatherState{};
}

#if defined(APORIA_SEASONS)
void Graphics::setSeason(Season season) noexcept {
    if (!impl_) {
        return;
    }
    impl_->season = season;
    impl_->seasonAtmosphere = seasonPreset(season);
    impl_->seasonActive = true;
    impl_->weather = impl_->seasonAtmosphere.weather;
}

Season Graphics::season() const noexcept {
    return impl_ ? impl_->season : Season::Summer;
}
#endif

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
    // The renderer resolves the LOD levels and the shadow pass itself, so the
    // scene data goes straight through.
    impl_->pendingCamera = camera;
    impl_->environment = environment;
    impl_->pendingItems = items;
    return renderFrame();
}

bool Graphics::readbackPixels(std::vector<std::uint8_t>& pixels) const {
    if (!impl_ || !impl_->directX) {
        return false;
    }
    if (!impl_->directX->readbackPixels(pixels)) {
        impl_->lastError = "The Direct3D backend could not read the rendered target";
        return false;
    }
    return true;
}

} // namespace Aporia
