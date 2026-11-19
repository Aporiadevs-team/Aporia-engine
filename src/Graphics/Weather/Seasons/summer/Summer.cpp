#include "Summer.hpp"

namespace Aporia::graphics_internal {

const SeasonPreset& summerPreset() noexcept {
    static const SeasonPreset preset{
        .season = Season::Summer,
        .skyZenithTint = {1.04f, 1.02f, 0.98f},
        .skyHorizonTint = {1.06f, 1.02f, 0.95f},
        .skyTintStrength = 0.30f,
        .sunColor = {1.00f, 0.95f, 0.80f},
        .sunIntensityScale = 1.05f,
        .ambient = 0.30f,
        .weather = {
            .precipitation = PrecipitationKind::None,
            .intensity = 0.0f,
            .wind = {2.0f, 0.5f, 0.0f},
            .fogDensity = 0.0006f,
            .fogColor = {0.78f, 0.80f, 0.84f},
            .fogHeightFalloff = 0.0f,
            .wetness = 0.0f,
            .snowCover = 0.0f,
            .turbulence = 0.0f,
        },
    };
    return preset;
}

} // namespace Aporia::graphics_internal
