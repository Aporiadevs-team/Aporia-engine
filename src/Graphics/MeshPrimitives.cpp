#include "Aporia/Graphics/MeshPrimitives.hpp"

#include <cmath>

namespace Aporia {
namespace apr {

namespace {

void addQuad(MeshData& mesh,
             const MeshVertex& a,
             const MeshVertex& b,
             const MeshVertex& c,
             const MeshVertex& d) {
    const std::uint32_t base = static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.insert(mesh.vertices.end(), {a, b, c, d});
    mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
}

MeshVertex vertex(const float position[3], const float normal[3], float u, float v) {
    MeshVertex result;
    result.position[0] = position[0];
    result.position[1] = position[1];
    result.position[2] = position[2];
    result.normal[0] = normal[0];
    result.normal[1] = normal[1];
    result.normal[2] = normal[2];
    result.uv[0] = u;
    result.uv[1] = v;
    return result;
}

}

MeshData makeBoxMesh(float halfX, float halfY, float halfZ) {
    MeshData mesh;
    mesh.vertices.reserve(24);
    mesh.indices.reserve(36);

    const float px[3] = {halfX, 0.0f, 0.0f};
    const float nx[3] = {-halfX, 0.0f, 0.0f};
    const float py[3] = {0.0f, halfY, 0.0f};
    const float ny[3] = {0.0f, -halfY, 0.0f};
    const float pz[3] = {0.0f, 0.0f, halfZ};
    const float nz[3] = {0.0f, 0.0f, -halfZ};

    const auto face = [&](const float normal[3], const float uAxis[3], const float vAxis[3]) {
        const float center[3] = {normal[0] * halfX, normal[1] * halfY, normal[2] * halfZ};
        const float corners[4][3] = {
            {center[0] - uAxis[0] - vAxis[0], center[1] - uAxis[1] - vAxis[1], center[2] - uAxis[2] - vAxis[2]},
            {center[0] + uAxis[0] - vAxis[0], center[1] + uAxis[1] - vAxis[1], center[2] + uAxis[2] - vAxis[2]},
            {center[0] + uAxis[0] + vAxis[0], center[1] + uAxis[1] + vAxis[1], center[2] + uAxis[2] + vAxis[2]},
            {center[0] - uAxis[0] + vAxis[0], center[1] - uAxis[1] + vAxis[1], center[2] - uAxis[2] + vAxis[2]},
        };
        addQuad(mesh,
            vertex(corners[0], normal, 0.0f, 0.0f),
            vertex(corners[1], normal, 1.0f, 0.0f),
            vertex(corners[2], normal, 1.0f, 1.0f),
            vertex(corners[3], normal, 0.0f, 1.0f));
    };

    face(pz, py, px);
    face(nz, px, py);
    face(py, px, pz);
    face(ny, pz, px);
    face(px, pz, py);
    face(nx, py, pz);

    return mesh;
}

MeshData makeSphereMesh(float radius, std::uint32_t segments, std::uint32_t rings) {
    MeshData mesh;
    mesh.vertices.reserve(static_cast<std::size_t>((rings + 1) * (segments + 1)));

    constexpr float pi = 3.14159265358979323846f;
    for (std::uint32_t ring = 0; ring <= rings; ++ring) {
        const float phi = pi * static_cast<float>(ring) / static_cast<float>(rings);
        const float sinPhi = std::sin(phi);
        const float cosPhi = std::cos(phi);
        for (std::uint32_t segment = 0; segment <= segments; ++segment) {
            const float theta = 2.0f * pi * static_cast<float>(segment) / static_cast<float>(segments);
            const float sinTheta = std::sin(theta);
            const float cosTheta = std::cos(theta);

            const float x = cosTheta * sinPhi;
            const float y = sinTheta * sinPhi;
            const float z = cosPhi;

            MeshVertex v;
            v.position[0] = radius * x;
            v.position[1] = radius * y;
            v.position[2] = radius * z;
            v.normal[0] = x;
            v.normal[1] = y;
            v.normal[2] = z;
            v.uv[0] = static_cast<float>(segment) / static_cast<float>(segments);
            v.uv[1] = static_cast<float>(ring) / static_cast<float>(rings);
            mesh.vertices.push_back(v);
        }
    }

    for (std::uint32_t ring = 0; ring < rings; ++ring) {
        for (std::uint32_t segment = 0; segment < segments; ++segment) {
            const std::uint32_t first = ring * (segments + 1) + segment;
            const std::uint32_t second = first + segments + 1;
            mesh.indices.insert(mesh.indices.end(), {first, second, first + 1, first + 1, second, second + 1});
        }
    }

    return mesh;
}

MeshData makePlaneMesh(float halfX, float halfY) {
    MeshData mesh;
    const float n[3] = {0.0f, 0.0f, 1.0f};
    const float a[3] = {-halfX, -halfY, 0.0f};
    const float b[3] = {halfX, -halfY, 0.0f};
    const float c[3] = {halfX, halfY, 0.0f};
    const float d[3] = {-halfX, halfY, 0.0f};
    addQuad(mesh, vertex(a, n, 0.0f, 0.0f), vertex(b, n, 1.0f, 0.0f), vertex(c, n, 1.0f, 1.0f), vertex(d, n, 0.0f, 1.0f));
    return mesh;
}

MeshData makeCapsuleMesh(float radius, float halfHeight, std::uint32_t segments, std::uint32_t rings) {
    MeshData mesh;
    mesh.vertices.reserve(static_cast<std::size_t>((2 * rings + 2) * (segments + 1)));

    constexpr float pi = 3.14159265358979323846f;
    for (std::uint32_t ring = 0; ring <= rings; ++ring) {
        const float phi = 0.5f * pi * static_cast<float>(ring) / static_cast<float>(rings);
        const float sinPhi = std::sin(phi);
        const float cosPhi = std::cos(phi);
        for (std::uint32_t segment = 0; segment <= segments; ++segment) {
            const float theta = 2.0f * pi * static_cast<float>(segment) / static_cast<float>(segments);
            MeshVertex v;
            v.position[0] = radius * std::cos(theta) * sinPhi;
            v.position[1] = radius * std::sin(theta) * sinPhi;
            v.position[2] = halfHeight + radius * cosPhi;
            v.normal[0] = std::cos(theta) * sinPhi;
            v.normal[1] = std::sin(theta) * sinPhi;
            v.normal[2] = cosPhi;
            v.uv[0] = static_cast<float>(segment) / static_cast<float>(segments);
            v.uv[1] = static_cast<float>(ring) / static_cast<float>(2 * rings);
            mesh.vertices.push_back(v);
        }
    }
    for (std::uint32_t ring = 0; ring <= rings; ++ring) {
        const float phi = 0.5f * pi * (1.0f - static_cast<float>(ring) / static_cast<float>(rings));
        const float sinPhi = std::sin(phi);
        const float cosPhi = std::cos(phi);
        for (std::uint32_t segment = 0; segment <= segments; ++segment) {
            const float theta = 2.0f * pi * static_cast<float>(segment) / static_cast<float>(segments);
            MeshVertex v;
            v.position[0] = radius * std::cos(theta) * sinPhi;
            v.position[1] = radius * std::sin(theta) * sinPhi;
            v.position[2] = -halfHeight - radius * cosPhi;
            v.normal[0] = std::cos(theta) * sinPhi;
            v.normal[1] = std::sin(theta) * sinPhi;
            v.normal[2] = -cosPhi;
            v.uv[0] = static_cast<float>(segment) / static_cast<float>(segments);
            v.uv[1] = 0.5f + static_cast<float>(ring) / static_cast<float>(2 * rings);
            mesh.vertices.push_back(v);
        }
    }

    const std::uint32_t rowSize = segments + 1;
    const std::uint32_t topRows = rings + 1;
    const auto addRow = [&](std::uint32_t rowIndex, std::uint32_t segmentsCount) {
        for (std::uint32_t segment = 0; segment < segmentsCount; ++segment) {
            const std::uint32_t first = rowIndex * rowSize + segment;
            const std::uint32_t second = first + rowSize;
            mesh.indices.insert(mesh.indices.end(), {first, second, first + 1, first + 1, second, second + 1});
        }
    };
    for (std::uint32_t ring = 0; ring < topRows - 1 + 1; ++ring) {
        addRow(ring, segments);
    }
    addRow(topRows - 1, segments);

    return mesh;
}

}
}
