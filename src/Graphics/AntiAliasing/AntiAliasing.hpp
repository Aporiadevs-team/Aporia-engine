#pragma once

#include "Aporia/Graphics/Graphics.hpp"

namespace Aporia::graphics_internal {

// Anti-aliasing package: mode + strength policy. The implementations live in
// `Smooth/TAA` (temporal resolve) and `Smooth/SMAA` (shader-side edge resolve).
[[nodiscard]] float clampAntiAliasingStrength(float strength) noexcept;

} // namespace Aporia::graphics_internal
