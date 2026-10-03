#include "Aporia/City/City.hpp"

#include "Aporia/Graphics/MeshPrimitives.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <unordered_map>

namespace Aporia {
namespace {

constexpr double kPi = 3.14159265358979323846;

double hashUnit(std::uint64_t seed, std::int64_t a, std::int64_t b, std::int64_t c = 0) noexcept {
    std::uint64_t h = seed;
    h ^= static_cast<std::uint64_t>(a) * 0x9E3779B97F4A7C15ULL;
    h ^= static_cast<std::uint64_t>(b) * 0xC2B2AE3D27D4EB4FULL;
    h ^= static_cast<std::uint64_t>(c) * 0xD6E8FEB86659FD93ULL;
    h ^= h >> 27;
    h *= 0x87C37B91114253D5ULL;
    h ^= h >> 33;
    h *= 0x4CF5AD432745937FULL;
    h ^= h >> 29;
    return static_cast<double>(h & ((1ULL << 53) - 1)) / 9007199254740992.0;
}

struct Rng {
    std::uint64_t state;

    explicit Rng(std::uint64_t seed) noexcept : state(seed) {}

    std::uint64_t next() noexcept {
        std::uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

    double unit() noexcept { return static_cast<double>(next() >> 11) / 9007199254740992.0; }
    double range(double low, double high) noexcept { return low + (high - low) * unit(); }
    bool chance(double p) noexcept { return unit() < p; }
};

enum class SettlementType : std::uint8_t { City, Town, Village };

struct Settlement {
    double x = 0.0;
    double y = 0.0;
    SettlementType type = SettlementType::Village;
    double radius = 200.0;       // built-up radius
    double rotation = 0.0;       // street network rotation
    std::int64_t cellI = 0;
    std::int64_t cellJ = 0;
};

SettlementType typeFor(double roll) noexcept {
    if (roll < 0.18) return SettlementType::City;
    if (roll < 0.55) return SettlementType::Town;
    return SettlementType::Village;
}

double radiusFor(SettlementType type, double roll) noexcept {
    switch (type) {
    case SettlementType::City: return 420.0 + roll * 260.0;
    case SettlementType::Town: return 260.0 + roll * 140.0;
    default: return 140.0 + roll * 90.0;
    }
}

// Settlement lookup: cell grid, one settlement max per cell.
bool settlementAtCell(const CityDesc& d, std::int64_t i, std::int64_t j, Settlement& out) noexcept {
    const double roll = hashUnit(d.seed, i, j, 1);
    if (roll >= d.settlementChance) {
        return false;
    }
    out.cellI = i;
    out.cellJ = j;
    out.x = (static_cast<double>(i) + 0.2 + hashUnit(d.seed, i, j, 2) * 0.6) * d.settlementCell;
    out.y = (static_cast<double>(j) + 0.2 + hashUnit(d.seed, i, j, 3) * 0.6) * d.settlementCell;
    out.type = typeFor(hashUnit(d.seed, i, j, 4));
    out.radius = radiusFor(out.type, hashUnit(d.seed, i, j, 5));
    out.rotation = hashUnit(d.seed, i, j, 6) * 2.0 * kPi;
    return true;
}

// Nearest settlement to a point (searches a 3x3 cell neighborhood).
bool nearestSettlement(const CityDesc& d, double x, double y, Settlement& out) noexcept {
    const double cellX = x / d.settlementCell;
    const double cellY = y / d.settlementCell;
    const std::int64_t baseI = static_cast<std::int64_t>(std::floor(cellX));
    const std::int64_t baseJ = static_cast<std::int64_t>(std::floor(cellY));
    bool found = false;
    double bestDistance = 1.0e18;
    for (std::int64_t di = -1; di <= 1; ++di) {
        for (std::int64_t dj = -1; dj <= 1; ++dj) {
            Settlement candidate;
            if (!settlementAtCell(d, baseI + di, baseJ + dj, candidate)) {
                continue;
            }
            const double dx = candidate.x - x;
            const double dy = candidate.y - y;
            const double distance = std::sqrt(dx * dx + dy * dy);
            if (distance < bestDistance) {
                bestDistance = distance;
                out = candidate;
                found = true;
            }
        }
    }
    return found;
}

// --- Polylines --------------------------------------------------------------
// Roads are polylines in world space; chunks clip them into segments.

struct Polyline {
    std::uint8_t kind = 0; // 0 lane, 1 avenue(main street/ring), 2 highway
    std::vector<double> xs;
    std::vector<double> ys;
};

void appendSegment(std::vector<Polyline>& lines, std::uint8_t kind,
                   double x0, double y0, double x1, double y1) {
    Polyline line;
    line.kind = kind;
    line.xs = {x0, x1};
    line.ys = {y0, y1};
    lines.push_back(std::move(line));
}

// Curved connection between two settlements: quadratic bezier with lateral
// bowing so highways are never ruler-straight.
Polyline highwayBetween(const CityDesc& d, const Settlement& a, const Settlement& b) noexcept {
    const double bow = (hashUnit(d.seed, a.cellI * 131 + b.cellI, a.cellJ * 197 + b.cellJ, 9) - 0.5) *
                       std::hypot(b.x - a.x, b.y - a.y) * 0.25;
    const double mx = (a.x + b.x) * 0.5;
    const double my = (a.y + b.y) * 0.5;
    const double dx = b.y - a.y;
    const double dy = -(b.x - a.x);
    const double length = std::hypot(dx, dy);
    const double nx = length > 0.0 ? dx / length : 0.0;
    const double ny = length > 0.0 ? dy / length : 0.0;
    const double cx = mx + nx * bow;
    const double cy = my + ny * bow;

    Polyline line;
    line.kind = 2;
    constexpr int steps = 12;
    for (int step = 0; step <= steps; ++step) {
        const double t = static_cast<double>(step) / steps;
        const double u = 1.0 - t;
        line.xs.push_back(u * u * a.x + 2.0 * u * t * cx + t * t * b.x);
        line.ys.push_back(u * u * a.y + 2.0 * u * t * cy + t * t * b.y);
    }
    return line;
}

// Settlement street network: radial-ring, deterministic, rotated per place.
void settlementRoads(const CityDesc& d, const Settlement& s, std::vector<Polyline>& lines) {
    Rng rng(d.seed ^ (static_cast<std::uint64_t>(s.cellI) * 0x517CC1B727220A95ULL)
                ^ (static_cast<std::uint64_t>(s.cellJ) * 0xA24BAED4963EE407ULL));
    const double cosR = std::cos(s.rotation);
    const double sinR = std::sin(s.rotation);
    const auto local = [&](double lx, double ly, double& wx, double& wy) {
        wx = s.x + lx * cosR - ly * sinR;
        wy = s.y + lx * sinR + ly * cosR;
    };

    if (s.type == SettlementType::Village) {
        // Village: one wavy main street through the center plus a side lane.
        Polyline main;
        main.kind = 1;
        constexpr int points = 7;
        for (int k = 0; k < points; ++k) {
            const double t = static_cast<double>(k) / (points - 1);
            const double along = (t - 0.5) * 2.0 * s.radius * 1.7;
            const double sway = std::sin(t * kPi * 2.0 + rng.range(0.0, 6.28)) * s.radius * 0.18;
            double wx, wy;
            local(along, sway, wx, wy);
            main.xs.push_back(wx);
            main.ys.push_back(wy);
        }
        lines.push_back(std::move(main));
        return;
    }

    // City/town: rings + radial spokes.
    const int ringCount = s.type == SettlementType::City ? 4 : 3;
    for (int ring = 1; ring <= ringCount; ++ring) {
        const double radius = s.radius * static_cast<double>(ring) / ringCount;
        const double wobble = 0.12; // rings are imperfect circles
        Polyline circle;
        circle.kind = ring == ringCount ? 1 : (ring * 2 >= ringCount ? 1 : 0);
        constexpr int points = 24;
        for (int k = 0; k <= points; ++k) {
            const double angle = 2.0 * kPi * k / points;
            const double jitterRadius = radius * (1.0 + wobble * std::sin(angle * 3.0 + static_cast<double>(ring) * 1.7 + s.rotation));
            double wx, wy;
            local(jitterRadius * std::cos(angle), jitterRadius * std::sin(angle), wx, wy);
            circle.xs.push_back(wx);
            circle.ys.push_back(wy);
        }
        lines.push_back(std::move(circle));
    }

    const int spokes = s.type == SettlementType::City ? 8 : 6;
    for (int spoke = 0; spoke < spokes; ++spoke) {
        const double angle = 2.0 * kPi * spoke / spokes + 0.13;
        const double lengthOuter = s.radius * 1.15;
        double x0, y0, x1, y1;
        local(0.0, 0.0, x0, y0);
        local(lengthOuter * std::cos(angle), lengthOuter * std::sin(angle), x1, y1);
        // Slight bend: midpoint offset.
        const double bendX = (x0 + x1) * 0.5 + std::sin(angle + 1.1) * s.radius * 0.06;
        const double bendY = (y0 + y1) * 0.5 + std::cos(angle + 1.1) * s.radius * 0.06;
        Polyline line;
        line.kind = 1;
        line.xs = {x0, bendX, x1};
        line.ys = {y0, bendY, y1};
        lines.push_back(std::move(line));
    }
}

// Building placement along settlement roads: houses face streets.
struct BuildingSpot {
    double x, y;
    double halfX, halfY;
    float height;
    bool park;
    std::uint8_t district;
};

void settlementBuildings(const CityDesc& d, const Settlement& s, std::vector<BuildingSpot>& spots) {
    Rng rng(d.seed ^ (static_cast<std::uint64_t>(s.cellI) * 0x9FB21C651E98DF25ULL)
                ^ (static_cast<std::uint64_t>(s.cellJ) * 0x1B87359355B9DF2FULL));
    const double cosR = std::cos(s.rotation);
    const double sinR = std::sin(s.rotation);
    const auto world = [&](double lx, double ly, double& wx, double& wy) {
        wx = s.x + lx * cosR - ly * sinR;
        wy = s.y + lx * sinR + ly * cosR;
    };

    // Downtown density by normalized radius.
    const auto districtFor = [&](double r) {
        if (s.type == SettlementType::Village) return std::uint8_t{3};
        if (r < 0.35) return s.type == SettlementType::City ? std::uint8_t{0} : std::uint8_t{1};
        if (r < 0.7) return std::uint8_t{1};
        return std::uint8_t{2};
    };

    if (s.type == SettlementType::Village) {
        // Houses scattered along the main street direction, both sides.
        const int houses = static_cast<int>(8 + rng.unit() * 10.0);
        for (int k = 0; k < houses; ++k) {
            const double along = (rng.unit() - 0.5) * 2.0 * s.radius * 1.6;
            const double side = rng.chance(0.5) ? 1.0 : -1.0;
            const double offset = side * (s.radius * 0.12 + rng.unit() * s.radius * 0.1);
            const double sway = std::sin((along / (s.radius * 1.7) + 0.5) * kPi * 2.0 + s.rotation) * s.radius * 0.18;
            double wx, wy;
            world(along, sway + offset, wx, wy);
            BuildingSpot spot;
            spot.x = wx;
            spot.y = wy;
            spot.halfX = rng.range(4.0, 8.0);
            spot.halfY = rng.range(4.0, 8.0);
            spot.height = static_cast<float>(rng.range(3.0, 7.0));
            spot.park = false;
            spot.district = 3;
            spots.push_back(spot);
        }
        return;
    }

    // City/town: buildings FILL the cells between rings and spokes — a polar
    // grid. Each (ring gap x spoke gap) cell gets a building with jitter, so
    // the map reads as dense blocks rather than dots on circles.
    const int ringCount = s.type == SettlementType::City ? 5 : 4;
    const int spokes = s.type == SettlementType::City ? 12 : 8;
    const double ringGap = s.radius / ringCount;
    for (int ring = 0; ring < ringCount; ++ring) {
        const double rInner = ringGap * ring;
        const double rOuter = ringGap * (ring + 1);
        const double midRadius = (rInner + rOuter) * 0.5;
        const std::uint8_t district = districtFor(midRadius / s.radius);
        // Cell size shrinks toward the center; aim for one building per cell.
        const double cellArc = 2.0 * kPi * midRadius / spokes;
        // Skip tiny central cells (roads plaza).
        if (midRadius < ringGap * 0.4) continue;
        for (int spoke = 0; spoke < spokes; ++spoke) {
            const double angle0 = 2.0 * kPi * spoke / spokes;
            // Random cell subdivision: 1-2 buildings per cell for variety.
            const int perCell = district == 2 ? (rng.chance(0.5) ? 2 : 1) : 1;
            for (int sub = 0; sub < perCell; ++sub) {
                const double angle = angle0 + 2.0 * kPi / spokes *
                    ((sub + 0.5) / perCell + (rng.unit() - 0.5) * 0.4);
                const double radius = rInner + (rOuter - rInner) * (0.3 + rng.unit() * 0.5);

                if (district == 2 && rng.chance(0.18)) continue; // house district gaps

                double wx, wy;
                world(radius * std::cos(angle), radius * std::sin(angle), wx, wy);

                BuildingSpot spot;
                spot.x = wx;
                spot.y = wy;
                spot.district = district;
                spot.park = rng.chance(district == 0 ? 0.05 : district == 1 ? 0.08 : 0.15);
                // Footprint fits the cell: arc width x ring depth, minus margins.
                const double footprintArc = cellArc / perCell * 0.55;
                const double footprintRadial = ringGap * 0.42;
                const double halfFoot = std::min(footprintArc, footprintRadial) * 0.5;
                if (spot.park) {
                    spot.halfX = halfFoot * 1.6;
                    spot.halfY = halfFoot * 1.6;
                    spot.height = 0.0f;
                } else if (district == 0) {
                    spot.halfX = halfFoot * 1.1;
                    spot.halfY = halfFoot * 1.1;
                    const float base = static_cast<float>(rng.range(16.0, 38.0));
                    const float layers = static_cast<float>(1 + rng.next() % std::max<std::uint32_t>(1, d.buildingLayers));
                    spot.height = base + layers * 3.0f;
                } else if (district == 1) {
                    spot.halfX = halfFoot * 1.15;
                    spot.halfY = halfFoot * 1.15;
                    spot.height = static_cast<float>(rng.range(8.0, 22.0));
                } else {
                    spot.halfX = halfFoot;
                    spot.halfY = halfFoot;
                    spot.height = static_cast<float>(rng.range(3.5, 9.0));
                }
                spots.push_back(spot);
            }
        }
    }
}

// --- Palette (UV slots) -----------------------------------------------------
constexpr float kRoad = 0.0f / 16.0f;
constexpr float kAvenue = 1.0f / 16.0f;
constexpr float kHighway = 2.0f / 16.0f;
constexpr float kSidewalk = 3.0f / 16.0f;
constexpr float kLot = 4.0f / 16.0f;
constexpr float kPark = 5.0f / 16.0f;
constexpr float kBuildingLight = 6.0f / 16.0f;
constexpr float kBuildingMid = 7.0f / 16.0f;
constexpr float kBuildingDark = 8.0f / 16.0f;
constexpr float kTowerGlass = 9.0f / 16.0f;
constexpr float kRoof = 10.0f / 16.0f;
constexpr float kField = 11.0f / 16.0f;
constexpr float kHouse = 12.0f / 16.0f;

constexpr float kRoadZ = 0.03f;
constexpr float kYardZ = 0.06f;
constexpr float kPadZ = 0.09f;

void pushQuad(MeshData& mesh, float z, float u, float hx, float hy, float cx, float cy) {
    const std::uint32_t base = static_cast<std::uint32_t>(mesh.vertices.size());
    const float px[4] = {cx - hx, cx + hx, cx + hx, cx - hx};
    const float py[4] = {cy - hy, cy - hy, cy + hy, cy + hy};
    for (int k = 0; k < 4; ++k) {
        MeshVertex v{};
        v.position[0] = px[k];
        v.position[1] = py[k];
        v.position[2] = z;
        v.normal[2] = 1.0f;
        v.uv[0] = u;
        v.uv[1] = 0.5f;
        mesh.vertices.push_back(v);
    }
    mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
}

void pushBox(MeshData& mesh, float x0, float y0, float x1, float y1, float z0, float z1, float uSide, float uTop) {
    struct Wall { float ax, ay, bx, by, nx, ny; };
    const Wall walls[4] = {
        {x0, y0, x1, y0, 0, -1},
        {x1, y0, x1, y1, 1, 0},
        {x1, y1, x0, y1, 0, 1},
        {x0, y1, x0, y0, -1, 0},
    };
    for (const Wall& w : walls) {
        const std::uint32_t b = static_cast<std::uint32_t>(mesh.vertices.size());
        const auto push = [&](float x, float y, float z) {
            MeshVertex v{};
            v.position[0] = x;
            v.position[1] = y;
            v.position[2] = z;
            v.normal[0] = w.nx;
            v.normal[1] = w.ny;
            v.normal[2] = 0.0f;
            v.uv[0] = uSide;
            v.uv[1] = 0.5f;
            mesh.vertices.push_back(v);
        };
        push(w.ax, w.ay, z0);
        push(w.bx, w.by, z0);
        push(w.bx, w.by, z1);
        push(w.ax, w.ay, z1);
        mesh.indices.insert(mesh.indices.end(), {b, b + 1, b + 2, b, b + 2, b + 3});
    }
    const std::uint32_t rb = static_cast<std::uint32_t>(mesh.vertices.size());
    const auto top = [&](float x, float y) {
        MeshVertex v{};
        v.position[0] = x;
        v.position[1] = y;
        v.position[2] = z1;
        v.normal[2] = 1.0f;
        v.uv[0] = uTop;
        v.uv[1] = 0.5f;
        mesh.vertices.push_back(v);
    };
    top(x0, y0);
    top(x1, y0);
    top(x1, y1);
    top(x0, y1);
    mesh.indices.insert(mesh.indices.end(), {rb, rb + 1, rb + 2, rb, rb + 2, rb + 3});
}

// Roads: a polyline segment drawn as a quad strip along its length. Simple
// robust approach for chunks: draw each polyline leg as a thick line quad
// (rotated rectangle covering the leg, width = road width). Slight overlaps
// at joints are invisible at map scale.
void pushThickLine(MeshData& mesh, double x0, double y0, double x1, double y1, double width, float z, float u) {
    const double dx = x1 - x0;
    const double dy = y1 - y0;
    const double length = std::hypot(dx, dy);
    if (length < 0.5) {
        return;
    }
    const double ux = dx / length;
    const double uy = dy / length;
    const double hx = ux * length * 0.5;
    const double hy = uy * length * 0.5;
    const double px = -uy * width * 0.5;
    const double py = ux * width * 0.5;
    const float cx = static_cast<float>((x0 + x1) * 0.5);
    const float cy = static_cast<float>((y0 + y1) * 0.5);

    const std::uint32_t base = static_cast<std::uint32_t>(mesh.vertices.size());
    const float corners[4][2] = {
        {cx - static_cast<float>(hx + px), cy - static_cast<float>(hy + py)},
        {cx + static_cast<float>(hx - px), cy + static_cast<float>(hy - py)},
        {cx + static_cast<float>(hx + px), cy + static_cast<float>(hy + py)},
        {cx - static_cast<float>(hx - px), cy - static_cast<float>(hy - py)},
    };
    for (const auto& corner : corners) {
        MeshVertex v{};
        v.position[0] = corner[0];
        v.position[1] = corner[1];
        v.position[2] = z;
        v.normal[2] = 1.0f;
        v.uv[0] = u;
        v.uv[1] = 0.5f;
        mesh.vertices.push_back(v);
    }
    mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
}

// Distance from point to segment.
double pointSegmentDistance(double px, double py, double ax, double ay, double bx, double by) noexcept {
    const double dx = bx - ax;
    const double dy = by - ay;
    const double lengthSquared = dx * dx + dy * dy;
    double t = 0.0;
    if (lengthSquared > 1.0e-12) {
        t = std::clamp(((px - ax) * dx + (py - ay) * dy) / lengthSquared, 0.0, 1.0);
    }
    const double cx = ax + dx * t;
    const double cy = ay + dy * t;
    return std::hypot(px - cx, py - cy);
}

double roadWidthFor(const CityDesc& d, std::uint8_t kind) noexcept {
    return kind == 2 ? d.highwayWidth : (kind == 1 ? d.avenueWidth : d.roadWidth);
}

}

struct CityWorld::Impl {
    CityDesc description;

