#include "SeasonPreset.hpp"

#include "autumn/Autumn.hpp"
#include "spring/Spring.hpp"
#include "summer/Summer.hpp"
#include "winter/Winter.hpp"

namespace Aporia::graphics_internal {

const SeasonPreset& seasonPresetData(Season season) noexcept {
    switch (season) {
    case Season::Spring:
        return springPreset();
    case Season::Autumn:
        return autumnPreset();
    case Season::Winter:
        return winterPreset();
    case Season::Summer:
        break;
    }
    return summerPreset();
}

} // namespace Aporia::graphics_internal

#if defined(APORIA_SEASONS)
namespace Aporia {

SeasonPreset seasonPreset(Season season) noexcept {
    return graphics_internal::seasonPresetData(season);
}

} // namespace Aporia
#endif
