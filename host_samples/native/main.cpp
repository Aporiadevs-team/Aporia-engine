// "Game base" sample: the user owns the window and message loop; Aporia owns
// graphics, physics, scene, input. ~25 lines of engine code, no game rules inside.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "Aporia/Game.hpp"

#include <cstring>
#include <iostream>
#include <vector>

namespace {

constexpr wchar_t windowClassName[] = L"AporiaNativeHost";

LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_CLOSE) {
        DestroyWindow(window);
        return 0;
    }
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

}

int main() {
    // Window: user side, plain Win32.
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_OWNDC;
    windowClass.lpfnWndProc = windowProcedure;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = windowClassName;
    if (RegisterClassExW(&windowClass) == 0) {
        return 1;
    }
    HWND window = CreateWindowExW(
        0, windowClassName, L"Aporia Game Base", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1280, 720, nullptr, nullptr, instance, nullptr);
    if (window == nullptr) {
        return 1;
    }
    ShowWindow(window, SW_SHOWNOACTIVATE);

    // Engine: init, world, bodies. Lines below are the whole engine setup.
    Aporia::Game game;
    Aporia::GraphicsContext context;
    context.nativeWindow = reinterpret_cast<std::uintptr_t>(window);
    Aporia::GraphicsConfig graphicsConfig;
    graphicsConfig.headless = false;
    if (!game.initGraphics(context, graphicsConfig)) {
        std::cerr << "initGraphics failed: " << game.graphics().lastError() << '\n';
        return 1;
    }

    Aporia::BodyDesc box;
    box.name = "box";
    box.meshKind = Aporia::MeshKind::Box;
    box.halfExtents = Aporia::Vec3{0.5f, 0.5f, 0.5f};
    box.transform.position = Aporia::Vec3{0.0, 0.0, 3.0};
    box.mass = 2.0;
    box.restitution = 0.3;
    const Aporia::Body fallingBox = game.createBody(box);

    // Terrain hills: mesh + heightfield collider, no asset files.
    Aporia::TerrainDesc terrainDescription;
    terrainDescription.seed = 5;
    terrainDescription.gridResolution = 96;
    terrainDescription.worldSize = 48.0f;
    terrainDescription.heightScale = 2.0f;
    terrainDescription.heightOffset = -1.5f;
    terrainDescription.noiseFrequency = 2.5f;
    terrainDescription.noiseOctaves = 4;
    const Aporia::Body terrainBody = game.createTerrain(terrainDescription);

    Aporia::CameraState camera;
    const Aporia::Mat4 view = Aporia::Mat4::lookAt({6.0, -6.0, 4.0}, {0.0, 0.0, 1.0}, {0.0, 0.0, 1.0});
    const Aporia::Mat4 projection = Aporia::Mat4::perspectiveVulkan(1.0, 1280.0 / 720.0, 0.1, 100.0);
    std::memcpy(camera.viewMatrix, &view, sizeof(view));
    std::memcpy(camera.projectionMatrix, &projection, sizeof(projection));
    camera.position[0] = 6.0;
    camera.position[1] = -6.0;
    camera.position[2] = 4.0;
    game.setCamera(camera);

    // Frame loop: user side; engine gives step(), input, render.
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);

        game.updateInput();
        Aporia::Vec3 push{};
        if (game.input().key(Aporia::KeyCode::W)) {
            push.y += 4.0;
        }
        if (game.input().key(Aporia::KeyCode::S)) {
            push.y -= 4.0;
        }
        if (game.input().key(Aporia::KeyCode::A)) {
            push.x -= 4.0;
        }
        if (game.input().key(Aporia::KeyCode::D)) {
            push.x += 4.0;
        }
        game.physics().applyForce(fallingBox.physicsBody(), push);

        game.step(1.0 / 60.0);

        std::vector<Aporia::RenderItem> items;
        for (const auto& body : {terrainBody, fallingBox}) {
            Aporia::RenderItem item;
            item.meshId = body.mesh().value();
            const Aporia::Transform* transform = game.scene().findComponent<Aporia::Transform>(
                body.object(), game.scene().componentTypeId("Transform"));
            const Aporia::Mat4 model = Aporia::Mat4::translation(transform->position) *
                                       Aporia::Mat4::fromQuaternion(transform->orientation);
            std::memcpy(item.modelMatrix, &model, sizeof(model));
            items.push_back(item);
        }
        if (!game.graphics().render(game.camera(), items)) {
            break;
        }
    }

    game.shutdownGraphics();
    return 0;
}
