#pragma once

#include "Aporia/Graphics/Weather.hpp"

namespace Aporia::graphics_internal {

// Season preset data table. Each season owns one .cpp with its literal
// atmosphere values, so tweaking a season never touches engine code.
[[nodiscard]] const SeasonPreset& seasonPresetData(Season season) noexcept;

[[nodiscard]] const SeasonPreset& springPreset() noexcept;
[[nodiscard]] const SeasonPreset& summerPreset() noexcept;
[[nodiscard]] const SeasonPreset& autumnPreset() noexcept;
[[nodiscard]] const SeasonPreset& winterPreset() noexcept;

} // namespace Aporia::graphics_internal
