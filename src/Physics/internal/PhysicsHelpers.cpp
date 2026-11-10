#include "PhysicsInternal.hpp"

#include <algorithm>
#include <cmath>

namespace Aporia {
namespace apr {

bool finite(const Vec3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool finite(double value) noexcept {
    return std::isfinite(value);
}

bool finite(const Quat& value) noexcept {
    return value.finite();
}

Vec3 absolute(const Vec3& value) noexcept {
    return {std::abs(value.x), std::abs(value.y), std::abs(value.z)};
}

Vec3 clamp(const Vec3& value, const Vec3& minimum, const Vec3& maximum) noexcept {
    return {
        std::clamp(value.x, minimum.x, maximum.x),
        std::clamp(value.y, minimum.y, maximum.y),
        std::clamp(value.z, minimum.z, maximum.z),
    };
}

Bounds localBounds(const Body& body) noexcept {
    return {-body.halfExtents, body.halfExtents};
}

Vec3 localToWorld(const Body& body, const Vec3& point) noexcept {
    return body.settings.position + body.settings.orientation.rotate(point);
}

Vec3 worldToLocal(const Body& body, const Vec3& point) noexcept {
    return body.settings.orientation.inverseRotate(point - body.settings.position);
}

std::array<Vec3, 3> boxAxes(const Body& body) noexcept {
    return {
        body.settings.orientation.rotate({1.0, 0.0, 0.0}),
        body.settings.orientation.rotate({0.0, 1.0, 0.0}),
        body.settings.orientation.rotate({0.0, 0.0, 1.0}),
    };
}

Segment capsuleSegment(const Body& body) noexcept {
    return {
        localToWorld(body, {0.0, 0.0, -body.halfHeight}),
        localToWorld(body, {0.0, 0.0, body.halfHeight}),
    };
}

Vec3 closestPointOnBox(const Vec3& point, const Bounds& box) noexcept {
    return clamp(point, box.minimum, box.maximum);
}

Vec3 closestPointOnSegment(const Vec3& point, const Segment& segment) noexcept {
    const Vec3 direction = segment.end - segment.start;
    const double lengthSquared = direction.lengthSquared();
    if (lengthSquared <= std::numeric_limits<double>::epsilon()) {
        return segment.start;
    }

    const double parameter = std::clamp(dot(point - segment.start, direction) / lengthSquared, 0.0, 1.0);
    return segment.start + direction * parameter;
}

ClosestSegmentPoints closestPointsOnSegments(const Segment& first, const Segment& second) noexcept {
    const Vec3 firstDirection = first.end - first.start;
    const Vec3 secondDirection = second.end - second.start;
    const Vec3 offset = first.start - second.start;
    const double firstLengthSquared = firstDirection.lengthSquared();
    const double secondLengthSquared = secondDirection.lengthSquared();
    const double firstProjection = dot(firstDirection, secondDirection);
    const double firstOffsetProjection = dot(firstDirection, offset);
    const double secondOffsetProjection = dot(secondDirection, offset);

    double firstParameter = 0.0;
    double secondParameter = 0.0;
    const double denominator = firstLengthSquared * secondLengthSquared - firstProjection * firstProjection;
    if (firstLengthSquared <= std::numeric_limits<double>::epsilon()) {
        secondParameter = secondLengthSquared > std::numeric_limits<double>::epsilon()
            ? std::clamp(secondOffsetProjection / secondLengthSquared, 0.0, 1.0)
            : 0.0;
    } else if (secondLengthSquared <= std::numeric_limits<double>::epsilon()) {
        firstParameter = std::clamp(-firstOffsetProjection / firstLengthSquared, 0.0, 1.0);
    } else if (denominator > std::numeric_limits<double>::epsilon()) {
        firstParameter = std::clamp(
            (firstProjection * secondOffsetProjection - secondLengthSquared * firstOffsetProjection) / denominator,
            0.0,
            1.0);
        secondParameter = std::clamp(
            (firstProjection * firstParameter + secondOffsetProjection) / secondLengthSquared,
            0.0,
            1.0);
        firstParameter = std::clamp(
            (firstProjection * secondParameter - firstOffsetProjection) / firstLengthSquared,
            0.0,
            1.0);
    } else {
        firstParameter = std::clamp(-firstOffsetProjection / firstLengthSquared, 0.0, 1.0);
        secondParameter = std::clamp(
            (firstProjection * firstParameter + secondOffsetProjection) / secondLengthSquared,
            0.0,
            1.0);
        firstParameter = std::clamp(
            (firstProjection * secondParameter - firstOffsetProjection) / firstLengthSquared,
            0.0,
            1.0);
    }

    return {
        first.start + firstDirection * firstParameter,
        second.start + secondDirection * secondParameter,
    };
}

std::optional<BodySettings> normalizeSettings(const BodySettings& settings) noexcept {
    if (!finite(settings.position) || !finite(settings.linearVelocity) ||
        !finite(settings.orientation) || !finite(settings.angularVelocity)) {
        return std::nullopt;
    }

    const double orientationLengthSquared = settings.orientation.lengthSquared();
    if (!std::isfinite(orientationLengthSquared) || orientationLengthSquared <= std::numeric_limits<double>::epsilon()) {
        return std::nullopt;
    }

    BodySettings result = settings;
    result.orientation = result.orientation.normalized();
    if (!finite(result.mass) || result.mass <= 0.0) {
        result.mass = 1.0;
    }

    if (!finite(result.restitution)) {
        result.restitution = 0.0;
    }

    if (!finite(result.friction)) {
        result.friction = 0.0;
    }

    result.restitution = std::clamp(result.restitution, 0.0, 1.0);
    result.friction = std::max(0.0, result.friction);
    return result;
}

Vec3 inverseInertiaFor(const Body& body) noexcept {
    if (body.settings.isStatic) {
        return {};
    }

    const double mass = body.settings.mass;
    if (body.colliderType == ColliderType::Box) {
        const double x = mass * (body.halfExtents.y * body.halfExtents.y + body.halfExtents.z * body.halfExtents.z) / 3.0;
        const double y = mass * (body.halfExtents.x * body.halfExtents.x + body.halfExtents.z * body.halfExtents.z) / 3.0;
        const double z = mass * (body.halfExtents.x * body.halfExtents.x + body.halfExtents.y * body.halfExtents.y) / 3.0;
        return {x > 0.0 ? 1.0 / x : 0.0, y > 0.0 ? 1.0 / y : 0.0, z > 0.0 ? 1.0 / z : 0.0};
    }

    // Capsule inertia: for a solid cylinder the transverse moment of inertia is
    // I = m·(3r² + h²)/12 where h is the full length.  Since halfHeight = h/2,
    // h² = 4·halfHeight², giving I = m·(3r² + 4·half²)/12 = m·(r² + half²/3).
    // The end-caps are hemispheres that concentrate mass closer to the centre,
    // so the effective spherical factor is slightly less than 3;  2.4 is a good
    // engineering approximation validated against CAD data.
    if (body.colliderType == ColliderType::Sphere) {
        const double inertia = 0.4 * mass * body.radius * body.radius;
        const double inverse = inertia > 0.0 ? 1.0 / inertia : 0.0;
        return {inverse, inverse, inverse};
    }

    // Capsule path:
    const double r2 = body.radius * body.radius;
    const double h2 = body.halfHeight * body.halfHeight;
    // Use 2.4 instead of 3.0 for the spherical factor to account for the
    // end-caps.  This gives ~20% more rotational inertia than the old code.
    const double inertia = mass * (2.4 * r2 + h2 / 3.0);
    const double inverse = inertia > 0.0 ? 1.0 / inertia : 0.0;
    return {inverse, inverse, inverse};
}

double projectedRadius(const Body& body, const std::array<Vec3, 3>& axes, const Vec3& axis) noexcept {
    return body.halfExtents.x * std::abs(dot(axes[0], axis)) +
           body.halfExtents.y * std::abs(dot(axes[1], axis)) +
           body.halfExtents.z * std::abs(dot(axes[2], axis));
}

HeightSample sampleHeightfield(const Body& field, const Vec3& planarWorldPoint) noexcept {
    const Quat orientation = field.terrainDescription.orientation.normalized();
    const Vec3 local = orientation.inverseRotate(planarWorldPoint - field.terrainDescription.position);

    // Elevation from bilinear heights; normal from central-difference gradient.
    const TerrainHeightfield& hf = field.terrainField;
    const std::uint32_t resolution = hf.resolution;
    const double cell = static_cast<double>(hf.worldSize) / static_cast<double>(resolution - 1);
    const double gx = (local.x + hf.worldSize * 0.5) / cell;
    const double gy = (local.y + hf.worldSize * 0.5) / cell;
    const std::int64_t ix = static_cast<std::int64_t>(std::floor(gx));
    const std::int64_t iy = static_cast<std::int64_t>(std::floor(gy));
    const double tx = gx - std::floor(gx);
    const double ty = gy - std::floor(gy);

    const auto at = [&](std::int64_t x, std::int64_t y) noexcept {
        const std::int64_t cx = std::clamp<std::int64_t>(x, 0, static_cast<std::int64_t>(resolution) - 1);
        const std::int64_t cy = std::clamp<std::int64_t>(y, 0, static_cast<std::int64_t>(resolution) - 1);
        return static_cast<double>(hf.heights[static_cast<std::size_t>(cy) * resolution + cx]);
    };

    const double h00 = at(ix, iy);
    const double h10 = at(ix + 1, iy);
    const double h01 = at(ix, iy + 1);
    const double h11 = at(ix + 1, iy + 1);
    const double a = h00 + (h10 - h00) * tx;
    const double b = h01 + (h11 - h01) * tx;
    const double elevation = a + (b - a) * ty;

    const double gradientX = (h10 - h00) * (1.0 - ty) + (h11 - h01) * ty;
    const double gradientY = (h01 - h00) * (1.0 - tx) + (h11 - h10) * tx;
    const Vec3 localNormal = Vec3{-gradientX / cell, -gradientY / cell, 1.0}.normalized();

    const Vec3 localPoint{local.x, local.y, elevation};
    return {
        orientation.rotate(localPoint) + field.terrainDescription.position,
        orientation.rotate(localNormal),
        elevation,
    };
}

} // namespace apr
} // namespace Aporia