    struct SettlementData {
        Settlement info;
        std::vector<Polyline> roads;
        std::vector<BuildingSpot> buildings;
    };
    std::map<std::pair<std::int64_t, std::int64_t>, SettlementData> settlements;

    struct Chunk {
        MeshHandle mesh;
    };
    std::map<std::pair<std::int64_t, std::int64_t>, Chunk> chunks;

    explicit Impl(const CityDesc& d) : description(d) {}

    SettlementData& settlementData(std::int64_t i, std::int64_t j) {
        const auto key = std::make_pair(i, j);
        const auto it = settlements.find(key);
        if (it != settlements.end()) {
            return it->second;
        }
        SettlementData data;
        data.info.cellI = i;
        data.info.cellJ = j;
        if (settlementAtCell(description, i, j, data.info)) {
            settlementRoads(description, data.info, data.roads);
            settlementBuildings(description, data.info, data.buildings);
            // Highways to 8 neighbor cells that host settlements.
            for (std::int64_t di = -1; di <= 1; ++di) {
                for (std::int64_t dj = -1; dj <= 1; ++dj) {
                    if (di == 0 && dj == 0) continue;
                    Settlement neighbor;
                    if (!settlementAtCell(description, i + di, j + dj, neighbor)) continue;
                    // Own each highway from the lower cell (deterministic, no duplicates).
                    if (std::make_pair(i + di, j + dj) > key) continue;
                    data.roads.push_back(highwayBetween(description, data.info, neighbor));
                }
            }
        }
        return settlements.emplace(key, std::move(data)).first->second;
    }

