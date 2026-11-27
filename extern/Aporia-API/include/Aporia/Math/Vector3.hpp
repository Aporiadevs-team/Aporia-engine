#pragma once

#include <cmath>
#include <limits>

namespace Aporia {

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    constexpr Vec3() noexcept = default;
    constexpr Vec3(double xValue, double yValue, double zValue) noexcept
        : x(xValue), y(yValue), z(zValue) {
    }

    Vec3& operator+=(const Vec3& other) noexcept {
        x += other.x;
        y += other.y;
        z += other.z;
        return *this;
    }

    Vec3& operator-=(const Vec3& other) noexcept {
        x -= other.x;
        y -= other.y;
        z -= other.z;
        return *this;
    }

    Vec3& operator*=(double scalar) noexcept {
        x *= scalar;
        y *= scalar;
        z *= scalar;
        return *this;
    }

    Vec3& operator/=(double scalar) noexcept {
        x /= scalar;
        y /= scalar;
        z /= scalar;
        return *this;
    }

    [[nodiscard]] double lengthSquared() const noexcept {
        return x * x + y * y + z * z;
    }

    [[nodiscard]] double length() const noexcept {
        return std::sqrt(lengthSquared());
    }

    [[nodiscard]] Vec3 normalized() const noexcept {
        const double squaredLength = lengthSquared();
        if (squaredLength <= std::numeric_limits<double>::epsilon()) {
            return {};
        }

        return *this * (1.0 / std::sqrt(squaredLength));
    }

    friend constexpr Vec3 operator+(Vec3 left, const Vec3& right) noexcept {
        return {left.x + right.x, left.y + right.y, left.z + right.z};
    }

    friend constexpr Vec3 operator-(Vec3 left, const Vec3& right) noexcept {
        return {left.x - right.x, left.y - right.y, left.z - right.z};
    }

    friend constexpr Vec3 operator-(const Vec3& value) noexcept {
        return {-value.x, -value.y, -value.z};
    }

    friend constexpr Vec3 operator*(Vec3 value, double scalar) noexcept {
        return {value.x * scalar, value.y * scalar, value.z * scalar};
    }

    friend constexpr Vec3 operator*(double scalar, Vec3 value) noexcept {
        return value * scalar;
    }

    friend constexpr Vec3 operator/(Vec3 value, double scalar) noexcept {
        return {value.x / scalar, value.y / scalar, value.z / scalar};
    }

    friend constexpr Vec3 cross(const Vec3& left, const Vec3& right) noexcept {
        return {
            left.y * right.z - left.z * right.y,
            left.z * right.x - left.x * right.z,
            left.x * right.y - left.y * right.x,
        };
    }

    friend constexpr double dot(const Vec3& left, const Vec3& right) noexcept {
        return left.x * right.x + left.y * right.y + left.z * right.z;
    }
};

}
