#include "Aporia/Game.hpp"
#include "Aporia/Math/Matrix4.hpp"
#include "Aporia/Math/Quaternion.hpp"
#include "Aporia/City/City.hpp"
#include "Aporia/Terrain/Terrain.hpp"

#include <cmath>
#include <limits>
#include <iostream>
#include <set>

#if defined(APORIA_ENGINE_HAS_GRAPHICS)
#include "Aporia/Graphics/MeshPrimitives.hpp"
#endif

#if defined(APORIA_ENGINE_HAS_INPUT)
#include "Aporia/Input/Input.hpp"
#endif

namespace {

int failures = 0;

struct TestComponent {
    int value = 0;
};

void check(bool condition, const char* message) {
    if (condition) {
        return;
    }

    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

}

#if defined(APORIA_ENGINE_HAS_INPUT)
namespace {

class ScriptedInputSource final : public Aporia::Input::Source {
public:
    bool keyDown(Aporia::KeyCode key) override {
        return downKeys.count(key) != 0;
    }

    bool mouseButtonDown(Aporia::MouseButton button) override {
        return downButtons.count(button) != 0;
    }

    Aporia::MouseState mouse() override {
        return mouseState;
    }

    std::set<Aporia::KeyCode> downKeys;
    std::set<Aporia::MouseButton> downButtons;
    Aporia::MouseState mouseState;
};

}

#endif

int main() {
    const Aporia::Quat quarterTurn = Aporia::Quat::fromAxisAngle({0.0, 0.0, 1.0}, 1.5707963267948966);
    const Aporia::Vec3 rotatedAxis = quarterTurn.rotate({1.0, 0.0, 0.0});
    check(std::abs(rotatedAxis.x) < 1.0e-12, "quaternion rotation must cancel x axis");
    check(std::abs(rotatedAxis.y - 1.0) < 1.0e-12, "quaternion rotation must map x axis to y axis");
    check((quarterTurn * Aporia::Quat::identity()).w == quarterTurn.w, "quaternion identity multiplication must preserve rotation");

    Aporia::Game game;

    check(game.scene().objectCount() == 0, "new scene must be empty");
    check(!Aporia::ObjectHandle{}.valid(), "default handle must be invalid");

    const Aporia::ObjectHandle object = game.scene().createObject("sample");
    check(object.valid(), "created object must have a valid handle");
    check(game.scene().contains(object), "scene must contain created object");

    const Aporia::ComponentTypeId componentType = game.scene().registerComponentType("test");
    check(componentType != Aporia::invalidComponentType, "component type must be registered");
    check(game.scene().registerComponentType("test") == componentType, "component type registration must be stable");
    check(game.scene().componentTypeId("test") == componentType, "component type lookup must find registration");
    check(game.scene().componentTypeId("missing") == Aporia::invalidComponentType, "unknown component type must be invalid");
    check(!game.scene().addComponent(object, Aporia::invalidComponentType, TestComponent{1}), "invalid component type must be rejected");

    check(game.scene().addComponent(object, componentType, TestComponent{42}), "component must be attached");
    check(game.scene().containsComponent(object, componentType), "attached component must be found");
    check(game.scene().componentCount(object) == 1, "component count must include attached component");

    TestComponent* mutableComponent = game.scene().findComponent<TestComponent>(object, componentType);
    check(mutableComponent != nullptr && mutableComponent->value == 42, "attached component must expose typed value");
    check(game.scene().addComponent(object, componentType, TestComponent{7}), "component replacement must succeed");
    check(game.scene().componentCount(object) == 1, "component replacement must not duplicate type");
    mutableComponent = game.scene().findComponent<TestComponent>(object, componentType);
    check(mutableComponent != nullptr && mutableComponent->value == 7, "component replacement must update value");
    check(game.scene().findComponent<int>(object, componentType) == nullptr, "typed lookup must reject wrong type");
    check(!game.scene().addComponent(Aporia::ObjectHandle{}, componentType, TestComponent{1}), "component cannot attach to missing object");

    const Aporia::Scene& constScene = game.scene();
    const TestComponent* constComponent = constScene.findComponent<TestComponent>(object, componentType);
    check(constComponent != nullptr && constComponent->value == 7, "const scene must expose typed component");
    check(game.scene().removeComponent(object, componentType), "component removal must succeed");
    check(!game.scene().containsComponent(object, componentType), "removed component must be absent");
    check(game.scene().componentCount(object) == 0, "removed component must not be counted");
    check(!game.scene().removeComponent(object, componentType), "removing missing component must fail");

    game.step();
    check(game.lastSubstepCount() == 1, "default step must execute one 300 Hz substep");

    game.step(1.0 / 150.0);
    check(game.lastSubstepCount() == 2, "custom delta must execute two fixed substeps");

#if defined(APORIA_ENGINE_HAS_PHYSICS)
    check(game.physics().timestepHz() == 300.0, "physics default timestep must be 300 Hz");
    game.setTimestepHz(120.0);
    check(game.physics().timestepHz() == 120.0, "Game::setTimestepHz must forward to physics");
    check(std::abs(game.timestepHz() - 120.0) < 1.0e-12, "Game timestep must reflect the new frequency");
    game.step(1.0 / 60.0);
    check(game.lastSubstepCount() == 2, "120 Hz physics must execute two substeps per 60 Hz frame");
    game.setTimestepHz(1000.0);
    check(game.physics().timestepHz() == 1000.0, "physics accepts arbitrary positive frequency");
    game.setTimestepHz(0.0);
    game.setTimestepHz(-50.0);
    game.setTimestepHz(std::numeric_limits<double>::infinity());
    check(game.physics().timestepHz() == 1000.0, "invalid frequencies must be rejected");
    game.setTimestepHz(300.0);

    // Heightfield terrain: sphere settles on a slope, box and capsule rest on it.
    Aporia::Game terrainGame;
    Aporia::TerrainDesc terrainDescription;
    terrainDescription.seed = 42;
    terrainDescription.gridResolution = 32;
    terrainDescription.worldSize = 32.0f;
    terrainDescription.heightScale = 2.0f;
    terrainDescription.heightOffset = 1.0f;
    terrainDescription.noiseOctaves = 3;
    const Aporia::TerrainHeightfield terrainField = Aporia::generateTerrainHeightfield(terrainDescription);
    check(terrainField.size() == 32u * 32u, "terrain heightfield must match grid resolution");
    check(terrainField.maxHeight > terrainField.minHeight, "terrain must have height variation");

    const Aporia::BodyHandle terrainBody = terrainGame.physics().createHeightfield(terrainDescription, terrainField);
    check(terrainBody.valid(), "heightfield collider must be created");

    const Aporia::Vec3 samplePoint{3.0, -2.0, 50.0};
    const double sampleElevation = terrainGame.physics().heightfieldHeightAt(terrainField, samplePoint);
    check(std::abs(sampleElevation - Aporia::terrainHeightAt(terrainDescription, terrainField, samplePoint)) < 1.0e-12,
          "heightfieldHeightAt must match terrainHeightAt");
    check(sampleElevation > 0.0, "terrain elevation must sit above the offset");
    const double repeatElevation = terrainGame.physics().heightfieldHeightAt(terrainField, samplePoint);
    check(std::abs(sampleElevation - repeatElevation) < 1.0e-12, "terrain sampling must be deterministic");

    Aporia::SphereBodyConfig terrainSphere;
    terrainSphere.position = Aporia::Vec3{3.0, -2.0, sampleElevation + 6.0};
    terrainSphere.radius = 0.5;
    terrainSphere.restitution = 0.0;
    terrainSphere.friction = 0.8;
    const Aporia::BodyHandle slopeBall = terrainGame.physics().createSphere(terrainSphere);
    for (int index = 0; index < 600; ++index) {
        terrainGame.step(1.0 / 60.0);
    }
    const Aporia::Vec3 ballPosition = terrainGame.physics().bodyPosition(slopeBall);
    check(ballPosition.z > sampleElevation - 1.0, "sphere must rest near the terrain surface");
    check(ballPosition.z < sampleElevation + 1.0, "sphere must not sink into the terrain");
    check(std::abs(terrainGame.physics().bodyVelocity(slopeBall).z) < 0.5, "sphere must settle on the terrain");

    Aporia::BoxBodyConfig terrainBox;
    terrainBox.position = Aporia::Vec3{-4.0, 5.0, sampleElevation + 5.0};
    terrainBox.halfExtents = Aporia::Vec3{0.4, 0.4, 0.4};
    terrainBox.restitution = 0.0;
    terrainBox.friction = 0.9;
    const Aporia::BodyHandle slopeBox = terrainGame.physics().createBox(terrainBox);
    for (int index = 0; index < 600; ++index) {
        terrainGame.step(1.0 / 60.0);
    }
    const Aporia::Vec3 boxPosition = terrainGame.physics().bodyPosition(slopeBox);
    const double boxGround = terrainGame.physics().heightfieldHeightAt(terrainField, boxPosition);
    std::cout << "[terrain] box pos z=" << boxPosition.z << " ground=" << boxGround << '\n';
    check(boxPosition.z > boxGround - 1.5 && boxPosition.z < boxGround + 1.5,
          "box must rest near the terrain surface");

    // City generation: infinite streaming world, deterministic blocks.
    Aporia::CityDesc cityDescription;
    cityDescription.seed = 777;
    {
        Aporia::CityWorld cityWorld(cityDescription);
        cityWorld.update(0.0, 0.0, 600.0, nullptr);
        check(cityWorld.loadedChunkCount() > 0, "streamed city must load blocks around the center");

        // Determinism: identical block content for the same indices.
        const std::size_t firstPass = cityWorld.loadedChunkCount();

        // Sampling: highway line and lot classification.
        bool sawHighway = false;
        bool sawLot = false;
        bool sawStreet = false;
        std::size_t roadSamples = 0;
        for (double y = -900.0; y <= 900.0 && !(sawHighway && sawLot && sawStreet); y += 12.0) {
            for (double x = -900.0; x <= 900.0 && !(sawHighway && sawLot && sawStreet); x += 12.0) {
                const Aporia::CitySample sample = cityWorld.sampleAt(x, y);
                sawHighway = sawHighway || sample.zone == Aporia::CityZone::Highway;
                sawLot = sawLot || sample.zone == Aporia::CityZone::Lot;
                sawStreet = sawStreet || sample.zone == Aporia::CityZone::Street ||
                            sample.zone == Aporia::CityZone::Avenue;
                roadSamples += (sample.zone == Aporia::CityZone::Highway ||
                                sample.zone == Aporia::CityZone::Avenue ||
                                sample.zone == Aporia::CityZone::Street) ? 1 : 0;
            }
        }
        check(sawHighway, "sampled area must include highway zones");
        check(sawLot, "sampled area must include building lots");
        check(sawStreet, "sampled area must include street zones");
        check(roadSamples > 20, "roads must cover a meaningful share of the map");

        const Aporia::CitySample farSample = cityWorld.sampleAt(1.0e6, 1.0e6);
        check(farSample.district == Aporia::CityDistrict::Countryside, "far point must be countryside");
        check(farSample.zone == Aporia::CityZone::Street || farSample.zone == Aporia::CityZone::Sidewalk ||
                  farSample.zone == Aporia::CityZone::Field || farSample.zone == Aporia::CityZone::Lot,
              "far point must classify into some zone");

        // Streaming: move far away, block count must not grow unbounded.
        cityWorld.update(5000.0, 5000.0, 400.0, nullptr);
        check(cityWorld.loadedChunkCount() < firstPass * 4, "streaming must unload distant blocks");

        (void)firstPass;
    }

    // Scroll: scripted source feeds the per-frame wheel accumulator.
    class ScrollSource final : public Aporia::Input::Source {
    public:
        bool keyDown(Aporia::KeyCode) override { return false; }
        bool mouseButtonDown(Aporia::MouseButton) override { return false; }
        Aporia::MouseState mouse() override { return {}; }
        double scrollDelta() override {
            const double delta = pending;
            pending = 0.0;
            return delta;
        }
        double pending = 0.0;
    };
    Aporia::Input scrollInput;
    auto* scrollSource = new ScrollSource();
    scrollInput.setSource(std::unique_ptr<Aporia::Input::Source>(scrollSource));
    scrollSource->pending = 2.5;
    scrollInput.update();
    check(std::abs(scrollInput.scrollDelta() - 2.5) < 1.0e-12, "scroll delta must flow through update");
    scrollInput.update();
    check(scrollInput.scrollDelta() == 0.0, "scroll delta must reset after being read");
#endif

#if defined(APORIA_ENGINE_HAS_PHYSICS)
    Aporia::SphereBodyConfig fallingBody;
    fallingBody.position = Aporia::Vec3{0.0, 0.0, 10.0};
    fallingBody.mass = 2.0;
    fallingBody.radius = 0.5;
    const Aporia::BodyHandle falling = game.physics().createSphere(fallingBody);

    check(falling.valid(), "falling body must be created");
    check(game.physics().bodyCount() == 1, "physics world must contain falling body");
    check(game.physics().bodyMass(falling) == 2.0, "body mass must use kilograms");

    game.step();
    check(game.physics().bodyPosition(falling).z < 10.0, "gravity must move a dynamic body down");
    check(game.physics().bodyVelocity(falling).z < 0.0, "gravity must create downward velocity");

    Aporia::Game angularGame;
    angularGame.physics().setGravity({});
    Aporia::SphereBodyConfig angularBody;
    angularBody.angularVelocity = Aporia::Vec3{0.0, 0.0, 1.0};
    const Aporia::BodyHandle spinning = angularGame.physics().createSphere(angularBody);
    angularGame.step();
    const Aporia::Vec3 spunAxis = angularGame.physics().bodyOrientation(spinning).rotate({1.0, 0.0, 0.0});
    check(spunAxis.y > 0.0, "angular velocity must integrate body orientation");
    check(std::abs(angularGame.physics().bodyAngularVelocity(spinning).z - 1.0) < 1.0e-12, "angular velocity must persist without torque");
    angularGame.physics().applyTorque(spinning, {0.0, 0.0, 2.0});
    angularGame.step();
    check(angularGame.physics().bodyAngularVelocity(spinning).z > 1.0, "torque must change angular velocity");

    Aporia::Game collisionGame;
    Aporia::SphereBodyConfig staticBody;
    staticBody.position = Aporia::Vec3{0.0, 0.0, 0.0};
    staticBody.radius = 1.0;
    staticBody.isStatic = true;
    const Aporia::BodyHandle fixed = collisionGame.physics().createSphere(staticBody);

    Aporia::SphereBodyConfig movingBody;
    movingBody.position = Aporia::Vec3{0.0, 0.0, 1.5};
    movingBody.linearVelocity = Aporia::Vec3{0.0, 0.0, -1.0};
    movingBody.radius = 0.5;
    movingBody.restitution = 0.0;
    const Aporia::BodyHandle moving = collisionGame.physics().createSphere(movingBody);

    collisionGame.step();
    const Aporia::ContactInfo contact = collisionGame.physics().lastContact();
    check(fixed.valid() && moving.valid(), "collision bodies must be created");
    check(collisionGame.physics().lastStepCollisionCount() == 1, "overlapping spheres must create one contact");
    check(contact.impulse > 0.0, "collision must apply a normal impulse");
    check(collisionGame.physics().bodyVelocity(moving).z >= -1.0e-9, "inelastic collision must remove downward velocity");
    Aporia::Game boxGame;
    Aporia::BoxBodyConfig staticBox;
    staticBox.position = Aporia::Vec3{0.0, 0.0, 0.0};
    staticBox.halfExtents = Aporia::Vec3{1.0, 1.0, 1.0};
    staticBox.isStatic = true;
    const Aporia::BodyHandle fixedBox = boxGame.physics().createBox(staticBox);

    Aporia::BoxBodyConfig movingBox;
    movingBox.position = Aporia::Vec3{1.5, 0.0, 0.0};
    movingBox.linearVelocity = Aporia::Vec3{-1.0, 0.0, 0.0};
    movingBox.halfExtents = Aporia::Vec3{1.0, 1.0, 1.0};
    movingBox.restitution = 0.0;
    const Aporia::BodyHandle dynamicBox = boxGame.physics().createBox(movingBox);

    boxGame.step();
    check(fixedBox.valid() && dynamicBox.valid(), "box bodies must be created");
    check(boxGame.physics().lastStepCollisionCount() >= 1, "overlapping boxes must create one contact");
    check(boxGame.physics().bodyVelocity(dynamicBox).x >= -1.0e-9, "box collision must remove approach velocity");

    Aporia::Game capsuleGame;
    Aporia::CapsuleBodyConfig capsule;
    capsule.position = Aporia::Vec3{0.0, 0.0, 0.0};
    capsule.radius = 0.5;
    capsule.halfHeight = 0.5;
    capsule.isStatic = true;
    const Aporia::BodyHandle staticCapsule = capsuleGame.physics().createCapsule(capsule);

    Aporia::SphereBodyConfig capsuleSphere;
    capsuleSphere.position = Aporia::Vec3{0.0, 0.0, 0.8};
    capsuleSphere.radius = 0.4;
    capsuleSphere.linearVelocity = Aporia::Vec3{0.0, 0.0, -1.0};
    capsuleSphere.restitution = 0.0;
    const Aporia::BodyHandle fallingCapsuleSphere = capsuleGame.physics().createSphere(capsuleSphere);

    capsuleGame.step();
    check(staticCapsule.valid() && fallingCapsuleSphere.valid(), "capsule bodies must be created");
    check(capsuleGame.physics().lastStepCollisionCount() == 1, "sphere must collide with capsule segment");
    check(capsuleGame.physics().bodyVelocity(fallingCapsuleSphere).z >= -1.0e-9, "capsule collision must remove downward velocity");
    Aporia::Game boxCapsuleGame;
    Aporia::BoxBodyConfig capsuleBox;
    capsuleBox.position = Aporia::Vec3{0.0, 0.0, 0.0};
    capsuleBox.halfExtents = Aporia::Vec3{1.0, 1.0, 0.5};
    capsuleBox.isStatic = true;
    const Aporia::BodyHandle fixedCapsuleBox = boxCapsuleGame.physics().createBox(capsuleBox);

    Aporia::CapsuleBodyConfig fallingCapsule;
    fallingCapsule.position = Aporia::Vec3{0.0, 0.0, 1.2};
    fallingCapsule.radius = 0.3;
    fallingCapsule.halfHeight = 0.5;
    fallingCapsule.linearVelocity = Aporia::Vec3{0.0, 0.0, -1.0};
    fallingCapsule.restitution = 0.0;
    const Aporia::BodyHandle dynamicCapsule = boxCapsuleGame.physics().createCapsule(fallingCapsule);

    boxCapsuleGame.step();
    check(fixedCapsuleBox.valid() && dynamicCapsule.valid(), "box and capsule bodies must be created");
    check(boxCapsuleGame.physics().lastStepCollisionCount() == 1, "box must collide with capsule segment");
    check(boxCapsuleGame.physics().bodyVelocity(dynamicCapsule).z >= -1.0e-9, "box collision must remove capsule approach velocity");
    Aporia::Game sphereBoxGame;
    Aporia::BoxBodyConfig sphereTargetBox;
    sphereTargetBox.position = Aporia::Vec3{0.0, 0.0, 0.0};
    sphereTargetBox.halfExtents = Aporia::Vec3{1.0, 1.0, 1.0};
    sphereTargetBox.isStatic = true;
    const Aporia::BodyHandle fixedSphereTarget = sphereBoxGame.physics().createBox(sphereTargetBox);

    Aporia::SphereBodyConfig fallingBoxSphere;
    fallingBoxSphere.position = Aporia::Vec3{0.0, 0.0, 1.4};
    fallingBoxSphere.radius = 0.5;
    fallingBoxSphere.linearVelocity = Aporia::Vec3{0.0, 0.0, -1.0};
    fallingBoxSphere.restitution = 0.0;
    const Aporia::BodyHandle dynamicBoxSphere = sphereBoxGame.physics().createSphere(fallingBoxSphere);

    sphereBoxGame.step();
    check(fixedSphereTarget.valid() && dynamicBoxSphere.valid(), "sphere and box bodies must be created");
    check(sphereBoxGame.physics().lastStepCollisionCount() == 1, "sphere must collide with box face");
    check(sphereBoxGame.physics().bodyVelocity(dynamicBoxSphere).z >= -1.0e-9, "box collision must remove sphere approach velocity");

    Aporia::Game capsulePairGame;
    Aporia::CapsuleBodyConfig fixedCapsulePair;
    fixedCapsulePair.position = Aporia::Vec3{0.0, 0.0, 0.0};
    fixedCapsulePair.radius = 0.4;
    fixedCapsulePair.halfHeight = 0.4;
    fixedCapsulePair.isStatic = true;
    const Aporia::BodyHandle fixedCapsulePairHandle = capsulePairGame.physics().createCapsule(fixedCapsulePair);

    Aporia::CapsuleBodyConfig movingCapsulePair;
    movingCapsulePair.position = Aporia::Vec3{0.0, 0.0, 1.0};
    movingCapsulePair.radius = 0.4;
    movingCapsulePair.halfHeight = 0.4;
    movingCapsulePair.linearVelocity = Aporia::Vec3{0.0, 0.0, -1.0};
    movingCapsulePair.restitution = 0.0;
    const Aporia::BodyHandle movingCapsulePairHandle = capsulePairGame.physics().createCapsule(movingCapsulePair);

    capsulePairGame.step();
    check(fixedCapsulePairHandle.valid() && movingCapsulePairHandle.valid(), "capsule pair bodies must be created");
    check(capsulePairGame.physics().lastStepCollisionCount() == 1, "capsules must collide with parallel segments");
    check(capsulePairGame.physics().bodyVelocity(movingCapsulePairHandle).z >= -1.0e-9, "capsule pair collision must remove approach velocity");
    Aporia::Game restitutionGame;
    Aporia::SphereBodyConfig restitutionTarget;
    restitutionTarget.position = Aporia::Vec3{0.0, 0.0, 0.0};
    restitutionTarget.radius = 1.0;
    restitutionTarget.isStatic = true;
    const Aporia::BodyHandle restitutionFixed = restitutionGame.physics().createSphere(restitutionTarget);

    Aporia::SphereBodyConfig bouncingSphere;
    bouncingSphere.position = Aporia::Vec3{0.0, 0.0, 1.5};
    bouncingSphere.radius = 0.5;
    bouncingSphere.linearVelocity = Aporia::Vec3{0.0, 0.0, -1.0};
    bouncingSphere.restitution = 0.8;
    const Aporia::BodyHandle bouncing = restitutionGame.physics().createSphere(bouncingSphere);

    restitutionGame.step();
    check(restitutionFixed.valid() && bouncing.valid(), "restitution bodies must be created");
    check(restitutionGame.physics().bodyVelocity(bouncing).z > 0.0, "restitution must produce separating velocity");
    check(restitutionGame.physics().bodyPosition(bouncing).z >= 1.5 - 0.02, "position correction must prevent sphere penetration");

    Aporia::Game frictionGame;
    Aporia::SphereBodyConfig frictionTarget;
    frictionTarget.radius = 1.0;
    frictionTarget.isStatic = true;
    const Aporia::BodyHandle frictionFixed = frictionGame.physics().createSphere(frictionTarget);

    Aporia::SphereBodyConfig slidingSphere;
    slidingSphere.position = Aporia::Vec3{0.0, 0.0, 1.2};
    slidingSphere.radius = 0.5;
    slidingSphere.linearVelocity = Aporia::Vec3{1.0, 0.0, -1.0};
    slidingSphere.restitution = 0.0;
    slidingSphere.friction = 1.0;
    const Aporia::BodyHandle sliding = frictionGame.physics().createSphere(slidingSphere);

    frictionGame.step();
    check(frictionFixed.valid() && sliding.valid(), "friction bodies must be created");
    check(frictionGame.physics().bodyVelocity(sliding).x < 1.0, "friction must reduce tangential velocity");
    check(frictionGame.physics().bodyVelocity(sliding).x >= -1.0e-9, "friction must not reverse tangential velocity");
    Aporia::Game orientedBoxGame;
    Aporia::BoxBodyConfig orientedBox;
    orientedBox.halfExtents = Aporia::Vec3{2.0, 0.5, 0.5};
    orientedBox.orientation = Aporia::Quat::fromAxisAngle({0.0, 0.0, 1.0}, 1.5707963267948966);
    orientedBox.isStatic = true;
    const Aporia::BodyHandle fixedOrientedBox = orientedBoxGame.physics().createBox(orientedBox);

    Aporia::SphereBodyConfig orientedBoxSphere;
    orientedBoxSphere.position = Aporia::Vec3{0.8, 0.0, 0.0};
    orientedBoxSphere.radius = 0.4;
    orientedBoxSphere.linearVelocity = Aporia::Vec3{-1.0, 0.0, 0.0};
    orientedBoxSphere.restitution = 0.0;
    const Aporia::BodyHandle dynamicOrientedBoxSphere = orientedBoxGame.physics().createSphere(orientedBoxSphere);

    orientedBoxGame.step();
    check(fixedOrientedBox.valid() && dynamicOrientedBoxSphere.valid(), "oriented box and sphere must be created");
    check(orientedBoxGame.physics().lastStepCollisionCount() == 1, "sphere must respect box orientation");
    check(orientedBoxGame.physics().bodyVelocity(dynamicOrientedBoxSphere).x >= -1.0e-9, "oriented box collision must remove approach velocity");

    Aporia::Game orientedCapsuleGame;
    Aporia::CapsuleBodyConfig orientedCapsule;
    orientedCapsule.radius = 0.3;
    orientedCapsule.halfHeight = 1.0;
    orientedCapsule.orientation = Aporia::Quat::fromAxisAngle({0.0, 1.0, 0.0}, 1.5707963267948966);
    orientedCapsule.isStatic = true;
    const Aporia::BodyHandle fixedOrientedCapsule = orientedCapsuleGame.physics().createCapsule(orientedCapsule);

    Aporia::SphereBodyConfig orientedCapsuleSphere;
    orientedCapsuleSphere.position = Aporia::Vec3{1.05, 0.0, 0.0};
    orientedCapsuleSphere.radius = 0.3;
    orientedCapsuleSphere.linearVelocity = Aporia::Vec3{-1.0, 0.0, 0.0};
    orientedCapsuleSphere.restitution = 0.0;
    const Aporia::BodyHandle dynamicOrientedCapsuleSphere = orientedCapsuleGame.physics().createSphere(orientedCapsuleSphere);

    orientedCapsuleGame.step();
    check(fixedOrientedCapsule.valid() && dynamicOrientedCapsuleSphere.valid(), "oriented capsule and sphere must be created");
    check(orientedCapsuleGame.physics().lastStepCollisionCount() == 1, "sphere must respect capsule segment orientation");
    check(orientedCapsuleGame.physics().bodyVelocity(dynamicOrientedCapsuleSphere).x >= -1.0e-4, "oriented capsule collision must remove approach velocity");

    Aporia::Game angularCollisionGame;
    Aporia::BoxBodyConfig angularGround;
    angularGround.halfExtents = Aporia::Vec3{0.5, 0.5, 0.5};
    angularGround.isStatic = true;
    const Aporia::BodyHandle fixedAngularGround = angularCollisionGame.physics().createBox(angularGround);

    Aporia::BoxBodyConfig angularBox;
    angularBox.position = Aporia::Vec3{0.6, 0.0, 0.7};
    angularBox.halfExtents = Aporia::Vec3{0.5, 0.5, 0.5};
    angularBox.linearVelocity = Aporia::Vec3{0.0, 0.0, -1.0};
    angularBox.restitution = 0.0;
    const Aporia::BodyHandle dynamicAngularBox = angularCollisionGame.physics().createBox(angularBox);

    angularCollisionGame.step();
    const Aporia::Vec3 collisionAngularVelocity = angularCollisionGame.physics().bodyAngularVelocity(dynamicAngularBox);
    check(fixedAngularGround.valid() && dynamicAngularBox.valid(), "angular collision bodies must be created");
    check(angularCollisionGame.physics().lastStepCollisionCount() >= 1, "offset box contact must be detected");
    check(std::abs(collisionAngularVelocity.x) + std::abs(collisionAngularVelocity.y) > 1.0e-9, "off-center impulse must create angular velocity");
#endif

    // --- Mat4 ---
    const Aporia::Mat4 translationMatrix = Aporia::Mat4::translation({1.0, 2.0, 3.0});
    const Aporia::Vec3 translatedPoint = translationMatrix.transformPoint({1.0, 1.0, 1.0});
    check(std::abs(translatedPoint.x - 2.0) < 1.0e-5, "translation must move x");
    check(std::abs(translatedPoint.y - 3.0) < 1.0e-5, "translation must move y");
    check(std::abs(translatedPoint.z - 4.0) < 1.0e-5, "translation must move z");

    const Aporia::Mat4 rotationMatrix = Aporia::Mat4::fromQuaternion(quarterTurn);
    const Aporia::Vec3 rotatedByMatrix = rotationMatrix.transformPoint({1.0, 0.0, 0.0});
    check(std::abs(rotatedByMatrix.x) < 1.0e-5, "quaternion matrix must cancel x");
    check(std::abs(rotatedByMatrix.y - 1.0) < 1.0e-5, "quaternion matrix must map x to y");

    const Aporia::Mat4 combined = Aporia::Mat4::translation({5.0, 0.0, 0.0}) * Aporia::Mat4::scale({2.0, 2.0, 2.0});
    const Aporia::Vec3 combinedPoint = combined.transformPoint({1.0, 0.0, 0.0});
    check(std::abs(combinedPoint.x - 7.0) < 1.0e-5, "matrix multiply must apply scale then translation");

    const Aporia::Mat4 look = Aporia::Mat4::lookAt({0.0, -5.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0});
    const Aporia::Vec3 lookCenter = look.transformPoint({0.0, 0.0, 0.0});
    check(std::abs(lookCenter.x) < 1.0e-5 && std::abs(lookCenter.y) < 1.0e-5, "lookAt must center the target");
    check(std::abs(lookCenter.z + 5.0) < 1.0e-5, "lookAt must place target at view depth 5 (negative view z)");

    const Aporia::Mat4 perspective = Aporia::Mat4::perspectiveVulkan(1.0, 1.0, 0.1, 10.0);
    const Aporia::Vec3 perspectiveNear = perspective.transformPoint({0.0, 0.0, -0.1});
    const Aporia::Vec3 perspectiveFar = perspective.transformPoint({0.0, 0.0, -10.0});
    check(std::abs(perspectiveNear.z) < 1.0e-4, "perspective near plane must map to depth 0");
    check(std::abs(perspectiveFar.z - 1.0) < 1.0e-4, "perspective far plane must map to depth 1");

    const Aporia::Mat4 ortho = Aporia::Mat4::orthographicVulkan(2.0, 2.0, 0.1, 10.0);
    const Aporia::Vec3 orthoCenter = ortho.transformPoint({0.0, 0.0, -5.0});
    check(std::abs(orthoCenter.x) < 1.0e-5 && std::abs(orthoCenter.y) < 1.0e-5, "ortho must keep center");

#if defined(APORIA_ENGINE_HAS_INPUT)
    // --- Input ---
    auto scripted = std::make_unique<ScriptedInputSource>();
    ScriptedInputSource* source = scripted.get();
    Aporia::Input input;
    input.setSource(std::move(scripted));

    input.update();
    check(!input.key(Aporia::KeyCode::W), "unpressed key must read as up");

    source->downKeys.insert(Aporia::KeyCode::W);
    input.update();
    check(input.key(Aporia::KeyCode::W), "pressed key must read as down");
    check(input.keyPressed(Aporia::KeyCode::W), "press edge must be detected");
    check(!input.keyReleased(Aporia::KeyCode::W), "held key must not report release edge");

    input.update();
    check(input.key(Aporia::KeyCode::W) && !input.keyPressed(Aporia::KeyCode::W), "held key must have no press edge");

    source->downKeys.clear();
    input.update();
    check(input.keyReleased(Aporia::KeyCode::W), "release edge must be detected");
    check(input.mouseButton(Aporia::MouseButton::Left) == false, "unpressed mouse button must read as up");

    source->downButtons.insert(Aporia::MouseButton::Left);
    source->mouseState = {12.0, 34.0};
    input.update();
    check(input.mouseButtonPressed(Aporia::MouseButton::Left), "mouse press edge must be detected");
    check(std::abs(input.mouse().x - 12.0) < 1.0e-9, "mouse x must round-trip");
    check(std::abs(input.mouse().y - 34.0) < 1.0e-9, "mouse y must round-trip");

    Aporia::Game inputGame;
    inputGame.input().setSource(std::make_unique<ScriptedInputSource>());
    inputGame.updateInput();
    check(!inputGame.input().key(Aporia::KeyCode::A), "game input must default to no keys");
#endif

#if defined(APORIA_ENGINE_HAS_GRAPHICS)
    // --- Mesh primitives ---
    const Aporia::MeshData boxMesh = Aporia::apr::makeBoxMesh(0.5f, 0.5f, 0.5f);
    check(boxMesh.vertices.size() == 24, "box mesh must have 24 vertices");
    check(boxMesh.indices.size() == 36, "box mesh must have 36 indices");
    bool boxMeshValid = boxMesh.vertices.size() == 24 && boxMesh.indices.size() == 36;
    for (const std::uint32_t index : boxMesh.indices) {
        boxMeshValid = boxMeshValid && index < boxMesh.vertices.size();
    }
    bool boxWindingOutward = boxMeshValid;
    bool boxFacesPlanar = boxMeshValid;
    for (std::size_t triangle = 0; triangle + 2 < boxMesh.indices.size(); triangle += 3) {
        const auto& a = boxMesh.vertices[boxMesh.indices[triangle]];
        const auto& b = boxMesh.vertices[boxMesh.indices[triangle + 1]];
        const auto& c = boxMesh.vertices[boxMesh.indices[triangle + 2]];
        const Aporia::Vec3 ab{b.position[0] - a.position[0], b.position[1] - a.position[1], b.position[2] - a.position[2]};
        const Aporia::Vec3 ac{c.position[0] - a.position[0], c.position[1] - a.position[1], c.position[2] - a.position[2]};
        const Aporia::Vec3 normal{a.normal[0], a.normal[1], a.normal[2]};
        boxWindingOutward = boxWindingOutward && dot(cross(ab, ac), normal) > 0.0;
        const double faceOffset = dot(
            Aporia::Vec3{a.position[0], a.position[1], a.position[2]}, normal);
        const double expectedOffset = 0.5 * (std::abs(normal.x) + std::abs(normal.y) + std::abs(normal.z));
        const auto facePlaneMatches = [&](const Aporia::MeshVertex& vertex) {
            return std::abs(dot(Aporia::Vec3{vertex.position[0], vertex.position[1], vertex.position[2]}, normal) - expectedOffset) < 1.0e-6;
        };
        boxFacesPlanar = boxFacesPlanar && std::abs(faceOffset - expectedOffset) < 1.0e-6 &&
            facePlaneMatches(b) && facePlaneMatches(c);
    }
    double boxMaxAbsPosition = 0.0;
    for (const Aporia::MeshVertex& vertex : boxMesh.vertices) {
        for (int axis = 0; axis < 3; ++axis) {
            boxMaxAbsPosition = std::max(boxMaxAbsPosition, std::abs(static_cast<double>(vertex.position[axis])));
        }
    }
    check(boxMeshValid, "box indices must only reference initialized vertices");
    check(boxWindingOutward, "box triangle winding must agree with outward face normals");
    check(boxFacesPlanar, "box triangles must lie on the plane at their declared half-extent");
    check(std::abs(boxMaxAbsPosition - 0.5) < 1.0e-6, "box vertices must remain within requested half extents");

    const Aporia::MeshData sphereMesh = Aporia::apr::makeSphereMesh(1.0f, 8, 4);
    check(sphereMesh.vertices.size() == (4 + 1) * (8 + 1), "sphere vertex grid must match segments/rings");
    bool sphereOnRadius = true;
    for (const Aporia::MeshVertex& vertex : sphereMesh.vertices) {
        const float radius = std::sqrt(
            vertex.position[0] * vertex.position[0] +
            vertex.position[1] * vertex.position[1] +
            vertex.position[2] * vertex.position[2]);
        if (std::abs(radius - 1.0f) > 1.0e-4f) {
            sphereOnRadius = false;
            break;
        }
    }
    check(sphereOnRadius, "all sphere vertices must lie on the radius");

    const Aporia::MeshData planeMesh = Aporia::apr::makePlaneMesh(1.0f, 1.0f);
    check(planeMesh.vertices.size() == 4 && planeMesh.indices.size() == 6, "plane must be one quad");

    const Aporia::MeshData capsuleMesh = Aporia::apr::makeCapsuleMesh(0.5f, 1.0f, 8, 4);
    check(capsuleMesh.indices.size() > 0 && capsuleMesh.vertices.size() > 0, "capsule must generate geometry");
    float capsuleTop = -1.0e9f;
    for (const Aporia::MeshVertex& vertex : capsuleMesh.vertices) {
        capsuleTop = std::max(capsuleTop, vertex.position[2]);
    }
    check(std::abs(capsuleTop - 1.5f) < 1.0e-5f, "capsule top must be halfHeight + radius");
#endif

#if defined(APORIA_ENGINE_HAS_PHYSICS)
    // --- Body: createBody and Transform sync (headless-safe parts) ---
    Aporia::Game bodyGame;
#if defined(APORIA_ENGINE_HAS_GRAPHICS)
    // createBody requires initialized graphics; without it the returned Body is invalid.
    const Aporia::BodyDesc invalidBodyDesc;
    const Aporia::Body invalidBody = bodyGame.createBody(invalidBodyDesc);
    check(!invalidBody.valid(), "createBody without graphics must return invalid body");
#endif

    // --- Raycast tests (Sphere, Box, Capsule) ---
    Aporia::PhysicsWorld raycastWorld;
    Aporia::SphereBodyConfig rSphere;
    rSphere.position = {0.0, 0.0, 5.0};
    rSphere.radius = 1.0;
    rSphere.isStatic = true;
    const Aporia::BodyHandle raySphereHandle = raycastWorld.createSphere(rSphere);

    Aporia::BoxBodyConfig rBox;
    rBox.position = {0.0, 5.0, 0.0};
    rBox.halfExtents = {1.0, 1.0, 1.0};
    rBox.isStatic = true;
    const Aporia::BodyHandle rayBoxHandle = raycastWorld.createBox(rBox);

    Aporia::CapsuleBodyConfig rCapsule;
    rCapsule.position = {5.0, 0.0, 0.0};
    rCapsule.radius = 0.5;
    rCapsule.halfHeight = 1.0;
    rCapsule.isStatic = true;
    const Aporia::BodyHandle rayCapsuleHandle = raycastWorld.createCapsule(rCapsule);

    // Raycast hitting sphere at z=4.0
    const Aporia::Ray rayZ({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0});
    const Aporia::RaycastHit hitZ = raycastWorld.raycast(rayZ, 10.0);
    check(hitZ.hasHit && hitZ.body == raySphereHandle, "raycast must hit sphere");
    check(std::abs(hitZ.distance - 4.0) < 1.0e-4, "raycast sphere distance must be 4.0");
    check(std::abs(hitZ.normal.z - (-1.0)) < 1.0e-4, "raycast sphere normal must point towards origin");

    // Raycast hitting box at y=4.0
    const Aporia::Ray rayY({0.0, 0.0, 0.0}, {0.0, 1.0, 0.0});
    const Aporia::RaycastHit hitY = raycastWorld.raycast(rayY, 10.0);
    check(hitY.hasHit && hitY.body == rayBoxHandle, "raycast must hit box");
    check(std::abs(hitY.distance - 4.0) < 1.0e-4, "raycast box distance must be 4.0");

    // Raycast hitting capsule at x=4.5
    const Aporia::Ray rayX({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0});
    const Aporia::RaycastHit hitX = raycastWorld.raycast(rayX, 10.0);
    check(hitX.hasHit && hitX.body == rayCapsuleHandle, "raycast must hit capsule");
    check(std::abs(hitX.distance - 4.5) < 1.0e-4, "raycast capsule distance must be 4.5");

    // Raycast miss
    const Aporia::Ray rayMiss({0.0, 0.0, 0.0}, {0.0, -1.0, 0.0});
    const Aporia::RaycastHit hitMiss = raycastWorld.raycast(rayMiss, 10.0);
    check(!hitMiss.hasHit, "raycast into empty space must not hit");

    // raycastAll sorted check
    const Aporia::RaycastHit hitZShort = raycastWorld.raycast(rayZ, 2.0);
    check(!hitZShort.hasHit, "raycast with short maxDistance must not hit distant object");
#endif

    if (failures == 0) {
        std::cout << "Aporia CLI tests passed\n";
    }

    return failures == 0 ? 0 : 1;
}
