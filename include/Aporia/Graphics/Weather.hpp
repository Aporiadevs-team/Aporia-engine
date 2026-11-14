#pragma once

#include "Aporia/Core/Export.hpp"

#include <cstdint>

namespace Aporia {

enum class PrecipitationKind : std::uint8_t {
    None = 0,
    Rain = 1,
    Snow = 2,
    Sleet = 3,
};

// Hard cap on the particle budget the precipitation batch accepts.
inline constexpr std::uint32_t kWeatherMaxParticles = 5000;

// Per-frame weather. A default-constructed state is clear: no precipitation and
// zero fog density, which leaves every frame identical to a weatherless one.
// The engine consumes fog and wind immediately; wetness and snowCover are
// carried for host-side shading decisions.
struct WeatherState {
    PrecipitationKind precipitation = PrecipitationKind::None;
    float intensity = 0.0f;              // 0..1 precipitation strength
    float wind[3] = {0.0f, 0.0f, 0.0f};  // m/s, Z-up world (x/y horizontal drift)
    float fogDensity = 0.0f;             // 1/m exponential extinction (0 = off)
    float fogColor[3] = {0.72f, 0.76f, 0.82f};
    float fogHeightFalloff = 0.0f;       // 1/m, thins fog with height (0 = uniform)
    float wetness = 0.0f;                // 0..1 surface wetness hint
    float snowCover = 0.0f;              // 0..1 snow accumulation hint
    float turbulence = 0.0f;             // 0..1 storm strength (drift + streak shear)
    std::uint32_t particleCount = 2500;  // budget before intensity, capped at 5000
    float fallSpeed = 0.0f;              // m/s along -Z, 0 = per-kind default
};

enum class Season : std::uint8_t {
    Spring = 0,
    Summer = 1,
    Autumn = 2,
    Winter = 3,
};

// Data-only atmosphere preset. Colours are linear multipliers; the tint
// strength blends between the engine's default palette (0) and the tint (1).
// `weather` carries the season's default fog, precipitation and wind; the host
// overrides individual fields with WeatherState after applying a season.
struct SeasonPreset {
    Season season = Season::Summer;
    float skyZenithTint[3] = {1.0f, 1.0f, 1.0f};
    float skyHorizonTint[3] = {1.0f, 1.0f, 1.0f};
    float skyTintStrength = 0.0f;
    float sunColor[3] = {1.0f, 0.94f, 0.82f}; // sky sun disc / glare tint
    float sunIntensityScale = 1.0f;           // multiplies the host sun intensity
    float ambient = 0.32f;                    // absolute ambient floor
    WeatherState weather{};
};

#if defined(APORIA_SEASONS)
// Resolved season preset the host can merge into its own WeatherState.
[[nodiscard]] APORIA_API SeasonPreset seasonPreset(Season season) noexcept;
#endif

} // namespace Aporia
