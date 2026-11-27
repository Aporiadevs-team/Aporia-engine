#pragma once

#include "Aporia/Core/Export.hpp"
#include "Aporia/Graphics/Graphics.hpp"
#include "Aporia/Graphics/MeshPrimitives.hpp"
#include "Aporia/Math/Quaternion.hpp"
#include "Aporia/Math/Vector3.hpp"
#include "Aporia/Scene/Scene.hpp"
#include "Aporia/Terrain/Terrain.hpp"

#if defined(APORIA_ENGINE_HAS_PHYSICS)
#include "Aporia/Physics/PhysicsWorld.hpp"
#endif

#include <cstdint>
#include <string>

namespace Aporia {

struct Transform {
    Vec3 position{};
    Quat orientation{};
    Vec3 scale{1.0, 1.0, 1.0};
};

struct MeshRef {
    MeshHandle mesh;
};

#if defined(APORIA_ENGINE_HAS_PHYSICS)
struct PhysicsRef {
    BodyHandle body;
};
#endif

enum class MeshKind : std::uint8_t {
    Box,
    Sphere,
    Plane,
    Capsule,
};

struct BodyDesc {
    std::string name;
    MeshKind meshKind = MeshKind::Box;
    // Mesh sizing, per kind:
    //   Box: halfExtents; Sphere: sphereRadius; Plane: planeHalfX/planeHalfY; Capsule: capsuleRadius/capsuleHalfHeight.
    Vec3 halfExtents{0.5f, 0.5f, 0.5f};
    float sphereRadius = 0.5f;
    float planeHalfX = 1.0f;
    float planeHalfY = 1.0f;
    float capsuleRadius = 0.5f;
    float capsuleHalfHeight = 0.5f;

    Transform transform{};

    // Physics: mass <= 0 or isStatic creates a static body; physicsDisabled skips physics entirely.
    bool physicsDisabled = false;
    bool isStatic = false;
    double mass = 1.0;
    double restitution = 0.2;
    double friction = 0.5;
    Vec3 linearVelocity{};
    Vec3 angularVelocity{};
};

// Convenience builder: a "Body" is a scene object with Transform + MeshRef (+ PhysicsRef).
// It is a construction helper, not a game entity; gameplay code lives in the host.
class APORIA_API Body {
public:
    [[nodiscard]] ObjectHandle object() const noexcept;
    [[nodiscard]] MeshHandle mesh() const noexcept;
#if defined(APORIA_ENGINE_HAS_PHYSICS)
    [[nodiscard]] BodyHandle physicsBody() const noexcept;
#endif
    [[nodiscard]] bool valid() const noexcept;

private:
    friend class Game;
    ObjectHandle object_;
    MeshHandle mesh_;
#if defined(APORIA_ENGINE_HAS_PHYSICS)
    BodyHandle body_;
#endif
};

}
