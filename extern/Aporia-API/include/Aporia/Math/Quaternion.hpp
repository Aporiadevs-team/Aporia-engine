#pragma once

#include "Aporia/Math/Vector3.hpp"

#include <cmath>
#include <limits>

namespace Aporia {

struct Quat {
    double w = 1.0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    constexpr Quat() noexcept = default;
    constexpr Quat(double wValue, double xValue, double yValue, double zValue) noexcept
        : w(wValue), x(xValue), y(yValue), z(zValue) {
    }

    [[nodiscard]] static constexpr Quat identity() noexcept {
        return {};
    }

    [[nodiscard]] static Quat fromAxisAngle(const Vec3& axis, double angle) noexcept {
        if (!std::isfinite(angle) || !std::isfinite(axis.x) || !std::isfinite(axis.y) || !std::isfinite(axis.z)) {
            return identity();
        }

        const double axisLengthSquared = axis.lengthSquared();
        if (!std::isfinite(axisLengthSquared) || axisLengthSquared <= std::numeric_limits<double>::epsilon()) {
            return identity();
        }

        const Vec3 unitAxis = axis * (1.0 / std::sqrt(axisLengthSquared));
        const double halfAngle = angle * 0.5;
        const double sine = std::sin(halfAngle);
        return {
            std::cos(halfAngle),
            unitAxis.x * sine,
            unitAxis.y * sine,
            unitAxis.z * sine,
        };
    }

    [[nodiscard]] bool finite() const noexcept {
        return std::isfinite(w) && std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
    }

    [[nodiscard]] double lengthSquared() const noexcept {
        return w * w + x * x + y * y + z * z;
    }

    [[nodiscard]] Quat normalized() const noexcept {
        const double squaredLength = lengthSquared();
        if (!finite() || !std::isfinite(squaredLength) || squaredLength <= std::numeric_limits<double>::epsilon()) {
            return identity();
        }

        const double inverseLength = 1.0 / std::sqrt(squaredLength);
        return {w * inverseLength, x * inverseLength, y * inverseLength, z * inverseLength};
    }

    [[nodiscard]] constexpr Quat conjugate() const noexcept {
        return {w, -x, -y, -z};
    }

    [[nodiscard]] Vec3 rotate(const Vec3& value) const noexcept {
        const Vec3 axis{x, y, z};
        const Vec3 doubledCross = cross(axis, value) * 2.0;
        return value + doubledCross * w + cross(axis, doubledCross);
    }

    [[nodiscard]] Vec3 inverseRotate(const Vec3& value) const noexcept {
        return conjugate().rotate(value);
    }

    friend constexpr Quat operator*(const Quat& left, const Quat& right) noexcept {
        return {
            left.w * right.w - left.x * right.x - left.y * right.y - left.z * right.z,
            left.w * right.x + left.x * right.w + left.y * right.z - left.z * right.y,
            left.w * right.y - left.x * right.z + left.y * right.w + left.z * right.x,
            left.w * right.z + left.x * right.y - left.y * right.x + left.z * right.w,
        };
    }

    friend constexpr Quat operator*(const Quat& value, double scalar) noexcept {
        return {value.w * scalar, value.x * scalar, value.y * scalar, value.z * scalar};
    }

    friend constexpr Quat operator*(double scalar, const Quat& value) noexcept {
        return value * scalar;
    }
};

}
