#include "PhysicsInternal.hpp"

#include <algorithm>
#include <cmath>

namespace Aporia {
namespace apr {

RaycastInternalHit raycastSphere(const Ray& ray, const Body& sphere) noexcept {
    RaycastInternalHit hit;
    const Vec3 m = ray.origin - sphere.settings.position;
    const double b = dot(m, ray.direction);
    const double c = dot(m, m) - sphere.radius * sphere.radius;
    if (c > 0.0 && b > 0.0) {
        return hit;
    }
    const double discr = b * b - c;
    if (discr < 0.0) {
        return hit;
    }
    double t = -b - std::sqrt(discr);
    if (t < 0.0) {
        t = -b + std::sqrt(discr);
    }
    if (t < 0.0) {
        return hit;
    }

    hit.distance = t;
    hit.point = ray.pointAt(t);
    hit.normal = (hit.point - sphere.settings.position).normalized();
    hit.hasHit = true;
    return hit;
}

RaycastInternalHit raycastBox(const Ray& ray, const Body& box) noexcept {
    RaycastInternalHit hit;
    const Vec3 localOrigin = worldToLocal(box, ray.origin);
    const Vec3 localDir = box.settings.orientation.inverseRotate(ray.direction);

    double tmin = 0.0;
    double tmax = std::numeric_limits<double>::infinity();
    Vec3 hitNormalLocal{0.0, 0.0, 1.0};

    const double boundsMin[3] = {-box.halfExtents.x, -box.halfExtents.y, -box.halfExtents.z};
    const double boundsMax[3] = {box.halfExtents.x, box.halfExtents.y, box.halfExtents.z};
    const double originArr[3] = {localOrigin.x, localOrigin.y, localOrigin.z};
    const double dirArr[3] = {localDir.x, localDir.y, localDir.z};

    for (int i = 0; i < 3; ++i) {
        if (std::abs(dirArr[i]) < 1.0e-9) {
            if (originArr[i] < boundsMin[i] || originArr[i] > boundsMax[i]) {
                return hit;
            }
        } else {
            const double ood = 1.0 / dirArr[i];
            double t1 = (boundsMin[i] - originArr[i]) * ood;
            double t2 = (boundsMax[i] - originArr[i]) * ood;
            Vec3 n1{}, n2{};
            if (i == 0) { n1.x = -1.0; n2.x = 1.0; }
            else if (i == 1) { n1.y = -1.0; n2.y = 1.0; }
            else { n1.z = -1.0; n2.z = 1.0; }

            if (t1 > t2) {
                std::swap(t1, t2);
                std::swap(n1, n2);
            }
            if (t1 > tmin) {
                tmin = t1;
                hitNormalLocal = n1;
            }
            tmax = std::min(tmax, t2);
            if (tmin > tmax) {
                return hit;
            }
        }
    }

    if (tmin < 0.0) {
        return hit;
    }

    hit.distance = tmin;
    hit.point = ray.pointAt(tmin);
    hit.normal = box.settings.orientation.rotate(hitNormalLocal).normalized();
    hit.hasHit = true;
    return hit;
}

RaycastInternalHit raycastCapsule(const Ray& ray, const Body& capsule) noexcept {
    RaycastInternalHit hit;
    const Segment seg = capsuleSegment(capsule);
    const Vec3 d = seg.end - seg.start;
    const Vec3 m = ray.origin - seg.start;
    const Vec3 n = ray.direction;

    const double md = dot(m, d);
    const double nd = dot(n, d);
    const double dd = dot(d, d);

    if (dd <= std::numeric_limits<double>::epsilon()) {
        Body sphereBody = capsule;
        sphereBody.settings.position = seg.start;
        return raycastSphere(ray, sphereBody);
    }

    const double nn = dot(n, n);
    const double mn = dot(m, n);
    const double a = dd * nn - nd * nd;
    const double k = dot(m, m) - capsule.radius * capsule.radius;
    const double c = dd * k - md * md;

    if (std::abs(a) > 1.0e-9) {
        const double b = dd * mn - nd * md;
        const double discr = b * b - a * c;
        if (discr >= 0.0) {
            const double t = (-b - std::sqrt(discr)) / a;
            if (t >= 0.0) {
                const double y = md + t * nd;
                if (y >= 0.0 && y <= dd) {
                    hit.distance = t;
                    hit.point = ray.pointAt(t);
                    const Vec3 axisPoint = seg.start + d * (y / dd);
                    hit.normal = (hit.point - axisPoint).normalized();
                    hit.hasHit = true;
                }
            }
        }
    }

    Body cap0 = capsule;
    cap0.settings.position = seg.start;
    const RaycastInternalHit hit0 = raycastSphere(ray, cap0);
    if (hit0.hasHit && hit0.distance < hit.distance) {
        hit = hit0;
    }

    Body cap1 = capsule;
    cap1.settings.position = seg.end;
    const RaycastInternalHit hit1 = raycastSphere(ray, cap1);
    if (hit1.hasHit && hit1.distance < hit.distance) {
        hit = hit1;
    }

    return hit;
}

RaycastInternalHit raycastHeightfield(const Ray& ray, const Body& field, double maxDistance) noexcept {
    RaycastInternalHit hit;
    if (field.terrainField.heights.empty() || field.terrainField.resolution < 2) {
        return hit;
    }

    // Transform the ray into the terrain's local space where height is always Z.
    // sampleHeightfield already works in local space; raycast must match.
    const Quat orientation = field.terrainDescription.orientation.normalized();
    const Vec3 localOrigin = orientation.inverseRotate(ray.origin - field.terrainDescription.position);
    const Vec3 localDir    = orientation.inverseRotate(ray.direction);
    const Ray localRay{localOrigin, localDir};

    const double cell = static_cast<double>(field.terrainField.worldSize) /
                        static_cast<double>(field.terrainField.resolution - 1);
    const double stepSize = std::max(0.05, cell * 0.5);

    double currentDist = 0.0;
    double prevDiff = 0.0;
    bool hasPrev = false;

    while (currentDist <= maxDistance) {
        const Vec3 currentPoint = localRay.pointAt(currentDist);
        // sampleHeightfield expects world-space input, but currentPoint is local.
        // Convert to world for the call, then use local Z for the elevation test.
        const Vec3 worldPoint = orientation.rotate(currentPoint) + field.terrainDescription.position;
        const HeightSample sample = sampleHeightfield(field, worldPoint);
        const double diff = currentPoint.z - sample.elevation;

        if (hasPrev && ((prevDiff > 0.0 && diff <= 0.0) || (prevDiff < 0.0 && diff >= 0.0))) {
            double t0 = currentDist - stepSize;
            double t1 = currentDist;
            for (int iter = 0; iter < 16; ++iter) {
                const double tmid = 0.5 * (t0 + t1);
                const Vec3 pmid = localRay.pointAt(tmid);
                const Vec3 pworld = orientation.rotate(pmid) + field.terrainDescription.position;
                const HeightSample smid = sampleHeightfield(field, pworld);
                if (pmid.z > smid.elevation) {
                    t0 = tmid;
                } else {
                    t1 = tmid;
                }
            }
            const double tHit = 0.5 * (t0 + t1);
            hit.distance = tHit;
            // Transform local-space hit point back to world space.
            hit.point = orientation.rotate(localRay.pointAt(tHit)) +
                        field.terrainDescription.position;
            // sampleHeightfield already returns the world-space normal.
            const HeightSample finalSample = sampleHeightfield(field, hit.point);
            hit.normal = finalSample.normal;
            hit.hasHit = true;
            return hit;
        }

        prevDiff = diff;
        hasPrev = true;
        currentDist += stepSize;
    }

    return hit;
}

} // namespace apr
} // namespace Aporia
