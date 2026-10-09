#pragma once

#include "Aporia/Graphics/Graphics.hpp"

#include <cstdint>
#include <vector>

namespace Aporia::graphics_internal {

// Owns the dynamic light list and packs it into the structure-of-arrays layout
// the mesh shader expects (`lightPosition[i]`, `lightDirection[i]`, ...).
class LightStore {
public:
    static constexpr std::uint32_t kMaxLights = 16;

    void setLights(const std::vector<LightDesc>& lights);
    void clear() noexcept;

    [[nodiscard]] std::uint32_t count() const noexcept { return count_; }
    [[nodiscard]] static constexpr std::uint32_t capacity() noexcept { return kMaxLights; }

    // SoA payload embedded in SceneUniforms (each entry is a vec4).
    float position[kMaxLights][4]{};
    float direction[kMaxLights][4]{};
    float color[kMaxLights][4]{};
    float params[kMaxLights][4]{}; // x radius, y softness, z inner cos, w outer cos

private:
    std::uint32_t count_ = 0;
};

} // namespace Aporia::graphics_internal
