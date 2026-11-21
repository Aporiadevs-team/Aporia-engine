#pragma once

#include <cstdint>
#include <string>

namespace Aporia::graphics_internal {

// Time-of-day manager. Keeps a wall clock in [0, 24h) and maps it to the game
// clock, so the renderer can drive sunsets etc. Sync sources are optional:
//   - syncWithComputerTime(): take the local OS clock, second-accurate (Windows).
//   - syncWithHours(...):     take real-world UTC + a time-zone offset.
//   - clocksMinusFromRealTime(): how game time advances versus real time.
class TimeManager {
public:
    // Seconds-accurate sync to the local computer clock. Windows only; on other
    // platforms it is a no-op.
    void syncWithComputerTime() noexcept;

    // Syncs to real UTC plus a time-zone offset (e.g. UTC+3 -> hours = 3).
    // On non-Windows it just sets the given wall time.
    void syncWithHours(int utcOffsetHours, int minutes = 0, int seconds = 0) noexcept;

    // Maps reality to the game clock:
    //   realMinutesPerDay - how many REAL minutes one full in-game day lasts
    //                       (1440 = real time).
    //   speed             - extra multiplier on top (1 = none).
    // Defaults keep 1 real minute == 1 game minute (1440 / 1).
    void clocksMinusFromRealTime(float realMinutesPerDay = 1440.0f, float speed = 1.0f) noexcept;

    // Sets the wall clock directly (hours wrapped into [0, 24)).
    void setTimeOfDay(double hours) noexcept;

    // Advances the clock by a real-time delta (seconds). Call once per frame.
    void update(double realDeltaSeconds) noexcept;

    [[nodiscard]] double totalHours() const noexcept;   // wrapped [0, 24)
    [[nodiscard]] float dayFraction() const noexcept;   // [0, 1)
    [[nodiscard]] std::uint32_t hour() const noexcept;
    [[nodiscard]] std::uint32_t minute() const noexcept;
    [[nodiscard]] std::uint32_t second() const noexcept;
    // Game minutes advanced per one real minute (1 = real time).
    [[nodiscard]] float timeScale() const noexcept { return static_cast<float>(timeScale_); }
    [[nodiscard]] std::string clockString() const noexcept; // "HH:MM:SS"

    // Sets the calendar date (season drives sunrise/sunset).
    void setDate(int month, int day) noexcept;
    // Manual day-length trim in hours: drifts sunrise earlier and sunset later
    // (negative shortens the day). Applied on top of the season.
    void setDayLengthBias(double hours) noexcept { dayLengthBiasHours_ = hours; }
    [[nodiscard]] double dayLengthBias() const noexcept { return dayLengthBiasHours_; }

    // Seasonal sunrise / sunset hours for the current date (interpolated).
    [[nodiscard]] double sunriseHour() const noexcept;
    [[nodiscard]] double sunsetHour() const noexcept;
    // Length of the day in hours (sunset - sunrise).
    [[nodiscard]] double daylightLength() const noexcept;

    // Sun direction (Z-up, unit) for the current time and date: rises in the
    // east at sunrise, peaks at solar noon and sets in the west. Below the
    // horizon at night (z < 0). `azimuthOffsetRadians` rotates the east-west arc.
    void sunDirection(float out[3], double azimuthOffsetRadians = 0.0) const noexcept;

private:
    double hours_ = 12.0;   // wall clock, [0, 24)
    double timeScale_ = 1.0; // game minutes per real minute
    int month_ = 6;          // 1..12, season for sunrise/sunset
    int day_ = 21;           // 1..31
    double dayLengthBiasHours_ = 0.0; // manual day-length trim
};

} // namespace Aporia::graphics_internal
