#pragma once

#include "Aporia/Core/Export.hpp"
#include "Aporia/Graphics/Graphics.hpp"

namespace Aporia {
namespace apr {

// All primitives are Z-up, centered at the origin, unit-sized unless stated.
APORIA_API MeshData makeBoxMesh(float halfX, float halfY, float halfZ);
APORIA_API MeshData makeSphereMesh(float radius, std::uint32_t segments = 24, std::uint32_t rings = 16);
APORIA_API MeshData makePlaneMesh(float halfX, float halfY); // facing +Z
APORIA_API MeshData makeCapsuleMesh(float radius, float halfHeight, std::uint32_t segments = 24, std::uint32_t rings = 8);

}
}
