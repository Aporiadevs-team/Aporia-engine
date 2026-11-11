#include "Aporia/Game.hpp"
#include "Aporia/Body.hpp"

#include <algorithm>
#include <cmath>

#if !defined(APORIA_ENGINE_HAS_GRAPHICS)
namespace Aporia {
Game::Game() = default;
Game::~Game() = default;
}
#endif

namespace Aporia {

Scene& Game::scene() noexcept {
    return scene_;
}

#if defined(APORIA_ENGINE_HAS_PHYSICS)
PhysicsWorld& Game::physics() noexcept {
    return physics_;
}
#endif

} // namespace Aporia

#if defined(APORIA_ENGINE_HAS_GRAPHICS)
namespace Aporia {

Game::Game() = default;
Game::~Game() = default;

ObjectHandle Body::object() const noexcept {
    return object_;
}

MeshHandle Body::mesh() const noexcept {
    return mesh_;
}

bool Body::valid() const noexcept {
    return object_.valid() && mesh_.valid();
}

#if defined(APORIA_ENGINE_HAS_PHYSICS)
BodyHandle Body::physicsBody() const noexcept {
    return body_;
}
#endif

bool Game::initGraphics(const GraphicsContext& context, const GraphicsConfig& config) {
#if defined(APORIA_ENGINE_HAS_INPUT)
    // The window handle arrives here anyway; wire the polled input source to it.
    if (context.nativeWindow != 0) {
        if (auto source = makeWin32InputSource(context.nativeWindow)) {
            input_.setSource(std::move(source));
        }
    }
#endif
    return graphics_.initialize(context, config);
}

Body Game::createBody(const BodyDesc& description) {
    Body result;
    if (!graphics_.initialized()) {
        return result;
    }

    MeshData meshData;
    switch (description.meshKind) {
    case MeshKind::Box:
        meshData = apr::makeBoxMesh(static_cast<float>(description.halfExtents.x),
                                    static_cast<float>(description.halfExtents.y),
                                    static_cast<float>(description.halfExtents.z));
        break;
    case MeshKind::Sphere:
        meshData = apr::makeSphereMesh(description.sphereRadius);
        break;
    case MeshKind::Plane:
        meshData = apr::makePlaneMesh(description.planeHalfX, description.planeHalfY);
        break;
    case MeshKind::Capsule:
        meshData = apr::makeCapsuleMesh(description.capsuleRadius, description.capsuleHalfHeight);
        break;
    }

    result.mesh_ = graphics_.createMesh(meshData);
    if (!result.mesh_.valid()) {
        return result;
    }

    ObjectHandle object = scene_.createObject(description.name);
    const ComponentTypeId transformType = scene_.registerComponentType("Transform");
    const ComponentTypeId meshType = scene_.registerComponentType("MeshRef");
    scene_.addComponent(object, transformType, Transform{description.transform});
    scene_.addComponent(object, meshType, MeshRef{result.mesh_});

#if defined(APORIA_ENGINE_HAS_PHYSICS)
    if (!description.physicsDisabled) {
        if (description.meshKind == MeshKind::Box) {
            BoxBodyConfig box;
            box.position = description.transform.position;
            box.orientation = description.transform.orientation;
            box.halfExtents = Vec3{
                static_cast<double>(description.halfExtents.x) * description.transform.scale.x,
                static_cast<double>(description.halfExtents.y) * description.transform.scale.y,
                static_cast<double>(description.halfExtents.z) * description.transform.scale.z,
            };
            box.mass = description.mass;
            box.restitution = description.restitution;
            box.friction = description.friction;
            box.isStatic = description.isStatic;
            box.linearVelocity = description.linearVelocity;
            box.angularVelocity = description.angularVelocity;
            result.body_ = physics_.createBox(box);
        } else if (description.meshKind == MeshKind::Sphere) {
            SphereBodyConfig sphere;
            sphere.position = description.transform.position;
            sphere.orientation = description.transform.orientation;
            sphere.radius = description.sphereRadius *
                static_cast<double>(std::max({description.transform.scale.x, description.transform.scale.y, description.transform.scale.z}));
            sphere.mass = description.mass;
            sphere.restitution = description.restitution;
            sphere.friction = description.friction;
            sphere.isStatic = description.isStatic;
            sphere.linearVelocity = description.linearVelocity;
            sphere.angularVelocity = description.angularVelocity;
            result.body_ = physics_.createSphere(sphere);
        } else if (description.meshKind == MeshKind::Capsule) {
            // Apply transform scale to the capsule physics dimensions.
            const double scaledRadius = static_cast<double>(description.capsuleRadius) *
                std::max(description.transform.scale.x, description.transform.scale.z);
            const double scaledHalfHeight = static_cast<double>(description.capsuleHalfHeight) *
                description.transform.scale.y;

            CapsuleBodyConfig capsule;
            capsule.position = description.transform.position;
            capsule.orientation = description.transform.orientation;
            capsule.radius = scaledRadius;
            capsule.halfHeight = scaledHalfHeight;
            capsule.mass = description.mass;
            capsule.restitution = description.restitution;
            capsule.friction = description.friction;
            capsule.isStatic = description.isStatic;
            capsule.linearVelocity = description.linearVelocity;
            capsule.angularVelocity = description.angularVelocity;
            result.body_ = physics_.createCapsule(capsule);
        }

        if (result.body_.valid()) {
            const ComponentTypeId physicsType = scene_.registerComponentType("PhysicsRef");
            scene_.addComponent(object, physicsType, PhysicsRef{result.body_});
        }
    }
#endif

    result.object_ = object;
    return result;
}

void Game::destroyBody(BodyHandle handle) noexcept {
    if (!handle.valid()) {
        return;
    }

    // Find the scene object that owns this physics body.
    const ComponentTypeId physicsType = scene_.componentTypeId("PhysicsRef");
    const ComponentTypeId meshType = scene_.componentTypeId("MeshRef");
    if (physicsType == invalidComponentType) {
        return;
    }

    ObjectHandle targetObject;
    MeshHandle targetMesh;

    for (const Scene::ComponentView& view : scene_.components()) {
        const ObjectHandle obj{view.objectId};
        const PhysicsRef* ref = scene_.findComponent<PhysicsRef>(obj, physicsType);
        if (ref != nullptr && ref->body.value == handle.value) {
            targetObject = obj;
            const Transform* transform = scene_.findComponent<Transform>(obj, scene_.componentTypeId("Transform"));
            if (transform == nullptr) {
                return; // invalid state
            }
            // Find the mesh component on the same object.
            const MeshRef* meshRef = scene_.findComponent<MeshRef>(obj, meshType);
            if (meshRef != nullptr) {
                targetMesh = meshRef->mesh;
            }
            break;
        }
    }

    // 1. Destroy the mesh.
    if (targetMesh.valid()) {
        graphics_.destroyMesh(targetMesh);
    }

    // 2. Remove the scene object (which also removes all its components).
    if (targetObject.valid()) {
        scene_.removeObject(targetObject);
    }

    // 3. Remove the physics body (when physics is enabled).
#if defined(APORIA_ENGINE_HAS_PHYSICS)
    physics_.destroyBody(handle);
#endif
}

#if defined(APORIA_ENGINE_HAS_GRAPHICS) && defined(APORIA_ENGINE_HAS_PHYSICS)
Body Game::createTerrain(const TerrainDesc& description) {
    Body result;

    const TerrainHeightfield field = generateTerrainHeightfield(description);
    result.body_ = physics_.createHeightfield(description, field);

    ObjectHandle object = scene_.createObject("terrain");
    const ComponentTypeId transformType = scene_.registerComponentType("Transform");
    scene_.addComponent(object, transformType, Transform{description.position, description.orientation, Vec3{1.0, 1.0, 1.0}});

    if (graphics_.initialized()) {
        result.mesh_ = graphics_.createMesh(makeTerrainMesh(field));
        const ComponentTypeId meshType = scene_.registerComponentType("MeshRef");
        if (result.mesh_.valid()) {
            scene_.addComponent(object, meshType, MeshRef{result.mesh_});
        }
    }

    if (result.body_.valid()) {
        const ComponentTypeId physicsType = scene_.registerComponentType("PhysicsRef");
        scene_.addComponent(object, physicsType, PhysicsRef{result.body_});
    }

    result.object_ = object;
    return result;
}
#endif

void Game::setCamera(const CameraState& camera) noexcept {
    camera_ = camera;
}

CameraState Game::camera() const noexcept {
    return camera_;
}
} // namespace Aporia
#endif