    double settlementInfluence(double x, double y) noexcept {
        // Distance to the nearest settlement center; -1 if none nearby.
        Settlement nearest;
        if (!nearestSettlement(description, x, y, nearest)) {
            return -1.0;
        }
        return std::hypot(x - nearest.x, y - nearest.y);
    }

    CitySample classify(double x, double y) {
        CitySample sample;
        // Roads from nearby settlements (their networks + highways).
        const double cell = description.settlementCell;
        const std::int64_t baseI = static_cast<std::int64_t>(std::floor(x / cell));
        const std::int64_t baseJ = static_cast<std::int64_t>(std::floor(y / cell));
        double bestRoad = 1.0e18;
        std::uint8_t bestKind = 255;
        for (std::int64_t di = -1; di <= 1; ++di) {
            for (std::int64_t dj = -1; dj <= 1; ++dj) {
                SettlementData& data = settlementData(baseI + di, baseJ + dj);
                if (data.roads.empty() && data.buildings.empty()) continue;
                for (const Polyline& line : data.roads) {
                    for (std::size_t k = 0; k + 1 < line.xs.size(); ++k) {
                        const double distance = pointSegmentDistance(
                            x, y, line.xs[k], line.ys[k], line.xs[k + 1], line.ys[k + 1]);
                        if (distance < bestRoad) {
                            bestRoad = distance;
                            bestKind = line.kind;
                        }
                    }
                }
            }
        }
        if (bestKind != 255 && bestRoad <= roadWidthFor(description, bestKind) * 0.5) {
            sample.zone = bestKind == 2 ? CityZone::Highway : (bestKind == 1 ? CityZone::Avenue : CityZone::Street);
            sample.district = CityDistrict::Countryside;
            // District from the nearest settlement for in-settlement roads.
            Settlement nearest;
            if (nearestSettlement(description, x, y, nearest)) {
                const double distance = std::hypot(x - nearest.x, y - nearest.y);
                if (distance < nearest.radius * 1.2) {
                    sample.district = nearest.type == SettlementType::Village
                        ? CityDistrict::Outskirts
                        : (nearest.type == SettlementType::City ? CityDistrict::Downtown : CityDistrict::Core);
                }
            }
            return sample;
        }

        // Buildings from nearby settlements.
        for (std::int64_t di = -1; di <= 1; ++di) {
            for (std::int64_t dj = -1; dj <= 1; ++dj) {
                SettlementData& data = settlementData(baseI + di, baseJ + dj);
                for (const BuildingSpot& spot : data.buildings) {
                    if (std::abs(x - spot.x) <= spot.halfX && std::abs(y - spot.y) <= spot.halfY) {
                        sample.zone = spot.park ? CityZone::Park : CityZone::Lot;
                        sample.district = static_cast<CityDistrict>(spot.district);
                        return sample;
                    }
                }
            }
        }

        // Settlement ground (yards) vs fields: inside the built radius -> yard.
        Settlement nearest;
        if (nearestSettlement(description, x, y, nearest)) {
            const double distance = std::hypot(x - nearest.x, y - nearest.y);
            if (distance < nearest.radius * 1.05) {
                sample.zone = CityZone::Sidewalk;
                sample.district = nearest.type == SettlementType::Village
                    ? CityDistrict::Outskirts
                    : (nearest.type == SettlementType::City ? CityDistrict::Downtown : CityDistrict::Core);
                return sample;
            }
        }

        sample.zone = CityZone::Field;
        sample.district = CityDistrict::Countryside;
        return sample;
    }

