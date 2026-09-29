// Render test 1 — 2D.
// SDL3 owns the window and event loop. Aporia owns render, physics, input.
// A single dynamic body on a static floor is pushed around with WASD.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <SDL3/SDL.h>

#include "Aporia/Game.hpp"

#include <cstring>
#include <iostream>
#include <vector>

namespace {

// Headless self-check: render the same scene into the 64x64 offscreen target
// and verify pixels via readback. If the mesh path is broken this fails with a
// message instead of a black window.
bool headlessMeshCheck() {
    Aporia::Game game;
    Aporia::GraphicsConfig config;
    config.headless = true;
    config.enableValidation = true;
    if (!game.initGraphics({}, config)) {
        std::cerr << "[selfcheck] initGraphics failed: " << game.graphics().lastError() << '\n';
        return false;
    }

    Aporia::BodyDesc floor;
    floor.meshKind = Aporia::MeshKind::Box;
    floor.halfExtents = Aporia::Vec3{8.0f, 0.5f, 0.5f};
    floor.transform.position = Aporia::Vec3{0.0, -4.0, 0.0};
    floor.isStatic = true;
    const Aporia::Body floorBody = game.createBody(floor);

    Aporia::BodyDesc player;
    player.meshKind = Aporia::MeshKind::Box;
    player.halfExtents = Aporia::Vec3{0.5f, 0.5f, 0.5f};
    const Aporia::Body playerBody = game.createBody(player);

    Aporia::CameraState camera;
    const Aporia::Mat4 view = Aporia::Mat4::lookAt({0.0, 0.0, 12.0}, {0.0, 0.0, 0.0}, {0.0, 1.0, 0.0});
    const Aporia::Mat4 projection = Aporia::Mat4::orthographicVulkan(18.0, 10.125, 0.1, 100.0);
    std::memcpy(camera.viewMatrix, &view, sizeof(view));
    std::memcpy(camera.projectionMatrix, &projection, sizeof(projection));
    camera.position[2] = 12.0;

    std::vector<Aporia::RenderItem> items;
    for (const Aporia::Body* body : {&floorBody, &playerBody}) {
        Aporia::RenderItem item;
        item.meshId = body->mesh().value();
        const Aporia::Mat4 model = Aporia::Mat4::translation(body->object().valid() && body == &floorBody
            ? Aporia::Vec3{0.0, -4.0, 0.0}
            : Aporia::Vec3{});
        std::memcpy(item.modelMatrix, &model, sizeof(model));
        items.push_back(item);
    }
    if (!game.graphics().render(camera, items)) {
        std::cerr << "[selfcheck] render failed: " << game.graphics().lastError() << '\n';
        return false;
    }

    std::vector<std::uint8_t> pixels;
    if (!game.graphics().readbackPixels(pixels)) {
        std::cerr << "[selfcheck] readback failed: " << game.graphics().lastError() << '\n';
        return false;
    }

    // Center of the screen is inside the player box; top-left corner is background.
    auto brightness = [&pixels](std::size_t x, std::size_t y) {
        const std::size_t offset = (y * 64 + x) * 4;
        return static_cast<int>(pixels[offset]) + pixels[offset + 1] + pixels[offset + 2];
    };
    const int centerBrightness = brightness(32, 32);
    const int cornerBrightness = brightness(2, 2);
    std::cout << "[selfcheck] center=" << centerBrightness << " corner=" << cornerBrightness << '\n';
    if (centerBrightness <= cornerBrightness) {
        std::cerr << "[selfcheck] FAIL: mesh pixels are not brighter than background; mesh rendering is broken\n";
        return false;
    }
    return true;
}

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
    if (!headlessMeshCheck()) {
        std::cerr << "Headless mesh self-check failed; not opening the window\n";
        return 1;
    }

