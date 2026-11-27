#pragma once

#include "Aporia/Core/Export.hpp"
#include "Aporia/Math/Quaternion.hpp"
#include "Aporia/Math/Vector3.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Aporia {

// Deterministic heightfield terrain: value noise + fractal Brownian motion.
// Field-local space is Z-up (heights along local +Z), grid spans
// [-worldSize/2, +worldSize/2] on local X/Y. The transform places it in the
// world; e.g. a -90° rotation about X turns it into a Y-up heightline for
// XY-plane 2D worlds. No asset files involved.
struct TerrainDesc {
    std::uint64_t seed = 1;
    std::uint32_t gridResolution = 64; // vertices per side, >= 2
    float worldSize = 64.0f;          // meters per side, centered at origin
    float heightScale = 4.0f;         // max height above the offset
    float heightOffset = 0.0f;        // base height added to the whole field
    float noiseFrequency = 3.0f;      // base feature count across the world
    std::uint32_t noiseOctaves = 4;   // fBm octaves, >= 1

    Vec3 position{};                  // world placement of the field origin
    Quat orientation{};               // identity keeps local Z-up = world Z-up
};

struct TerrainHeightfield {
    std::uint32_t resolution = 0; // vertices per side
    float worldSize = 0.0f;
    float minHeight = 0.0f;
    float maxHeight = 0.0f;
    std::vector<float> heights;   // row-major [y * resolution + x], x/y grid indices

    [[nodiscard]] const float* data() const noexcept { return heights.data(); }
    [[nodiscard]] std::size_t size() const noexcept { return heights.size(); }
};

// Generates the heightfield from the descriptor. Deterministic for a given seed.
[[nodiscard]] APORIA_API TerrainHeightfield generateTerrainHeightfield(const TerrainDesc& description);

// Surface elevation along the field's up axis at the planar spot of worldPoint
// (descriptor transform applied). Works for any placement: the point is
// un-rotated into field space, so a Y-up 2D terrain and a Z-up 3D terrain both
// resolve correctly. Outside the field the border heights extend outward.
[[nodiscard]] APORIA_API double terrainHeightAt(
    const TerrainDesc& description,
    const TerrainHeightfield& field,
    const Vec3& worldPoint);

// Builds the renderable grid mesh from the heightfield, in field-local space
// (Z-up). Caller places it with the descriptor transform.
struct MeshData;
[[nodiscard]] APORIA_API MeshData makeTerrainMesh(const TerrainHeightfield& field);

}
