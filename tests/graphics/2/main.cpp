// Render test 2 — 3D.
// Z-up world: static ground plane, a few obstacles, one dynamic body pushed
// with WASD (screen-relative). Perspective camera looks at the player body.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <SDL3/SDL.h>

#include "Aporia/Game.hpp"

#include <cstring>
#include <iostream>
#include <vector>

namespace {

void appendItem(std::vector<Aporia::RenderItem>& items, const Aporia::Body& body, Aporia::Scene& scene) {
    const Aporia::ComponentTypeId transformType = scene.componentTypeId("Transform");
    const Aporia::Transform* transform = scene.findComponent<Aporia::Transform>(body.object(), transformType);
    if (transform == nullptr) {
        return;
    }

    Aporia::RenderItem item;
    item.meshId = body.mesh().value();
    const Aporia::Mat4 model = Aporia::Mat4::translation(transform->position) *
                               Aporia::Mat4::fromQuaternion(transform->orientation) *
                               Aporia::Mat4::scale(transform->scale);
    std::memcpy(item.modelMatrix, &model, sizeof(model));
    items.push_back(item);
}

}

int main() {
    if (SDL_Init(SDL_INIT_VIDEO) != true) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("Aporia Render Test 2 — 3D", 1280, 720, 0);
    if (window == nullptr) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << '\n';
        SDL_Quit();
        return 1;
    }

    const HWND hwnd = static_cast<HWND>(SDL_GetPointerProperty(
        SDL_GetWindowProperties(window),
        SDL_PROP_WINDOW_WIN32_HWND_POINTER,
        nullptr));
    if (hwnd == nullptr) {
        std::cerr << "SDL window has no Win32 handle\n";
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    Aporia::Game game;
    Aporia::GraphicsContext context;
    context.nativeWindow = reinterpret_cast<std::uintptr_t>(hwnd);
    Aporia::GraphicsConfig graphicsConfig;
    graphicsConfig.headless = false;
    graphicsConfig.enableValidation = true;
    if (!game.initGraphics(context, graphicsConfig)) {
        std::cerr << "initGraphics failed: " << game.graphics().lastError() << '\n';
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Stage B terrain: hills from noise, heightfield collider + mesh in one call.
    Aporia::TerrainDesc terrainDescription;
    terrainDescription.seed = 7;
    terrainDescription.gridResolution = 96;
    terrainDescription.worldSize = 48.0f;
    terrainDescription.heightScale = 2.5f;
    terrainDescription.heightOffset = -0.5f;
    terrainDescription.noiseFrequency = 2.5f;
    terrainDescription.noiseOctaves = 4;
    const Aporia::Body terrainBody = game.createTerrain(terrainDescription);

    // Obstacles the player can bump into; dropped onto the terrain at runtime.
    std::vector<Aporia::Body> obstacles;
    const Aporia::Vec3 obstaclePositions[3] = {
        {3.0, 3.0, 4.0},
        {-4.0, 2.0, 4.5},
        {1.0, -4.5, 5.0},
    };
    for (const Aporia::Vec3& position : obstaclePositions) {
        Aporia::BodyDesc obstacle;
        obstacle.meshKind = Aporia::MeshKind::Box;
        obstacle.halfExtents = Aporia::Vec3{0.5f, 0.5f, 0.5f};
        obstacle.transform.position = position;
        obstacle.isStatic = true;
        obstacles.push_back(game.createBody(obstacle));
    }

    Aporia::BodyDesc player;
    player.name = "player";
    player.meshKind = Aporia::MeshKind::Sphere;
    player.sphereRadius = 0.5f;
    player.transform.position = Aporia::Vec3{0.0, 0.0, 2.0};
    player.restitution = 0.2;
    const Aporia::Body playerBody = game.createBody(player);

    bool running = true;
    while (running) {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
                running = false;
            }
        }

        game.updateInput();

        // WASD pushes the body in world XY; the camera follows its position.
        Aporia::Vec3 push{};
        if (game.input().key(Aporia::KeyCode::A)) {
            push.x -= 60.0;
        }
        if (game.input().key(Aporia::KeyCode::D)) {
            push.x += 60.0;
        }
        if (game.input().key(Aporia::KeyCode::S)) {
            push.y -= 60.0;
        }
        if (game.input().key(Aporia::KeyCode::W)) {
            push.y += 60.0;
        }
        if (game.input().key(Aporia::KeyCode::Space)) {
            push.z += 200.0;
        }
        game.physics().applyForce(playerBody.physicsBody(), push);

        game.step(1.0 / 60.0);

        const Aporia::Vec3 playerPosition = game.physics().bodyPosition(playerBody.physicsBody());

        Aporia::CameraState camera;
        const Aporia::Vec3 eye = {playerPosition.x - 5.0, playerPosition.y - 5.0, playerPosition.z + 4.0};
        const Aporia::Mat4 view = Aporia::Mat4::lookAt(eye, playerPosition, {0.0, 0.0, 1.0});
        const Aporia::Mat4 projection = Aporia::Mat4::perspectiveVulkan(1.05, 1280.0 / 720.0, 0.1, 200.0);
        std::memcpy(camera.viewMatrix, &view, sizeof(view));
        std::memcpy(camera.projectionMatrix, &projection, sizeof(projection));
        camera.position[0] = eye.x;
        camera.position[1] = eye.y;
        camera.position[2] = eye.z;
        game.setCamera(camera);

        std::vector<Aporia::RenderItem> items;
        appendItem(items, terrainBody, game.scene());
        for (const Aporia::Body& obstacle : obstacles) {
            appendItem(items, obstacle, game.scene());
        }
        appendItem(items, playerBody, game.scene());
        if (!game.graphics().render(game.camera(), items)) {
            const std::string& error = game.graphics().lastError();
            if (error.find("out of date") == std::string::npos &&
                error.find("recreated") == std::string::npos) {
                std::cerr << "render failed: " << error << '\n';
                running = false;
            }
        }
    }

    game.shutdownGraphics();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
