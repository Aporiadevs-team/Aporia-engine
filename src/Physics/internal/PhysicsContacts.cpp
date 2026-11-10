#include "PhysicsInternal.hpp"

#include <algorithm>
#include <cmath>

namespace Aporia {
namespace apr {

std::optional<Contact> sphereSphere(const Body& first, const Body& second) noexcept {
    const Vec3 separation = second.settings.position - first.settings.position;
    const double radiusSum = first.radius + second.radius;
    const double distanceSquared = separation.lengthSquared();

    if (distanceSquared >= radiusSum * radiusSum) {
        return std::nullopt;
    }

    const double distance = std::sqrt(distanceSquared);
    const Vec3 normal = distance > std::numeric_limits<double>::epsilon()
        ? separation / distance
        : Vec3{1.0, 0.0, 0.0};
    const Vec3 point = first.settings.position + normal * first.radius;
    return Contact{normal, point, radiusSum - distance};
}

std::optional<Contact> sphereBox(const Body& sphere, const Body& box) noexcept {
    const Bounds boxBounds = localBounds(box);
    const Vec3 localSphere = worldToLocal(box, sphere.settings.position);
    const Vec3 localClosest = closestPointOnBox(localSphere, boxBounds);
    const Vec3 localSeparation = localSphere - localClosest;
    const double distanceSquared = localSeparation.lengthSquared();

    if (distanceSquared > std::numeric_limits<double>::epsilon()) {
        const double distance = std::sqrt(distanceSquared);
        if (distance >= sphere.radius) {
            return std::nullopt;
        }

        const Vec3 normal = box.settings.orientation.rotate(localSeparation / distance);
        const Vec3 point = localToWorld(box, localClosest);
        return Contact{normal, point, sphere.radius - distance};
    }

    const double positiveX = boxBounds.maximum.x - localSphere.x;
    const double negativeX = localSphere.x - boxBounds.minimum.x;
    const double positiveY = boxBounds.maximum.y - localSphere.y;
    const double negativeY = localSphere.y - boxBounds.minimum.y;
    const double positiveZ = boxBounds.maximum.z - localSphere.z;
    const double negativeZ = localSphere.z - boxBounds.minimum.z;

    double distanceToFace = positiveX;
    Vec3 localNormal{1.0, 0.0, 0.0};
    if (negativeX < distanceToFace) {
        distanceToFace = negativeX;
        localNormal = {-1.0, 0.0, 0.0};
    }
    if (positiveY < distanceToFace) {
        distanceToFace = positiveY;
        localNormal = {0.0, 1.0, 0.0};
    }
    if (negativeY < distanceToFace) {
        distanceToFace = negativeY;
        localNormal = {0.0, -1.0, 0.0};
    }
    if (positiveZ < distanceToFace) {
        distanceToFace = positiveZ;
        localNormal = {0.0, 0.0, 1.0};
    }
    if (negativeZ < distanceToFace) {
        distanceToFace = negativeZ;
        localNormal = {0.0, 0.0, -1.0};
    }

    Vec3 localPoint = localSphere;
    if (localNormal.x > 0.5) {
        localPoint.x = boxBounds.maximum.x;
    } else if (localNormal.x < -0.5) {
        localPoint.x = boxBounds.minimum.x;
    } else if (localNormal.y > 0.5) {
        localPoint.y = boxBounds.maximum.y;
    } else if (localNormal.y < -0.5) {
        localPoint.y = boxBounds.minimum.y;
    } else if (localNormal.z > 0.5) {
        localPoint.z = boxBounds.maximum.z;
    } else {
        localPoint.z = boxBounds.minimum.z;
    }

    return Contact{
        box.settings.orientation.rotate(localNormal),
        localToWorld(box, localPoint),
        sphere.radius + distanceToFace,
    };
}

std::optional<Contact> sphereCapsule(const Body& sphere, const Body& capsule) noexcept {
    const Segment segment = capsuleSegment(capsule);
    const Vec3 closest = closestPointOnSegment(sphere.settings.position, segment);
    const Vec3 separation = sphere.settings.position - closest;
    const double radiusSum = sphere.radius + capsule.radius;
    const double distanceSquared = separation.lengthSquared();

    if (distanceSquared >= radiusSum * radiusSum) {
        return std::nullopt;
    }

    const double distance = std::sqrt(distanceSquared);
    const Vec3 normal = distance > std::numeric_limits<double>::epsilon()
        ? separation / distance
        : Vec3{1.0, 0.0, 0.0};
    return Contact{normal, closest, radiusSum - distance};
}

std::optional<Contact> boxBox(const Body& first, const Body& second) noexcept {
    const std::array<Vec3, 3> firstAxes = boxAxes(first);
    const std::array<Vec3, 3> secondAxes = boxAxes(second);
    const Vec3 centerSeparation = second.settings.position - first.settings.position;
    std::optional<Contact> bestContact;
    double smallestOverlap = std::numeric_limits<double>::infinity();
    Vec3 bestContactPoint{};  // will be set when best axis is found

    const auto testAxis = [&](const Vec3& candidate) noexcept {
        const double lengthSquared = candidate.lengthSquared();
        if (lengthSquared <= std::numeric_limits<double>::epsilon()) {
            return;
        }

        const Vec3 axis = candidate / std::sqrt(lengthSquared);
        const double projectedDistance = dot(centerSeparation, axis);
        const double overlap = projectedRadius(first, firstAxes, axis) +
                               projectedRadius(second, secondAxes, axis) - std::abs(projectedDistance);
        if (overlap <= 0.0 || overlap >= smallestOverlap) {
            return;
        }

        const double sign = projectedDistance >= 0.0 ? 1.0 : -1.0;
        smallestOverlap = overlap;
        // Contact point: the deepest corner of the incident body projected
        // along the contact normal.  The incident body is the one whose face
        // is parallel to the reference face (i.e. the body whose axis aligns
        // with the MTD axis).
        const Body* incident = projectedDistance >= 0.0 ? &first : &second;
        const Bounds b = localBounds(*incident);
        // Pick the corner furthest along (or against) the normal.
        const Vec3 localCorner{
            sign * b.minimum.x > 0.0 ? b.minimum.x : b.maximum.x,
            sign * b.minimum.y > 0.0 ? b.minimum.y : b.maximum.y,
            sign * b.minimum.z > 0.0 ? b.minimum.z : b.maximum.z,
        };
        bestContactPoint = localToWorld(*incident, localCorner);
        bestContact = Contact{axis * sign, bestContactPoint, overlap};
    };

    for (const Vec3& axis : firstAxes) {
        testAxis(axis);
    }
    for (const Vec3& axis : secondAxes) {
        testAxis(axis);
    }
    for (const Vec3& firstAxis : firstAxes) {
        for (const Vec3& secondAxis : secondAxes) {
            testAxis(cross(firstAxis, secondAxis));
        }
    }

    return bestContact;
}

std::optional<Contact> capsuleCapsule(const Body& first, const Body& second) noexcept {
    const ClosestSegmentPoints closest = closestPointsOnSegments(capsuleSegment(first), capsuleSegment(second));
    const Vec3 separation = closest.second - closest.first;
    const double radiusSum = first.radius + second.radius;
    const double distanceSquared = separation.lengthSquared();
    if (distanceSquared >= radiusSum * radiusSum) {
        return std::nullopt;
    }

    const double distance = std::sqrt(distanceSquared);
    const Vec3 normal = distance > std::numeric_limits<double>::epsilon()
        ? separation / distance
        : Vec3{1.0, 0.0, 0.0};
    return Contact{normal, (closest.first + closest.second) * 0.5, radiusSum - distance};
}

std::optional<Contact> boxCapsule(const Body& box, const Body& capsule) noexcept {
    const Bounds boxBounds = localBounds(box);
    const Segment worldSegment = capsuleSegment(capsule);
    const Vec3 start = worldToLocal(box, worldSegment.start);
    const Vec3 end = worldToLocal(box, worldSegment.end);
    const Vec3 direction = end - start;

    const auto distanceSquaredAt = [&](double parameter) noexcept {
        const Vec3 point = start + direction * parameter;
        return (point - closestPointOnBox(point, boxBounds)).lengthSquared();
    };

    double lower = 0.0;
    double upper = 1.0;
    for (std::size_t iteration = 0; iteration < 32; ++iteration) {
        const double firstParameter = lower + (upper - lower) / 3.0;
        const double secondParameter = upper - (upper - lower) / 3.0;
        if (distanceSquaredAt(firstParameter) < distanceSquaredAt(secondParameter)) {
            upper = secondParameter;
        } else {
            lower = firstParameter;
        }
    }

    const double parameter = (lower + upper) * 0.5;
    const Vec3 localPoint = start + direction * parameter;
    const Vec3 localClosest = closestPointOnBox(localPoint, boxBounds);
    const Vec3 localSeparation = localPoint - localClosest;
    const double distanceSquared = localSeparation.lengthSquared();
    if (distanceSquared >= capsule.radius * capsule.radius) {
        return std::nullopt;
    }

    const double distance = std::sqrt(distanceSquared);
    if (distance > std::numeric_limits<double>::epsilon()) {
        return Contact{
            box.settings.orientation.rotate(localSeparation / distance),
            localToWorld(box, localClosest),
            capsule.radius - distance,
        };
    }

    const double positiveX = boxBounds.maximum.x - localPoint.x;
    const double negativeX = localPoint.x - boxBounds.minimum.x;
    const double positiveY = boxBounds.maximum.y - localPoint.y;
    const double negativeY = localPoint.y - boxBounds.minimum.y;
    const double positiveZ = boxBounds.maximum.z - localPoint.z;
    const double negativeZ = localPoint.z - boxBounds.minimum.z;

    double distanceToFace = positiveX;
    Vec3 localNormal{1.0, 0.0, 0.0};
    if (negativeX < distanceToFace) {
        distanceToFace = negativeX;
        localNormal = {-1.0, 0.0, 0.0};
    }
    if (positiveY < distanceToFace) {
        distanceToFace = positiveY;
        localNormal = {0.0, 1.0, 0.0};
    }
    if (negativeY < distanceToFace) {
        distanceToFace = negativeY;
        localNormal = {0.0, -1.0, 0.0};
    }
    if (positiveZ < distanceToFace) {
        distanceToFace = positiveZ;
        localNormal = {0.0, 0.0, 1.0};
    }
    if (negativeZ < distanceToFace) {
        distanceToFace = negativeZ;
        localNormal = {0.0, 0.0, -1.0};
    }

    Vec3 localContact = localPoint;
    if (localNormal.x > 0.5) {
        localContact.x = boxBounds.maximum.x;
    } else if (localNormal.x < -0.5) {
        localContact.x = boxBounds.minimum.x;
    } else if (localNormal.y > 0.5) {
        localContact.y = boxBounds.maximum.y;
    } else if (localNormal.y < -0.5) {
        localContact.y = boxBounds.minimum.y;
    } else if (localNormal.z > 0.5) {
        localContact.z = boxBounds.maximum.z;
    } else {
        localContact.z = boxBounds.minimum.z;
    }

    return Contact{
        box.settings.orientation.rotate(localNormal),
        localToWorld(box, localContact),
        capsule.radius + distanceToFace,
    };
}

std::optional<Contact> sphereHeightfield(const Body& sphere, const Body& field) noexcept {
    const HeightSample sample = sampleHeightfield(field, sphere.settings.position);
    const Vec3 separation = sphere.settings.position - sample.point;
    const double distance = separation.length();

    if (distance >= sphere.radius) {
        return std::nullopt;
    }

    if (distance > std::numeric_limits<double>::epsilon()) {
        // Center above the surface: standard deepest-point contact.
        const Vec3 normal = separation / distance;
        return Contact{normal, sample.point, sphere.radius - distance};
    }

    // Center at/below the surface: push up along the terrain normal.
    return Contact{sample.normal, sample.point, sphere.radius};
}

std::optional<Contact> boxHeightfield(const Body& box, const Body& field) noexcept {
    // Vertex-based test: sample the terrain under all 8 corners plus the center.
    // Unlike a single "downward face" sweep this cannot miss the surface when the
    // box is tilted on a steep slope, which is what made cars sink through hills.
    const Vec3 up = field.terrainDescription.orientation.normalized().rotate({0.0, 0.0, 1.0});
    const double hx = box.halfExtents.x;
    const double hy = box.halfExtents.y;
    const double hz = box.halfExtents.z;
    const Vec3 localPoints[9] = {
        {-hx, -hy, -hz}, {hx, -hy, -hz}, {-hx, hy, -hz}, {hx, hy, -hz},
        {-hx, -hy, hz}, {hx, -hy, hz}, {-hx, hy, hz}, {hx, hy, hz},
        {0.0, 0.0, 0.0},
    };

    std::optional<Contact> best;
    double deepest = 0.0;
    for (const Vec3& localPoint : localPoints) {
        const Vec3 worldPoint = localToWorld(box, localPoint);
        const HeightSample sample = sampleHeightfield(field, worldPoint);
        const double penetration = dot(sample.point - worldPoint, up);
        if (penetration > deepest) {
            deepest = penetration;
            best = Contact{sample.normal, worldPoint, penetration};
        }
    }
    return best;
}

std::optional<Contact> capsuleHeightfield(const Body& capsule, const Body& field) noexcept {
    // Sample along the capsule segment; deepest sphere-style contact wins.
    const Segment segment = capsuleSegment(capsule);
    std::optional<Contact> best;
    double deepest = 0.0;

    constexpr std::size_t sampleCount = 8;
    for (std::size_t index = 0; index < sampleCount; ++index) {
        const double parameter = static_cast<double>(index) / static_cast<double>(sampleCount - 1);
        const Vec3 center = segment.start + (segment.end - segment.start) * parameter;
        const HeightSample sample = sampleHeightfield(field, center);
        const Vec3 separation = center - sample.point;
        const double distance = separation.length();
        if (distance >= capsule.radius) {
            continue;
        }
        const double depth = distance > std::numeric_limits<double>::epsilon()
            ? capsule.radius - distance
            : capsule.radius;
        const Vec3 normal = distance > std::numeric_limits<double>::epsilon()
            ? separation / distance
            : sample.normal;
        if (depth > deepest) {
            deepest = depth;
            best = Contact{normal, sample.point, depth};
        }
    }
    return best;
}

// Multi-contact generation for Box-Box and Box-Heightfield (Manifolds)
void boxBoxManifold(const Body& first, const Body& second, std::vector<Contact>& contacts) noexcept {
    // SAT over the 15 candidate axes to find the minimum-penetration axis. The
    // axis also tells which body owns the reference face; the other body
    // contributes its incident face, which is clipped to the reference face to
    // produce up to four contacts with correct per-point depths. This replaces
    // the old "one SAT depth smeared over corner points" manifold that made
    // crates pop and jitter.
    const std::array<Vec3, 3> firstAxes = boxAxes(first);
    const std::array<Vec3, 3> secondAxes = boxAxes(second);
    const Vec3 centerSeparation = second.settings.position - first.settings.position;

    double smallestOverlap = std::numeric_limits<double>::infinity();
    Vec3 contactNormal{};
    bool normalFromFirst = true;
    bool edgeAxis = false;
    bool separated = false;

    const auto testAxis = [&](const Vec3& candidate, bool fromFirst, bool isEdge) noexcept {
        const double lengthSquared = candidate.lengthSquared();
        if (lengthSquared <= std::numeric_limits<double>::epsilon()) {
            return;
        }
        const Vec3 axis = candidate / std::sqrt(lengthSquared);
        const double distance = dot(centerSeparation, axis);
        const double overlap = projectedRadius(first, firstAxes, axis) +
                               projectedRadius(second, secondAxes, axis) - std::abs(distance);
        if (overlap <= 0.0) {
            separated = true;
            return;
        }
        if (overlap < smallestOverlap) {
            smallestOverlap = overlap;
            contactNormal = axis * (distance >= 0.0 ? 1.0 : -1.0); // points first -> second
            normalFromFirst = fromFirst;
            edgeAxis = isEdge;
        }
    };

    for (const Vec3& axis : firstAxes) {
        testAxis(axis, true, false);
    }
    for (const Vec3& axis : secondAxes) {
        testAxis(axis, false, false);
    }
    for (const Vec3& firstAxis : firstAxes) {
        for (const Vec3& secondAxis : secondAxes) {
            testAxis(cross(firstAxis, secondAxis), true, true);
        }
    }

    if (separated || !std::isfinite(smallestOverlap)) {
        return;
    }
    if (edgeAxis) {
        // Edge-edge is rare; fall back to the single deepest-point contact.
        const std::optional<Contact> single = boxBox(first, second);
        if (single.has_value()) {
            contacts.push_back(*single);
        }
        return;
    }

    // Reference body owns the face; incident body is clipped against it.
    const Body& reference = normalFromFirst ? first : second;
    const Body& incident = normalFromFirst ? second : first;
    const std::array<Vec3, 3> referenceAxes = normalFromFirst ? firstAxes : secondAxes;
    const std::array<Vec3, 3> incidentAxes = normalFromFirst ? secondAxes : firstAxes;
    const Vec3 referenceOutward = normalFromFirst ? contactNormal : contactNormal * -1.0;

    // Reference face: the local axis most aligned with the outward direction.
    std::size_t referenceAxis = 0;
    double bestAlignment = -2.0;
    for (std::size_t index = 0; index < 3; ++index) {
        const double alignment = dot(referenceAxes[index], referenceOutward);
        if (std::abs(alignment) > bestAlignment) {
            bestAlignment = std::abs(alignment);
            referenceAxis = index;
        }
    }
    const double referenceSign = dot(referenceAxes[referenceAxis], referenceOutward) >= 0.0 ? 1.0 : -1.0;
    const Vec3 referenceFaceNormal = referenceAxes[referenceAxis] * referenceSign;
    const double referenceExtent =
        referenceAxis == 0 ? reference.halfExtents.x : referenceAxis == 1 ? reference.halfExtents.y : reference.halfExtents.z;
    const Vec3 referenceFaceCenter = reference.settings.position + referenceFaceNormal * referenceExtent;
    const std::size_t referenceTangentA = (referenceAxis + 1) % 3;
    const std::size_t referenceTangentB = (referenceAxis + 2) % 3;
    const Vec3 referenceTangentAVector = referenceAxes[referenceTangentA];
    const Vec3 referenceTangentBVector = referenceAxes[referenceTangentB];
    const double referenceTangentAExtent =
        referenceTangentA == 0 ? reference.halfExtents.x : referenceTangentA == 1 ? reference.halfExtents.y : reference.halfExtents.z;
    const double referenceTangentBExtent =
        referenceTangentB == 0 ? reference.halfExtents.x : referenceTangentB == 1 ? reference.halfExtents.y : reference.halfExtents.z;

    // Incident face: the box face most opposed to the reference outward plane.
    // Both signs of every local axis are candidates, otherwise a face pointing
    // along a negative local axis would never be selected.
    std::size_t incidentAxis = 0;
    double incidentSign = 1.0;
    double worstAlignment = 2.0;
    for (std::size_t index = 0; index < 3; ++index) {
        const double alignment = dot(incidentAxes[index], referenceFaceNormal);
        if (alignment < worstAlignment) {
            worstAlignment = alignment;
            incidentAxis = index;
            incidentSign = 1.0;
        }
        if (-alignment < worstAlignment) {
            worstAlignment = -alignment;
            incidentAxis = index;
            incidentSign = -1.0;
        }
    }
    const Vec3 incidentFaceNormal = incidentAxes[incidentAxis] * incidentSign;
    const double incidentExtent =
        incidentAxis == 0 ? incident.halfExtents.x : incidentAxis == 1 ? incident.halfExtents.y : incident.halfExtents.z;
    const Vec3 incidentFaceCenter = incident.settings.position + incidentFaceNormal * incidentExtent;
    const std::size_t incidentTangentA = (incidentAxis + 1) % 3;
    const std::size_t incidentTangentB = (incidentAxis + 2) % 3;
    const Vec3 incidentTangentAVector = incidentAxes[incidentTangentA];
    const Vec3 incidentTangentBVector = incidentAxes[incidentTangentB];
    const double incidentTangentAExtent =
        incidentTangentA == 0 ? incident.halfExtents.x : incidentTangentA == 1 ? incident.halfExtents.y : incident.halfExtents.z;
    const double incidentTangentBExtent =
        incidentTangentB == 0 ? incident.halfExtents.x : incidentTangentB == 1 ? incident.halfExtents.y : incident.halfExtents.z;

    // Incident face as a quad, then Sutherland-Hodgman clip to the side planes
    // of the reference face.
    std::vector<Vec3> polygon = {
        incidentFaceCenter + incidentTangentAVector * incidentTangentAExtent + incidentTangentBVector * incidentTangentBExtent,
        incidentFaceCenter - incidentTangentAVector * incidentTangentAExtent + incidentTangentBVector * incidentTangentBExtent,
        incidentFaceCenter - incidentTangentAVector * incidentTangentAExtent - incidentTangentBVector * incidentTangentBExtent,
        incidentFaceCenter + incidentTangentAVector * incidentTangentAExtent - incidentTangentBVector * incidentTangentBExtent,
    };

    const auto clipAgainst = [](std::vector<Vec3>& points, const Vec3& planeNormal, double planeOffset) noexcept {
        std::vector<Vec3> output;
        const std::size_t count = points.size();
        for (std::size_t index = 0; index < count; ++index) {
            const Vec3& current = points[index];
            const Vec3& next = points[(index + 1) % count];
            const double currentDistance = dot(planeNormal, current) - planeOffset;
            const double nextDistance = dot(planeNormal, next) - planeOffset;
            if (currentDistance <= 0.0) {
                output.push_back(current);
            }
            if ((currentDistance < 0.0 && nextDistance > 0.0) ||
                (currentDistance > 0.0 && nextDistance < 0.0)) {
                const double parameter = currentDistance / (currentDistance - nextDistance);
                output.push_back(current + (next - current) * parameter);
            }
        }
        points = std::move(output);
    };

    clipAgainst(polygon, referenceTangentAVector, dot(referenceTangentAVector, referenceFaceCenter) + referenceTangentAExtent);
    clipAgainst(polygon, referenceTangentAVector * -1.0, dot(referenceTangentAVector * -1.0, referenceFaceCenter) + referenceTangentAExtent);
    clipAgainst(polygon, referenceTangentBVector, dot(referenceTangentBVector, referenceFaceCenter) + referenceTangentBExtent);
    clipAgainst(polygon, referenceTangentBVector * -1.0, dot(referenceTangentBVector * -1.0, referenceFaceCenter) + referenceTangentBExtent);

    for (const Vec3& point : polygon) {
        const double separation = dot(referenceFaceNormal, point - referenceFaceCenter);
        if (separation > 0.0) {
            continue; // in front of the reference face: not touching
        }
        contacts.emplace_back(contactNormal, point, -separation);
    }

    if (contacts.empty()) {
        const std::optional<Contact> single = boxBox(first, second);
        if (single.has_value()) {
            contacts.push_back(*single);
        }
    }
}

void boxHeightfieldManifold(const Body& box, const Body& field, std::vector<Contact>& contacts) noexcept {
    // Same vertex set as boxHeightfield, but keep every penetrating point so a
    // resting box gets its supporting corners instead of a single contact.
    const Vec3 up = field.terrainDescription.orientation.normalized().rotate({0.0, 0.0, 1.0});
    const double hx = box.halfExtents.x;
    const double hy = box.halfExtents.y;
    const double hz = box.halfExtents.z;
    const Vec3 localPoints[9] = {
        {-hx, -hy, -hz}, {hx, -hy, -hz}, {-hx, hy, -hz}, {hx, hy, -hz},
        {-hx, -hy, hz}, {hx, -hy, hz}, {-hx, hy, hz}, {hx, hy, hz},
        {0.0, 0.0, 0.0},
    };

    for (const Vec3& localPoint : localPoints) {
        const Vec3 worldPoint = localToWorld(box, localPoint);
        const HeightSample sample = sampleHeightfield(field, worldPoint);
        const double penetration = dot(sample.point - worldPoint, up);
        if (penetration > 0.0) {
            contacts.emplace_back(sample.normal, worldPoint, penetration);
        }
    }
}

} // namespace apr
} // namespace Aporia