    // Builds chunk (ci, cj) geometry: road legs + buildings intersecting it.
    MeshData buildChunkMesh(std::int64_t ci, std::int64_t cj) {
        MeshData mesh;
        const double size = description.chunkSize;
        const double minX = ci * size;
        const double minY = cj * size;
        const double maxX = minX + size;
        const double maxY = minY + size;

        // Field base for countryside chunks (cities draw their own yards, but
        // the base always exists under everything).
        pushQuad(mesh, 0.0f, kField, static_cast<float>(size * 0.5), static_cast<float>(size * 0.5),
                 static_cast<float>((minX + maxX) * 0.5), static_cast<float>((minY + maxY) * 0.5));

        const double cell = description.settlementCell;
        const std::int64_t baseI = static_cast<std::int64_t>(std::floor((minX + maxX) * 0.5 / cell));
        const std::int64_t baseJ = static_cast<std::int64_t>(std::floor((minY + maxY) * 0.5 / cell));
        for (std::int64_t di = -2; di <= 2; ++di) {
            for (std::int64_t dj = -2; dj <= 2; ++dj) {
                SettlementData& data = settlementData(baseI + di, baseJ + dj);
                if (data.roads.empty() && data.buildings.empty()) continue;

                // Roads.
                for (const Polyline& line : data.roads) {
                    const double width = roadWidthFor(description, line.kind);
                    const float u = line.kind == 2 ? kHighway : (line.kind == 1 ? kAvenue : kRoad);
                    for (std::size_t k = 0; k + 1 < line.xs.size(); ++k) {
                        // Rough chunk-intersection cull.
                        const double lx0 = std::max(line.xs[k], line.xs[k + 1]);
                        const double lx1 = std::min(line.xs[k], line.xs[k + 1]);
                        if (lx1 > maxX + width || lx0 < minX - width) continue;
                        const double ly0 = std::max(line.ys[k], line.ys[k + 1]);
                        const double ly1 = std::min(line.ys[k], line.ys[k + 1]);
                        if (ly1 > maxY + width || ly0 < minY - width) continue;
                        pushThickLine(mesh, line.xs[k], line.ys[k], line.xs[k + 1], line.ys[k + 1], width, kRoadZ, u);
                    }
                }

                // Building pads + boxes (culled by AABB).
                for (const BuildingSpot& spot : data.buildings) {
                    if (spot.x + spot.halfX < minX || spot.x - spot.halfX > maxX ||
                        spot.y + spot.halfY < minY || spot.y - spot.halfY > maxY) {
                        continue;
                    }
                    const float padU = spot.park ? kPark : kLot;
                    pushQuad(mesh, kPadZ, padU, static_cast<float>(spot.halfX + description.sidewalkWidth),
                             static_cast<float>(spot.halfY + description.sidewalkWidth),
                             static_cast<float>(spot.x), static_cast<float>(spot.y));
                    if (spot.park) {
                        continue;
                    }
                    float uWall = kBuildingMid;
                    if (spot.district == 0) uWall = kTowerGlass;
                    else if (spot.district == 2) uWall = kHouse;
                    else if (spot.district >= 3) uWall = kBuildingDark;

                    const float x0 = static_cast<float>(spot.x - spot.halfX);
                    const float x1 = static_cast<float>(spot.x + spot.halfX);
                    const float y0 = static_cast<float>(spot.y - spot.halfY);
                    const float y1 = static_cast<float>(spot.y + spot.halfY);
                    if (spot.district == 0 && spot.height > 28.0f) {
                        float hx = static_cast<float>(spot.halfX);
                        float hy = static_cast<float>(spot.halfY);
                        float zLow = 0.0f;
                        for (float tier = 0; tier < 3.0f; ++tier) {
                            const float zHigh = spot.height * (tier + 1.0f) / 3.0f;
                            pushBox(mesh, static_cast<float>(spot.x) - hx, static_cast<float>(spot.y) - hy,
                                    static_cast<float>(spot.x) + hx, static_cast<float>(spot.y) + hy,
                                    zLow, zHigh, uWall, kRoof);
                            zLow = zHigh;
                            hx *= 0.7f;
                            hy *= 0.7f;
                        }
                    } else if (spot.district == 2) {
                        pushBox(mesh, x0, y0, x1, y1, 0.0f, spot.height, uWall, kRoof);
                        pushBox(mesh, x0 + 0.5f, y0 + 0.5f, x1 - 0.5f, y1 - 0.5f, spot.height, spot.height + 0.9f, kRoof, kRoof);
                    } else {
                        pushBox(mesh, x0, y0, x1, y1, 0.0f, spot.height, uWall, kRoof);
                    }
                }
            }
        }
        return mesh;
    }
};

CityWorld::CityWorld(const CityDesc& description)
    : impl_(std::make_unique<Impl>(description)) {
}

CityWorld::~CityWorld() = default;

void CityWorld::update(double centerX, double centerY, double viewRadius, Graphics* graphics) {
    const double size = impl_->description.chunkSize;
    const std::int64_t ciLow = static_cast<std::int64_t>(std::floor((centerX - viewRadius) / size));
    const std::int64_t ciHigh = static_cast<std::int64_t>(std::floor((centerX + viewRadius) / size));
    const std::int64_t cjLow = static_cast<std::int64_t>(std::floor((centerY - viewRadius) / size));
    const std::int64_t cjHigh = static_cast<std::int64_t>(std::floor((centerY + viewRadius) / size));

    for (std::int64_t ci = ciLow; ci <= ciHigh; ++ci) {
        for (std::int64_t cj = cjLow; cj <= cjHigh; ++cj) {
            const auto key = std::make_pair(ci, cj);
            auto it = impl_->chunks.find(key);
            if (it == impl_->chunks.end()) {
                Impl::Chunk chunk;
                chunk.mesh = MeshHandle{};
                if (graphics != nullptr) {
                    chunk.mesh = graphics->createMesh(impl_->buildChunkMesh(ci, cj));
                }
                it = impl_->chunks.emplace(key, chunk).first;
            } else if (graphics != nullptr && !it->second.mesh.valid() && graphics->initialized()) {
                it->second.mesh = graphics->createMesh(impl_->buildChunkMesh(ci, cj));
            }
        }
    }

    for (auto it = impl_->chunks.begin(); it != impl_->chunks.end();) {
        const bool keep = it->first.first >= ciLow && it->first.first <= ciHigh &&
                          it->first.second >= cjLow && it->first.second <= cjHigh;
        if (keep) {
            ++it;
        } else {
            if (graphics != nullptr && it->second.mesh.valid()) {
                graphics->destroyMesh(it->second.mesh);
            }
            it = impl_->chunks.erase(it);
        }
    }
}

CitySample CityWorld::sampleAt(double x, double y) {
    return impl_->classify(x, y);
}

double CityWorld::groundAt(double x, double y) {
    const CitySample sample = sampleAt(x, y);
    return sample.zone == CityZone::Field ? 0.0 : kYardZ;
}

std::vector<CityWorld::Visible> CityWorld::visibleChunks() const {
    std::vector<Visible> result;
    result.reserve(impl_->chunks.size());
    for (const auto& entry : impl_->chunks) {
        if (entry.second.mesh.valid()) {
            result.push_back(Visible{entry.first.first, entry.first.second, entry.second.mesh});
        }
    }
    return result;
}

const CityDesc& CityWorld::description() const noexcept {
    return impl_->description;
}

std::size_t CityWorld::loadedChunkCount() const noexcept {
    return impl_->chunks.size();
}

}
