// Sun view-projection shared by every renderer.
//
// The helper is deliberately free of graphics API types: the Direct3D renderer
// builds the shadow matrix from it and uploads it to the shader unchanged.
#pragma once

#include "Aporia/Math/Matrix4.hpp"
#include "Aporia/Math/Vector3.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace Aporia::graphics_internal {

// Builds the sun view-projection, snapped to the shadow texel grid so the
// map does not swim when the camera moves.
inline void buildSunMatrix(
    const float sunDirectionIn[3],
    const double cameraPositionIn[3],
    float shadowExtent,
    std::uint32_t mapSize,
    float outMatrix[16]) noexcept {
    float directionX = sunDirectionIn[0];
    float directionY = sunDirectionIn[1];
    float directionZ = sunDirectionIn[2];
    const float length =
        std::sqrt(directionX * directionX + directionY * directionY + directionZ * directionZ);
    if (length <= 1.0e-6f) {
        directionX = 0.35f;
        directionY = -0.5f;
        directionZ = 0.8f;
    } else {
        directionX /= length;
        directionY /= length;
        directionZ /= length;
    }
    if (std::abs(directionZ) > 0.999f) {
        directionZ = directionZ > 0.0f ? 0.999f : -0.999f;
    }

    const Aporia::Vec3 sunDirection(directionX, directionY, directionZ);
    const Aporia::Vec3 cameraPosition(
        cameraPositionIn[0], cameraPositionIn[1], cameraPositionIn[2]);
    Aporia::Vec3 center = cameraPosition;
    const Aporia::Vec3 up =
        std::abs(directionZ) < 0.9f ? Aporia::Vec3(0.0, 0.0, 1.0) : Aporia::Vec3(1.0, 0.0, 0.0);
    const double extent = std::max(1.0, static_cast<double>(shadowExtent));
    // The sun eye sits far enough that the whole ortho box (plus room for tall
    // casters) fits between near and far, whatever extent the host picked.
    const double eyeDistance = std::max(60.0, extent + 40.0);
    const double farPlane = std::max(200.0, eyeDistance + extent + 80.0);
    const Aporia::Vec3 lightEye = center + sunDirection * eyeDistance;
    const Aporia::Mat4 unsnappedView = Aporia::Mat4::lookAt(lightEye, center, up);
    const Aporia::Vec3 right{unsnappedView.m[0][0], unsnappedView.m[1][0], unsnappedView.m[2][0]};
    const Aporia::Vec3 correctedUp{unsnappedView.m[0][1], unsnappedView.m[1][1], unsnappedView.m[2][1]};
    // The light-space position of the camera centre. offset itself is parallel
    // to the sun direction, so projecting IT onto right/up always gives zero --
    // the snap has to work on the absolute light-space centre, otherwise the
    // texel grid slides with the camera and every shadow edge swims.
    const double lightX = dot(right, center);
    const double lightY = dot(correctedUp, center);
    const double texelWorldSize = (extent * 2.0) / static_cast<double>(std::max(1u, mapSize));
    if (texelWorldSize > 0.0) {
        const double snappedX = std::round(lightX / texelWorldSize) * texelWorldSize;
        const double snappedY = std::round(lightY / texelWorldSize) * texelWorldSize;
        center += right * (snappedX - lightX) + correctedUp * (snappedY - lightY);
    }
    const Aporia::Vec3 eye = center + sunDirection * eyeDistance;
    const Aporia::Mat4 view = Aporia::Mat4::lookAt(eye, center, up);
    const Aporia::Mat4 projection =
        Aporia::Mat4::orthographicD3D(extent * 2.0, extent * 2.0, 0.1, farPlane);
    const Aporia::Mat4 sunViewProj = projection * view;
    std::memcpy(outMatrix, &sunViewProj, sizeof(sunViewProj));
}

}  // namespace Aporia::graphics_internal