    if (SDL_Init(SDL_INIT_VIDEO) != true) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("Aporia Render Test 1 — 2D", 960, 540, 0);
    if (window == nullptr) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << '\n';
        SDL_Quit();
        return 1;
    }

    // Hand the SDL3 window to the engine; Vulkan render surface comes from it.
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

    // 2D world: gravity along -Y, XY is the play plane, camera looks down -Z.
    game.physics().setGravity({0.0, -9.81, 0.0});

    // Stage B terrain as rolling 2D ground: the heightfield is generated Z-up,
    // then rotated -90° about X so its height axis becomes world -Y... actually
    // +Y-up requires rotating the field's +Z into world +Y; the ground line
    // runs along world X. The player walks the hilly surface left and right.
    Aporia::TerrainDesc terrainDescription;
    terrainDescription.seed = 11;
    terrainDescription.gridResolution = 128;
    terrainDescription.worldSize = 40.0f;
    terrainDescription.heightScale = 3.0f;
    terrainDescription.heightOffset = -4.5f;
    terrainDescription.noiseFrequency = 3.0f;
    terrainDescription.noiseOctaves = 3;
    // Rotate field local +Z (height) onto world +Y: -90 degrees about X maps
    // +Z -> +Y and +Y -> -Z, so the field spans X (width) and -Z (depth, unused).
    terrainDescription.orientation = Aporia::Quat::fromAxisAngle({1.0, 0.0, 0.0}, -1.5707963267948966);
    const Aporia::Body terrainBody = game.createTerrain(terrainDescription);

    Aporia::BodyDesc player;
    player.name = "player";
    player.meshKind = Aporia::MeshKind::Sphere;
    player.sphereRadius = 0.5f;
    player.transform.position = Aporia::Vec3{0.0, 2.0, 0.0};
    player.restitution = 0.1;
    const Aporia::Body playerBody = game.createBody(player);

    Aporia::CameraState camera;
    const Aporia::Mat4 view = Aporia::Mat4::lookAt({0.0, 0.0, 14.0}, {0.0, 0.0, 0.0}, {0.0, 1.0, 0.0});
    const Aporia::Mat4 projection = Aporia::Mat4::orthographicVulkan(24.0, 13.5, 0.1, 100.0);
    std::memcpy(camera.viewMatrix, &view, sizeof(view));
    std::memcpy(camera.projectionMatrix, &projection, sizeof(projection));
    camera.position[0] = 0.0;
    camera.position[1] = 0.0;
    camera.position[2] = 14.0;
    game.setCamera(camera);

    bool running = true;
    while (running) {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
                running = false;
            }
        }

        // Input -> body force. This is the game rule; the engine just provides input.
        game.updateInput();
        Aporia::Vec3 push{};
        if (game.input().key(Aporia::KeyCode::A)) {
            push.x -= 30.0;
        }
        if (game.input().key(Aporia::KeyCode::D)) {
            push.x += 30.0;
        }
        if (game.input().key(Aporia::KeyCode::W)) {
            push.y += 40.0;
        }
        game.physics().applyForce(playerBody.physicsBody(), push);

        game.step(1.0 / 60.0);

        // Camera follows the player along X.
        const Aporia::Vec3 playerPosition = game.physics().bodyPosition(playerBody.physicsBody());
        Aporia::CameraState camera;
        const Aporia::Mat4 view = Aporia::Mat4::lookAt(
            {playerPosition.x, 0.0, 14.0}, {playerPosition.x, 0.0, 0.0}, {0.0, 1.0, 0.0});
        const Aporia::Mat4 projection = Aporia::Mat4::orthographicVulkan(24.0, 13.5, 0.1, 100.0);
        std::memcpy(camera.viewMatrix, &view, sizeof(view));
        std::memcpy(camera.projectionMatrix, &projection, sizeof(projection));
        camera.position[0] = playerPosition.x;
        camera.position[1] = 0.0;
        camera.position[2] = 14.0;
        game.setCamera(camera);

        std::vector<Aporia::RenderItem> items;
        appendItem(items, terrainBody, game.scene());
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
