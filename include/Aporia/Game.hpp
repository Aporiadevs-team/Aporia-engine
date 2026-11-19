#pragma once

#include "Aporia/Core/Export.hpp"
#include "Aporia/Math/Matrix4.hpp"
#include "Aporia/Scene/Scene.hpp"

#if defined(APORIA_ENGINE_HAS_PHYSICS)
#include "Aporia/Physics/PhysicsWorld.hpp"
#endif

#if defined(APORIA_ENGINE_HAS_GRAPHICS)
#include "Aporia/Body.hpp"
#include "Aporia/Graphics/Graphics.hpp"
#endif

#if defined(APORIA_ENGINE_HAS_INPUT)
#include "Aporia/Input/Input.hpp"
#endif

#include <cstdint>

namespace Aporia {

class APORIA_API Game {
public:
    Game();
    ~Game();

    Game(const Game&) = delete;
    Game& operator=(const Game&) = delete;
    Game(Game&&) noexcept = default;
    Game& operator=(Game&&) noexcept = default;

    [[nodiscard]] Scene& scene() noexcept;

#if defined(APORIA_ENGINE_HAS_PHYSICS)
    [[nodiscard]] PhysicsWorld& physics() noexcept;
#endif

#if defined(APORIA_ENGINE_HAS_GRAPHICS)
    bool initGraphics(
        const GraphicsContext& context = {},
        const GraphicsConfig& config = {});
    void shutdownGraphics() noexcept;
    [[nodiscard]] Graphics& graphics() noexcept;

    // Creates a scene object with Transform + MeshRef (+ PhysicsRef when physics
    // is enabled and the descriptor does not disable it). Returns an invalid Body
    // if graphics is not initialized or mesh creation failed.
    Body createBody(const BodyDesc& description);

    // Destroy a body created via createBody(). Removes the physics body (when
    // physics is enabled), the scene object and all its components (Transform,
    // MeshRef, PhysicsRef). Invalidates the returned Body handle.
    void destroyBody(BodyHandle handle) noexcept;

#if defined(APORIA_ENGINE_HAS_PHYSICS)
    // Stage B terrain: generates the heightfield, creates the render mesh and
    // the static heightfield collider, returns them as a Body. The mesh is
    // placed with the descriptor transform. Returns an invalid Body without
    // graphics; the heightfield still works headless via physics().
    Body createTerrain(const TerrainDesc& description);
#endif

    void setCamera(const CameraState& camera) noexcept;
    [[nodiscard]] CameraState camera() const noexcept;

    // Weather for the next frames (precipitation, wind, fog). The default state
    // is clear and renders exactly like a weatherless frame.
    void setWeather(const WeatherState& weather) noexcept;
    [[nodiscard]] WeatherState weather() const noexcept;

#if defined(APORIA_SEASONS)
    // Applies the season's atmosphere preset; setWeather() can override it.
    void setSeason(Season season) noexcept;
    [[nodiscard]] Season season() const noexcept;
#endif
#endif

#if defined(APORIA_ENGINE_HAS_INPUT)
    // Polls input sources; call once per frame before reading key states.
    void updateInput() noexcept;
    [[nodiscard]] Input& input() noexcept;
#endif

    void step() noexcept;
    void step(double frameDelta) noexcept;

    void setTimestepHz(double hz) noexcept;
    [[nodiscard]] double timestepHz() const noexcept;

    void setMaxSubsteps(std::uint32_t value) noexcept;
    [[nodiscard]] std::uint32_t maxSubsteps() const noexcept;
    [[nodiscard]] std::uint32_t lastSubstepCount() const noexcept;

private:
    void advanceFixedStep() noexcept;
    void syncTransforms() noexcept;

    Scene scene_;
#if defined(APORIA_ENGINE_HAS_PHYSICS)
    PhysicsWorld physics_;
#endif
#if defined(APORIA_ENGINE_HAS_GRAPHICS)
    Graphics graphics_;
    CameraState camera_;
#endif
#if defined(APORIA_ENGINE_HAS_INPUT)
    Input input_;
#endif
    double timestepSeconds_ = 1.0 / 300.0;
    double accumulator_ = 0.0;
    std::uint32_t maxSubsteps_ = 8;
    std::uint32_t lastSubstepCount_ = 0;

#if defined(APORIA_ENGINE_HAS_GRAPHICS)
    ComponentTypeId transformType_ = invalidComponentType;
    ComponentTypeId meshRefType_ = invalidComponentType;
    ComponentTypeId physicsRefType_ = invalidComponentType;
#endif
};

}
