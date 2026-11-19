#include "Winter.hpp"

namespace Aporia::graphics_internal {

const SeasonPreset& winterPreset() noexcept {
    static const SeasonPreset preset{
        .season = Season::Winter,
        .skyZenithTint = {0.88f, 0.95f, 1.08f},
        .skyHorizonTint = {0.94f, 0.98f, 1.05f},
        .skyTintStrength = 0.65f,
        .sunColor = {0.92f, 0.95f, 1.00f},
        .sunIntensityScale = 0.72f,
        .ambient = 0.26f,
        .weather = {
            .precipitation = PrecipitationKind::Snow,
            .intensity = 0.50f,
            .wind = {-2.5f, 1.5f, 0.0f},
            .fogDensity = 0.0045f,
            .fogColor = {0.72f, 0.76f, 0.82f},
            .fogHeightFalloff = 0.04f,
            .wetness = 0.10f,
            .snowCover = 0.60f,
            .turbulence = 0.15f,
        },
    };
    return preset;
}

} // namespace Aporia::graphics_internal
