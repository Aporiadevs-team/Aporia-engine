#include "Graphics/GlobalDrawer/GraphicsImpl.hpp"

namespace Aporia {

void Graphics::setTimeOfDay(double hours) noexcept {
    if (impl_) {
        impl_->timeManager.setTimeOfDay(hours);
    }
}

double Graphics::timeOfDay() const noexcept {
    return impl_ ? impl_->timeManager.totalHours() : 0.0;
}

void Graphics::syncTimeWithComputer() noexcept {
    if (impl_) {
        impl_->timeManager.syncWithComputerTime();
    }
}

void Graphics::syncTimeWithHours(int utcOffsetHours, int minutes, int seconds) noexcept {
    if (impl_) {
        impl_->timeManager.syncWithHours(utcOffsetHours, minutes, seconds);
    }
}

void Graphics::setGameClock(float realMinutesPerDay, float speed) noexcept {
    if (impl_) {
        impl_->timeManager.clocksMinusFromRealTime(realMinutesPerDay, speed);
    }
}

float Graphics::gameClockScale() const noexcept {
    return impl_ ? impl_->timeManager.timeScale() : 1.0f;
}

void Graphics::setTimeDrivenSun(bool enabled) noexcept {
    if (impl_) {
        impl_->timeDrivenSun = enabled;
    }
}

bool Graphics::timeDrivenSun() const noexcept {
    return impl_ != nullptr && impl_->timeDrivenSun;
}

void Graphics::setDate(int month, int day) noexcept {
    if (impl_) {
        impl_->timeManager.setDate(month, day);
    }
}

double Graphics::sunriseHour() const noexcept {
    return impl_ ? impl_->timeManager.sunriseHour() : 0.0;
}

double Graphics::sunsetHour() const noexcept {
    return impl_ ? impl_->timeManager.sunsetHour() : 0.0;
}

void Graphics::setDayLengthBias(double hours) noexcept {
    if (impl_) {
        impl_->timeManager.setDayLengthBias(hours);
    }
}

double Graphics::dayLengthBias() const noexcept {
    return impl_ ? impl_->timeManager.dayLengthBias() : 0.0;
}

} // namespace Aporia
