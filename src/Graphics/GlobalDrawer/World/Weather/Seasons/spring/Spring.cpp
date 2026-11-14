#include "Spring.hpp"

namespace Aporia::graphics_internal {

const SeasonPreset& springPreset() noexcept {
    static const SeasonPreset preset{
        .season = Season::Spring,
        .skyZenithTint = {0.95f, 1.00f, 0.97f},
        .skyHorizonTint = {0.98f, 1.00f, 1.03f},
        .skyTintStrength = 0.35f,
        .sunColor = {1.00f, 0.97f, 0.90f},
        .sunIntensityScale = 1.00f,
        .ambient = 0.33f,
        .weather = {
            .precipitation = PrecipitationKind::Rain,
            .intensity = 0.20f,
            .wind = {3.5f, -1.5f, 0.0f},
            .fogDensity = 0.0018f,
            .fogColor = {0.70f, 0.76f, 0.82f},
            .fogHeightFalloff = 0.02f,
            .wetness = 0.25f,
            .snowCover = 0.0f,
            .turbulence = 0.10f,
        },
    };
    return preset;
}

} // namespace Aporia::graphics_internal
