#include "Aporia/Terrain/Terrain.hpp"

#include "Aporia/Graphics/Graphics.hpp"
#include "Aporia/Graphics/MeshPrimitives.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace Aporia {
namespace {

// 2D value noise with a deterministic integer hash; smooth (quintic) fade.
double hashValue(std::uint64_t seed, std::int64_t x, std::int64_t y) noexcept {
    std::uint64_t h = seed;
    h ^= static_cast<std::uint64_t>(x) * 0x9E3779B97F4A7C15ULL;
    h ^= static_cast<std::uint64_t>(y) * 0xC2B2AE3D27D4EB4FULL;
    h ^= h >> 27;
    h *= 0x87C37B91114253D5ULL;
    h ^= h >> 33;
    h *= 0x4CF5AD432745937FULL;
    h ^= h >> 29;
    // Map to [-1, 1] with 53 mantissa bits of entropy.
    return static_cast<double>(h & ((1ULL << 53) - 1)) / static_cast<double>(1ULL << 53) * 2.0 - 1.0;
}

double fade(double t) noexcept {
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

double valueNoise(std::uint64_t seed, double x, double y) noexcept {
    const double floorX = std::floor(x);
    const double floorY = std::floor(y);
    const double tx = fade(x - floorX);
    const double ty = fade(y - floorY);
    const std::int64_t ix = static_cast<std::int64_t>(floorX);
    const std::int64_t iy = static_cast<std::int64_t>(floorY);

    const double v00 = hashValue(seed, ix, iy);
    const double v10 = hashValue(seed, ix + 1, iy);
    const double v01 = hashValue(seed, ix, iy + 1);
    const double v11 = hashValue(seed, ix + 1, iy + 1);
    const double a = v00 + (v10 - v00) * tx;
    const double b = v01 + (v11 - v01) * tx;
    return a + (b - a) * ty;
}

double fractalNoise(
    std::uint64_t seed,
    double x,
    double y,
    std::uint32_t octaves) noexcept {
    double amplitude = 1.0;
    double frequency = 1.0;
    double sum = 0.0;
    double normalization = 0.0;
    for (std::uint32_t octave = 0; octave < octaves; ++octave) {
        sum += valueNoise(seed + octave * 0x517CC1B727220A95ULL, x * frequency, y * frequency) * amplitude;
        normalization += amplitude;
        amplitude *= 0.5;
        frequency *= 2.0;
    }
    return normalization > 0.0 ? sum / normalization : 0.0;
}

double localHeight(const TerrainHeightfield& field, std::int64_t gridX, std::int64_t gridY) noexcept {
    const std::int64_t clampedX = std::clamp<std::int64_t>(gridX, 0, static_cast<std::int64_t>(field.resolution) - 1);
    const std::int64_t clampedY = std::clamp<std::int64_t>(gridY, 0, static_cast<std::int64_t>(field.resolution) - 1);
    return field.heights[static_cast<std::size_t>(clampedY) * field.resolution + clampedX];
}

// Height along the up axis at local (x, y) in meters, bilinear.
double heightAtLocal(const TerrainHeightfield& field, double localX, double localY) noexcept {
    if (field.resolution < 2 || field.worldSize <= 0.0f) {
        return 0.0;
    }

    const double cell = static_cast<double>(field.worldSize) / static_cast<double>(field.resolution - 1);
    const double gx = (localX + field.worldSize * 0.5) / cell;
    const double gy = (localY + field.worldSize * 0.5) / cell;
    const std::int64_t ix = static_cast<std::int64_t>(std::floor(gx));
    const std::int64_t iy = static_cast<std::int64_t>(std::floor(gy));
    const double tx = gx - std::floor(gx);
    const double ty = gy - std::floor(gy);

    const double h00 = localHeight(field, ix, iy);
    const double h10 = localHeight(field, ix + 1, iy);
    const double h01 = localHeight(field, ix, iy + 1);
    const double h11 = localHeight(field, ix + 1, iy + 1);
    const double a = h00 + (h10 - h00) * tx;
    const double b = h01 + (h11 - h01) * tx;
    return a + (b - a) * ty;
}

}

TerrainHeightfield generateTerrainHeightfield(const TerrainDesc& description) {
    TerrainHeightfield field;
    field.resolution = std::max<std::uint32_t>(2, description.gridResolution);
    field.worldSize = description.worldSize;
    field.heights.assign(static_cast<std::size_t>(field.resolution) * field.resolution, 0.0f);

    const std::uint32_t octaves = std::max<std::uint32_t>(1, description.noiseOctaves);
    float minimum = std::numeric_limits<float>::infinity();
    float maximum = -std::numeric_limits<float>::infinity();
    for (std::uint32_t gy = 0; gy < field.resolution; ++gy) {
        for (std::uint32_t gx = 0; gx < field.resolution; ++gx) {
            // Map grid to noise domain: noiseFrequency features across the world.
            const double nx = static_cast<double>(gx) / static_cast<double>(field.resolution - 1);
            const double ny = static_cast<double>(gy) / static_cast<double>(field.resolution - 1);
            const double noise = fractalNoise(description.seed, nx * description.noiseFrequency, ny * description.noiseFrequency, octaves);
            const double height = description.heightOffset + noise * description.heightScale;
            const float stored = static_cast<float>(height);
            field.heights[static_cast<std::size_t>(gy) * field.resolution + gx] = stored;
            minimum = std::min(minimum, stored);
            maximum = std::max(maximum, stored);
        }
    }

    field.minHeight = minimum;
    field.maxHeight = maximum;
    return field;
}

double terrainHeightAt(
    const TerrainDesc& description,
    const TerrainHeightfield& field,
    const Vec3& worldPoint) {
    if (field.heights.empty() || field.resolution < 2) {
        return 0.0;
    }

    // Un-rotate the query point into field-local space; the field is Z-up
    // locally, so local z is the height axis and local x/y the planar axes.
    const Quat orientation = description.orientation.normalized();
    const Vec3 local = orientation.inverseRotate(worldPoint - description.position);
    const double elevation = heightAtLocal(field, local.x, local.y);
    // Surface coordinate along the world up axis: base plane + elevation.
    // Identity placement -> world Z; -90° X rotation (2D) -> world Y.
    return dot(description.position, orientation.rotate({0.0, 0.0, 1.0})) + elevation;
}

MeshData makeTerrainMesh(const TerrainHeightfield& field) {
    MeshData mesh;
    if (field.resolution < 2 || field.heights.size() < static_cast<std::size_t>(field.resolution) * field.resolution) {
        return mesh;
    }

    const std::uint32_t resolution = field.resolution;
    const float worldSize = field.worldSize;
    const float half = worldSize * 0.5f;
    const float cell = worldSize / static_cast<float>(resolution - 1);

    mesh.vertices.reserve(static_cast<std::size_t>(resolution) * resolution);
    for (std::uint32_t gy = 0; gy < resolution; ++gy) {
        for (std::uint32_t gx = 0; gx < resolution; ++gx) {
            const float x = -half + static_cast<float>(gx) * cell;
            const float y = -half + static_cast<float>(gy) * cell;
            const float z = field.heights[static_cast<std::size_t>(gy) * resolution + gx];

            // Central-difference normals; borders use one-sided differences.
            const float left = field.heights[static_cast<std::size_t>(gy) * resolution + std::max(gx, 1u) - 1];
            const float right = field.heights[static_cast<std::size_t>(gy) * resolution + std::min(gx + 1u, resolution - 1)];
            const float down = field.heights[static_cast<std::size_t>(std::max(gy, 1u) - 1) * resolution + gx];
            const float up = field.heights[static_cast<std::size_t>(std::min(gy + 1u, resolution - 1)) * resolution + gx];

            Vec3 normal = Vec3{
                -(right - left) * static_cast<double>(half) / cell,
                -(up - down) * static_cast<double>(half) / cell,
                2.0 * static_cast<double>(half),
            }.normalized();
            if (normal.lengthSquared() <= std::numeric_limits<double>::epsilon()) {
                normal = {0.0, 0.0, 1.0};
            }

            MeshVertex vertex{};
            vertex.position[0] = x;
            vertex.position[1] = y;
            vertex.position[2] = z;
            vertex.normal[0] = static_cast<float>(normal.x);
            vertex.normal[1] = static_cast<float>(normal.y);
            vertex.normal[2] = static_cast<float>(normal.z);
            vertex.uv[0] = static_cast<float>(gx) / static_cast<float>(resolution - 1);
            vertex.uv[1] = static_cast<float>(gy) / static_cast<float>(resolution - 1);
            mesh.vertices.push_back(vertex);
        }
    }

    mesh.indices.reserve(static_cast<std::size_t>(resolution - 1) * (resolution - 1) * 6);
    for (std::uint32_t gy = 0; gy < resolution - 1; ++gy) {
        for (std::uint32_t gx = 0; gx < resolution - 1; ++gx) {
            const std::uint32_t topLeft = gy * resolution + gx;
            const std::uint32_t topRight = topLeft + 1;
            const std::uint32_t bottomLeft = topLeft + resolution;
            const std::uint32_t bottomRight = bottomLeft + 1;
            mesh.indices.insert(mesh.indices.end(), {
                topLeft, bottomLeft, bottomRight,
                topLeft, bottomRight, topRight,
            });
        }
    }

    return mesh;
}

}
