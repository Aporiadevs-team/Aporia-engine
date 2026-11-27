#include "Autumn.hpp"

namespace Aporia::graphics_internal {

const SeasonPreset& autumnPreset() noexcept {
    static const SeasonPreset preset{
        .season = Season::Autumn,
        .skyZenithTint = {0.96f, 0.98f, 1.04f},
        .skyHorizonTint = {1.10f, 0.98f, 0.88f},
        .skyTintStrength = 0.55f,
        .sunColor = {1.00f, 0.88f, 0.70f},
        .sunIntensityScale = 0.90f,
        .ambient = 0.30f,
        .weather = {
            .precipitation = PrecipitationKind::Rain,
            .intensity = 0.35f,
            .wind = {5.0f, 2.0f, 0.0f},
            .fogDensity = 0.0035f,
            .fogColor = {0.62f, 0.63f, 0.67f},
            .fogHeightFalloff = 0.03f,
            .wetness = 0.45f,
            .snowCover = 0.0f,
            .turbulence = 0.25f,
        },
    };
    return preset;
}

} // namespace Aporia::graphics_internal
