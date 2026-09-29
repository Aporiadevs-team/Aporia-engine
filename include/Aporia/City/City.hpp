#pragma once

#include "Aporia/Core/Export.hpp"
#include "Aporia/Graphics/Graphics.hpp"
#include "Aporia/Math/Vector3.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace Aporia {

// Infinite world of settlements: procedural cities, towns and villages
// scattered across open country, connected by curved highways. Streets inside
// settlements are radial-ring networks with per-settlement rotation and
// jitter — no global grid. The world streams as 256 m chunks around the
// camera and is deterministic per seed. No asset files.
struct CityDesc {
    std::uint64_t seed = 1;
    float settlementCell = 1800.0f;    // mean spacing between settlements (m)
    float settlementChance = 0.45f;    // probability a cell hosts a settlement
    float highwayWidth = 16.0f;
    float avenueWidth = 11.0f;         // main streets / central rings
    float roadWidth = 7.0f;            // local streets and country lanes
    float sidewalkWidth = 4.0f;        // yard ring around building footprints
    std::uint32_t buildingLayers = 6;  // max tower roof steps
    float chunkSize = 256.0f;          // streaming tile edge (m)
};

enum class CityZone : std::uint8_t {
    Field,     // open country
    Highway,   // inter-settlement highway
    Avenue,    // main street / central ring
    Street,    // local street or lane
    Sidewalk,  // settlement yard ground
    Lot,       // building footprint
    Park,
};

enum class CityDistrict : std::uint8_t {
    Downtown = 0,   // city tower core
    Core = 1,       // city/town mid-rise belt
    Residential = 2,// house belts of cities and towns
    Outskirts = 3,  // village ground
    Countryside = 4,// no settlement
};

struct CitySample {
    CityZone zone = CityZone::Field;
    CityDistrict district = CityDistrict::Countryside;
};

class APORIA_API CityWorld {
public:
    explicit CityWorld(const CityDesc& description);
    ~CityWorld();

    CityWorld(const CityWorld&) = delete;
    CityWorld& operator=(const CityWorld&) = delete;

    // Streams chunks around (centerX, centerY); creates/destroys chunk meshes.
    // graphics may be null (headless sampling only).
    void update(double centerX, double centerY, double viewRadius, Graphics* graphics);

    // Zone + district at a world point (settlement layouts generate on demand).
    [[nodiscard]] CitySample sampleAt(double x, double y);

    // Ground elevation (curb steps) at a world point.
    [[nodiscard]] double groundAt(double x, double y);

    struct Visible {
        std::int64_t ci = 0;   // chunk x index
        std::int64_t cj = 0;   // chunk y index
        MeshHandle mesh;
    };
    // Chunks currently loaded with meshes (model = identity).
    [[nodiscard]] std::vector<Visible> visibleChunks() const;

    [[nodiscard]] const CityDesc& description() const noexcept;
    [[nodiscard]] std::size_t loadedChunkCount() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
