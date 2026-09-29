#pragma once

#include "Aporia/Math/Quaternion.hpp"
#include "Aporia/Math/Vector3.hpp"

#include <cmath>
#include <limits>

namespace Aporia {

// Column-major 4x4 matrix matching Vulkan memory layout:
// storage[column][row], multiplication left-to-right applies rightmost first.
struct Mat4 {
    float m[4][4] = {};

    constexpr Mat4() noexcept {
        m[0][0] = 1.0f;
        m[1][1] = 1.0f;
        m[2][2] = 1.0f;
        m[3][3] = 1.0f;
    }

    constexpr Mat4(float m00, float m01, float m02, float m03,
                   float m10, float m11, float m12, float m13,
                   float m20, float m21, float m22, float m23,
                   float m30, float m31, float m32, float m33) noexcept {
        m[0][0] = m00; m[0][1] = m01; m[0][2] = m02; m[0][3] = m03;
        m[1][0] = m10; m[1][1] = m11; m[1][2] = m12; m[1][3] = m13;
        m[2][0] = m20; m[2][1] = m21; m[2][2] = m22; m[2][3] = m23;
        m[3][0] = m30; m[3][1] = m31; m[3][2] = m32; m[3][3] = m33;
    }

    [[nodiscard]] static constexpr Mat4 identity() noexcept {
        return {};
    }

    [[nodiscard]] static constexpr Mat4 translation(const Vec3& value) noexcept {
        Mat4 result;
        result.m[3][0] = static_cast<float>(value.x);
        result.m[3][1] = static_cast<float>(value.y);
        result.m[3][2] = static_cast<float>(value.z);
        return result;
    }

    [[nodiscard]] static constexpr Mat4 scale(const Vec3& value) noexcept {
        Mat4 result;
        result.m[0][0] = static_cast<float>(value.x);
        result.m[1][1] = static_cast<float>(value.y);
        result.m[2][2] = static_cast<float>(value.z);
        return result;
    }

    [[nodiscard]] static Mat4 fromQuaternion(const Quat& value) noexcept {
        const Quat unit = value.normalized();
        const float x = static_cast<float>(unit.x);
        const float y = static_cast<float>(unit.y);
        const float z = static_cast<float>(unit.z);
        const float w = static_cast<float>(unit.w);

        Mat4 result;
        result.m[0][0] = 1.0f - 2.0f * (y * y + z * z);
        result.m[0][1] = 2.0f * (x * y + w * z);
        result.m[0][2] = 2.0f * (x * z - w * y);
        result.m[1][0] = 2.0f * (x * y - w * z);
        result.m[1][1] = 1.0f - 2.0f * (x * x + z * z);
        result.m[1][2] = 2.0f * (y * z + w * x);
        result.m[2][0] = 2.0f * (x * z + w * y);
        result.m[2][1] = 2.0f * (y * z - w * x);
        result.m[2][2] = 1.0f - 2.0f * (x * x + y * y);
        return result;
    }

    [[nodiscard]] static Mat4 lookAt(const Vec3& eye, const Vec3& target, const Vec3& up) noexcept {
        const Vec3 forward = (target - eye).normalized();
        const Vec3 right = cross(forward, up).normalized();
        const Vec3 correctedUp = cross(right, forward);

        Mat4 result;
        result.m[0][0] = static_cast<float>(right.x);
        result.m[1][0] = static_cast<float>(right.y);
        result.m[2][0] = static_cast<float>(right.z);
        result.m[0][1] = static_cast<float>(correctedUp.x);
        result.m[1][1] = static_cast<float>(correctedUp.y);
        result.m[2][1] = static_cast<float>(correctedUp.z);
        result.m[0][2] = static_cast<float>(-forward.x);
        result.m[1][2] = static_cast<float>(-forward.y);
        result.m[2][2] = static_cast<float>(-forward.z);
        result.m[3][0] = static_cast<float>(-dot(right, eye));
        result.m[3][1] = static_cast<float>(-dot(correctedUp, eye));
        result.m[3][2] = static_cast<float>(dot(forward, eye));
        return result;
    }

    // Vulkan clip space: right-handed view, +Y down in NDC, depth in [0, 1].
    [[nodiscard]] static Mat4 perspectiveVulkan(double verticalFovRadians, double aspect, double nearZ, double farZ) noexcept {
        const float focalLength = static_cast<float>(1.0 / std::tan(verticalFovRadians * 0.5));
        const float depth = static_cast<float>(1.0 / (nearZ - farZ));

        Mat4 result;
        result.m[0][0] = focalLength / static_cast<float>(aspect);
        result.m[1][1] = -focalLength;
        result.m[2][2] = static_cast<float>(farZ * depth);
        result.m[2][3] = -1.0f;
        result.m[3][2] = static_cast<float>(nearZ * farZ * depth);
        result.m[3][3] = 0.0f;
        return result;
    }

    // Vulkan clip space orthographic projection with depth in [0, 1].
    [[nodiscard]] static Mat4 orthographicVulkan(double width, double height, double nearZ, double farZ) noexcept {
        const float depth = static_cast<float>(1.0 / (nearZ - farZ));

        Mat4 result;
        result.m[0][0] = static_cast<float>(2.0 / width);
        result.m[1][1] = static_cast<float>(-2.0 / height);
        result.m[2][2] = static_cast<float>(depth);
        result.m[3][2] = static_cast<float>(nearZ * depth);
        return result;
    }

    constexpr Mat4 operator*(const Mat4& right) const noexcept {
        Mat4 result;
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                float sum = 0.0f;
                for (int index = 0; index < 4; ++index) {
                    sum += m[index][row] * right.m[column][index];
                }
                result.m[column][row] = sum;
            }
        }
        return result;
    }

    [[nodiscard]] Vec3 transformPoint(const Vec3& value) const noexcept {
        const float x = static_cast<float>(value.x);
        const float y = static_cast<float>(value.y);
        const float z = static_cast<float>(value.z);
        const float w = m[0][3] * x + m[1][3] * y + m[2][3] * z + m[3][3];
        return {
            (m[0][0] * x + m[1][0] * y + m[2][0] * z + m[3][0]) / w,
            (m[0][1] * x + m[1][1] * y + m[2][1] * z + m[3][1]) / w,
            (m[0][2] * x + m[1][2] * y + m[2][2] * z + m[3][2]) / w,
        };
    }

    [[nodiscard]] Vec3 transformVector(const Vec3& value) const noexcept {
        const float x = static_cast<float>(value.x);
        const float y = static_cast<float>(value.y);
        const float z = static_cast<float>(value.z);
        return {
            m[0][0] * x + m[1][0] * y + m[2][0] * z,
            m[0][1] * x + m[1][1] * y + m[2][1] * z,
            m[0][2] * x + m[1][2] * y + m[2][2] * z,
        };
    }

    [[nodiscard]] static constexpr Mat4 zero() noexcept {
        Mat4 result;
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                result.m[column][row] = 0.0f;
            }
        }
        return result;
    }

    friend constexpr bool operator==(const Mat4&, const Mat4&) noexcept = default;
};

}
