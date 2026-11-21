#include "LightStore.hpp"

#include <algorithm>
#include <cmath>

namespace Aporia::graphics_internal {

void LightStore::setLights(const std::vector<LightDesc>& lights) {
    count_ = static_cast<std::uint32_t>(std::min<std::size_t>(lights.size(), kMaxLights));
    for (std::uint32_t index = 0; index < count_; ++index) {
        const LightDesc& light = lights[index];
        position[index][0] = light.position[0];
        position[index][1] = light.position[1];
        position[index][2] = light.position[2];
        position[index][3] = static_cast<float>(light.type);

        direction[index][0] = light.direction[0];
        direction[index][1] = light.direction[1];
        direction[index][2] = light.direction[2];
        direction[index][3] = 0.0f;

        color[index][0] = light.color[0];
        color[index][1] = light.color[1];
        color[index][2] = light.color[2];
        color[index][3] = light.intensity;

        params[index][0] = std::max(light.radius, 0.001f);
        params[index][1] = std::max(light.softness, 0.01f);
        params[index][2] = std::cos(std::min(light.innerAngle, light.outerAngle));
        params[index][3] = std::cos(std::max(light.innerAngle, light.outerAngle));
    }
}

void LightStore::clear() noexcept {
    count_ = 0;
}

} // namespace Aporia::graphics_internal
