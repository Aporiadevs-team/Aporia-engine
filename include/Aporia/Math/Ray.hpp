#pragma once

#include "Aporia/Math/Vector3.hpp"

#include <cmath>
#include <limits>

namespace Aporia {

struct Ray {
    Vec3 origin{};
    Vec3 direction{0.0, 0.0, 1.0};

    constexpr Ray() noexcept = default;
    constexpr Ray(const Vec3& originValue, const Vec3& directionValue) noexcept
        : origin(originValue), direction(directionValue) {
    }

    [[nodiscard]] Vec3 pointAt(double distance) const noexcept {
        return origin + direction * distance;
    }

    [[nodiscard]] bool valid() const noexcept {
        return std::isfinite(origin.x) && std::isfinite(origin.y) && std::isfinite(origin.z) &&
               std::isfinite(direction.x) && std::isfinite(direction.y) && std::isfinite(direction.z) &&
               direction.lengthSquared() > std::numeric_limits<double>::epsilon();
    }
};

} // namespace Aporia
