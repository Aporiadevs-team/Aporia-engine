#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "TimeManager.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Aporia::graphics_internal {

namespace {
double wrapHours(double hours) noexcept {
    hours = std::fmod(hours, 24.0);
    if (hours < 0.0) {
        hours += 24.0;
    }
    return hours;
}
} // namespace

void TimeManager::syncWithComputerTime() noexcept {
#if defined(_WIN32)
    SYSTEMTIME local{};
    GetLocalTime(&local);
    hours_ = wrapHours(static_cast<double>(local.wHour) +
                       static_cast<double>(local.wMinute) / 60.0 +
                       static_cast<double>(local.wSecond) / 3600.0 +
                       static_cast<double>(local.wMilliseconds) / 3600000.0);
    setDate(local.wMonth, local.wDay);
#endif
}

void TimeManager::syncWithHours(int utcOffsetHours, int minutes, int seconds) noexcept {
#if defined(_WIN32)
    SYSTEMTIME utc{};
    GetSystemTime(&utc);
    const double base = static_cast<double>(utc.wHour) +
                        static_cast<double>(utc.wMinute) / 60.0 +
                        static_cast<double>(utc.wSecond) / 3600.0;
    hours_ = wrapHours(base + static_cast<double>(utcOffsetHours) +
                       static_cast<double>(minutes) / 60.0 +
                       static_cast<double>(seconds) / 3600.0);
    setDate(utc.wMonth, utc.wDay);
#else
    hours_ = wrapHours(static_cast<double>(utcOffsetHours) +
                       static_cast<double>(minutes) / 60.0 +
                       static_cast<double>(seconds) / 3600.0);
#endif
}

void TimeManager::clocksMinusFromRealTime(float realMinutesPerDay, float speed) noexcept {
    const double dayMinutes = realMinutesPerDay > 0.0f ? static_cast<double>(realMinutesPerDay) : 1440.0;
    const double multiplier = speed > 0.0f ? static_cast<double>(speed) : 1.0;
    // 1440 game minutes per day spread over `dayMinutes` real minutes.
    timeScale_ = (1440.0 / dayMinutes) * multiplier;
}

void TimeManager::setTimeOfDay(double hours) noexcept {
    hours_ = wrapHours(hours);
}

void TimeManager::update(double realDeltaSeconds) noexcept {
    if (realDeltaSeconds <= 0.0) {
        return;
    }
    // timeScale_ is game minutes per real minute: real seconds -> real minutes
    // (/60), then game minutes -> hours (/60), so the divisor is 3600.
    hours_ = wrapHours(hours_ + realDeltaSeconds * timeScale_ / 3600.0);
}

double TimeManager::totalHours() const noexcept {
    return hours_;
}

float TimeManager::dayFraction() const noexcept {
    return static_cast<float>(hours_ / 24.0);
}

std::uint32_t TimeManager::hour() const noexcept {
    return static_cast<std::uint32_t>(std::floor(hours_));
}

std::uint32_t TimeManager::minute() const noexcept {
    return static_cast<std::uint32_t>(std::floor((hours_ - std::floor(hours_)) * 60.0));
}

std::uint32_t TimeManager::second() const noexcept {
    const double hoursFraction = hours_ - std::floor(hours_);
    const double minutesFraction = hoursFraction * 60.0 - std::floor(hoursFraction * 60.0);
    return static_cast<std::uint32_t>(std::floor(minutesFraction * 60.0));
}

namespace {
constexpr double kPi = 3.14159265358979323846;

// Rough northern-latitude seasonal table: sunrise / sunset hours per month.
// Winter (Dec/Jan) is short and dark, summer (Jun/Jul) is near white nights.
constexpr double kSunrise[12] = {9.8, 9.0, 7.6, 6.1, 4.5, 3.8, 3.9, 4.6, 5.9, 7.4, 8.9, 9.7};
constexpr double kSunset[12] = {16.9, 17.1, 18.0, 19.6, 21.6, 23.6, 23.7, 22.3, 20.2, 18.4, 17.2, 16.7};
constexpr int kDaysInMonth[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
} // namespace

void TimeManager::setDate(int month, int day) noexcept {
    month_ = month < 1 ? 1 : (month > 12 ? 12 : month);
    const int maxDay = kDaysInMonth[month_ - 1];
    day_ = day < 1 ? 1 : (day > maxDay ? maxDay : day);
}

namespace {
double interpolateMonth(const double table[12], int month, int day) noexcept {
    const double m = static_cast<double>(month - 1) +
                     static_cast<double>(day - 1) / static_cast<double>(kDaysInMonth[month - 1]);
    const int i0 = static_cast<int>(std::floor(m)) % 12;
    const int i1 = (i0 + 1) % 12;
    const double f = m - std::floor(m);
    return table[i0] * (1.0 - f) + table[i1] * f;
}
} // namespace

double TimeManager::sunriseHour() const noexcept {
    const double raw = interpolateMonth(kSunrise, month_, day_);
    return std::max(0.0, raw - dayLengthBiasHours_ * 0.5);
}

double TimeManager::sunsetHour() const noexcept {
    const double raw = interpolateMonth(kSunset, month_, day_);
    return std::min(24.0, raw + dayLengthBiasHours_ * 0.5);
}

double TimeManager::daylightLength() const noexcept {
    return sunsetHour() - sunriseHour();
}

void TimeManager::sunDirection(float out[3], double azimuthOffsetRadians) const noexcept {
    const double sunrise = sunriseHour();
    const double sunset = sunsetHour();
    // Peak solar altitude follows the day length: a short winter day keeps the
    // noon sun low (long shadows, lit facades) instead of putting it at the
    // zenith, a near-polar summer day lifts it high. Without a latitude input
    // this is the only seasonal signal the clock has.
    const double dayHours = std::max(1.0, sunset - sunrise);
    const double peak = std::clamp(std::pow(dayHours / 24.0, 1.6), 0.12, 0.97);
    double elevation = 0.0;
    double horizontal = 1.0;
    if (hours_ >= sunrise && hours_ <= sunset) {
        const double u = (hours_ - sunrise) / (sunset - sunrise);
        const double sine = std::sin(kPi * u);   // 0 at the ends, +1 at solar noon
        const double cosine = std::cos(kPi * u); // +1 east at sunrise, -1 west at sunset
        elevation = peak * sine;
        horizontal = std::sqrt(std::max(0.0, 1.0 - elevation * elevation)) *
                     (cosine >= 0.0 ? 1.0 : -1.0);
    } else {
        // Night: from sunset round through midnight back to sunrise.
        const double nightLength = 24.0 - (sunset - sunrise);
        const double w = hours_ > sunset ? (hours_ - sunset) / nightLength
                                         : (hours_ + 24.0 - sunset) / nightLength;
        elevation = -std::sin(kPi * w);  // negative, -1 at midnight
        horizontal = -std::cos(kPi * w);
    }
    out[0] = static_cast<float>(horizontal * std::cos(azimuthOffsetRadians));
    out[1] = static_cast<float>(horizontal * std::sin(azimuthOffsetRadians));
    out[2] = static_cast<float>(elevation);
}

std::string TimeManager::clockString() const noexcept {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%02u:%02u:%02u", hour(), minute(), second());
    return std::string(buffer);
}

} // namespace Aporia::graphics_internal
