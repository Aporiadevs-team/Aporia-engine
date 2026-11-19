#pragma once

#include "Aporia/Graphics/Graphics.hpp"

#include <cstdint>

namespace Aporia::graphics_internal {

// Quality preset parameter table: one place scales the whole picture.
struct QualityParameters {
    std::uint32_t shadowMapSize;
    int shadowTaps;          // specialization constant: 1, 9, 49
    bool hdr;
    bool reflections;
    float reflectionScale;   // reflection target relative to main target
    float shadowStrength;    // ambient floor inside shadows
    std::uint32_t textureScalePercent;
    std::uint32_t maxLights; // dynamic lights that reach the shader (0/8/16)
};

[[nodiscard]] QualityParameters qualityParameters(Aporia::GraphicsQuality quality) noexcept;

} // namespace Aporia::graphics_internal