#if defined(APORIA_ENGINE_HAS_INPUT)
namespace Aporia {
void Game::updateInput() noexcept {
    input_.update();
}

Input& Game::input() noexcept {
    return input_;
}
}
#endif

#if defined(APORIA_ENGINE_HAS_GRAPHICS)
namespace Aporia {
void Game::shutdownGraphics() noexcept {
    graphics_.shutdown();
}

Graphics& Game::graphics() noexcept {
    return graphics_;
}
}
#endif

namespace Aporia {

void Game::step() noexcept {
    step(timestepSeconds_);
}

void Game::step(double frameDelta) noexcept {
    if (!std::isfinite(frameDelta) || frameDelta <= 0.0) {
        return;
    }

    accumulator_ += frameDelta;
    lastSubstepCount_ = 0;

    while (accumulator_ >= timestepSeconds_ && lastSubstepCount_ < maxSubsteps_) {
        advanceFixedStep();
        accumulator_ -= timestepSeconds_;
        ++lastSubstepCount_;
    }

    if (lastSubstepCount_ == maxSubsteps_ && accumulator_ >= timestepSeconds_) {
        accumulator_ = 0.0;
    }

    if (lastSubstepCount_ > 0) {
        syncTransforms();
    }
}

void Game::setTimestepHz(double hz) noexcept {
    if (!std::isfinite(hz) || hz <= 0.0) {
        return;
    }

    timestepSeconds_ = 1.0 / hz;
    accumulator_ = 0.0;
#if defined(APORIA_ENGINE_HAS_PHYSICS)
    physics_.setTimestepHz(hz);
#endif
}

double Game::timestepHz() const noexcept {
    return 1.0 / timestepSeconds_;
}

void Game::setMaxSubsteps(std::uint32_t value) noexcept {
    if (value == 0) {
        return;
    }

    maxSubsteps_ = value;
}

std::uint32_t Game::maxSubsteps() const noexcept {
    return maxSubsteps_;
}

std::uint32_t Game::lastSubstepCount() const noexcept {
    return lastSubstepCount_;
}

void Game::advanceFixedStep() noexcept {
#if defined(APORIA_ENGINE_HAS_PHYSICS)
    physics_.step(timestepSeconds_);
#endif
}

void Game::syncTransforms() noexcept {
#if defined(APORIA_ENGINE_HAS_PHYSICS)
    const ComponentTypeId transformType = scene_.componentTypeId("Transform");
    const ComponentTypeId physicsType = scene_.componentTypeId("PhysicsRef");
    if (transformType == invalidComponentType || physicsType == invalidComponentType) {
        return;
    }

    for (const Scene::ComponentView& view : scene_.components()) {
        if (view.type != physicsType) {
            continue;
        }
        const ObjectHandle object{view.objectId};
        const PhysicsRef* reference = scene_.findComponent<PhysicsRef>(object, physicsType);
        Transform* transform = scene_.findComponent<Transform>(object, transformType);
        if (reference == nullptr || transform == nullptr || !reference->body.valid()) {
            continue;
        }
        transform->position = physics_.bodyPosition(reference->body);
        transform->orientation = physics_.bodyOrientation(reference->body);
    }
#endif
}

}
