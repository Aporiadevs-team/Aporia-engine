// Render test 3 — infinite city map.
// SDL3 owns the window and event loop. Aporia owns render, physics, input.
// Top-down streamed city: LMB drag pans, wheel zooms, WASD drives a probe
// dot; the host prints the zone under the dot. City chunks stream around the
// camera — walk any direction forever.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <SDL3/SDL.h>

#include "Aporia/Game.hpp"
#include "Aporia/City/City.hpp"

#include <cstring>
#include <iostream>
#include <vector>

namespace {

struct PaletteEntry { std::uint8_t r, g, b, a; };
// Slots: 0 road, 1 avenue, 2 highway, 3 sidewalk, 4 lot, 5 park,
// 6 building light, 7 building mid, 8 building dark, 9 tower glass,
// 10 roof, 11 field, 12 house.
constexpr PaletteEntry palette[16] = {
    { 62,  62,  70, 255},  // road
    { 80,  80,  90, 255},  // avenue
    {140,  70,  55, 255},  // highway
    {150, 150, 158, 255},  // sidewalk / yard
    {105, 105,  95, 255},  // lot ground
    { 70, 140,  60, 255},  // park
    {185, 175, 155, 255},  // building light
    {145, 140, 132, 255},  // building mid
    {105, 100,  95, 255},  // building dark
    {110, 150, 175, 255},  // tower glass
    { 85,  78,  70, 255},  // roof
    {125, 135,  95, 255},  // field
    {195, 150, 110, 255},  // house
    {255,   0, 255, 255},
    {255,   0, 255, 255},
    {255,   0, 255, 255},
};

const char* zoneName(Aporia::CityZone zone) {
    switch (zone) {
    case Aporia::CityZone::Field: return "fields";
    case Aporia::CityZone::Highway: return "HIGHWAY";
    case Aporia::CityZone::Avenue: return "avenue";
    case Aporia::CityZone::Street: return "street";
    case Aporia::CityZone::Sidewalk: return "yard";
    case Aporia::CityZone::Lot: return "lot";
    case Aporia::CityZone::Park: return "park";
    default: return "?";
    }
}

const char* districtName(Aporia::CityDistrict district) {
    switch (district) {
    case Aporia::CityDistrict::Downtown: return "downtown";
    case Aporia::CityDistrict::Core: return "core";
    case Aporia::CityDistrict::Residential: return "residential";
    case Aporia::CityDistrict::Outskirts: return "outskirts";
    default: return "countryside";
    }
}

}

int main() {
    if (SDL_Init(SDL_INIT_VIDEO) != true) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("Aporia Render Test 3 — Infinite City", 1280, 720, SDL_WINDOW_RESIZABLE);
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

    Aporia::CityDesc cityDescription;
    cityDescription.seed = 2026;
    Aporia::CityWorld cityWorld(cityDescription);

    // 16x1 palette texture bound as the albedo; mesh UV.x selects the slot.
    std::vector<std::uint8_t> palettePixels;
    palettePixels.reserve(16 * 4);
    for (const PaletteEntry& entry : palette) {
        palettePixels.insert(palettePixels.end(), {entry.r, entry.g, entry.b, entry.a});
    }
    Aporia::TextureDesc paletteDescription;
    paletteDescription.width = 16;
    paletteDescription.height = 1;
    paletteDescription.format = Aporia::TextureFormat::Rgba8Srgb;
    paletteDescription.pixels = palettePixels.data();
    paletteDescription.dataSize = palettePixels.size();
    const Aporia::TextureHandle paletteTexture = game.graphics().createTexture2D(paletteDescription);
    if (paletteTexture.valid()) {
        game.graphics().setTexture2D(paletteTexture);
    } else {
        std::cerr << "palette texture failed: " << game.graphics().lastError() << '\n';
    }

    Aporia::Vec3 viewCenter{0.0, 0.0, 0.0};
    double zoom = 1.2;
    bool dragging = false;
    std::uint32_t frameCounter = 0;

    bool running = true;
    while (running) {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
                running = false;
            }
        }

        int windowWidth = 1280;
        int windowHeight = 720;
        SDL_GetWindowSize(window, &windowWidth, &windowHeight);

        game.updateInput();

        // LMB drag pans the map.
        const Aporia::MouseState mouse = game.input().mouse();
        const bool leftDown = game.input().mouseButton(Aporia::MouseButton::Left);
        static Aporia::MouseState lastMouse{};
        if (leftDown && !dragging) {
            dragging = true;
            lastMouse = mouse;
        }
        if (!leftDown) {
            dragging = false;
        }
        const double metersPerPixel = 1.0 / zoom;
        if (dragging) {
            viewCenter.x -= (mouse.x - lastMouse.x) * metersPerPixel;
            viewCenter.y += (mouse.y - lastMouse.y) * metersPerPixel;
            lastMouse = mouse;
        }

        // Wheel zoom anchored at the cursor.
        const double scroll = game.input().scrollDelta();
        if (scroll != 0.0) {
            const double oldMetersPerPixel = metersPerPixel;
            zoom = std::max(0.15, std::min(40.0, zoom * (1.0 + scroll * 0.15)));
            const double newMetersPerPixel = 1.0 / zoom;
            const double anchorX = mouse.x - windowWidth * 0.5;
            const double anchorY = windowHeight * 0.5 - mouse.y;
            viewCenter.x += anchorX * (oldMetersPerPixel - newMetersPerPixel);
            viewCenter.y += anchorY * (oldMetersPerPixel - newMetersPerPixel);
        }

        // WASD moves the view (map panning); the zone probe is the view center.
        const double moveSpeed = 500.0 / zoom;
        if (game.input().key(Aporia::KeyCode::W)) viewCenter.y += moveSpeed / 60.0;
        if (game.input().key(Aporia::KeyCode::S)) viewCenter.y -= moveSpeed / 60.0;
        if (game.input().key(Aporia::KeyCode::A)) viewCenter.x -= moveSpeed / 60.0;
        if (game.input().key(Aporia::KeyCode::D)) viewCenter.x += moveSpeed / 60.0;

        game.step(1.0 / 60.0);

        // Stream city chunks around the view.
        const double halfHeight = static_cast<double>(windowHeight) / zoom * 0.5;
        cityWorld.update(viewCenter.x, viewCenter.y, halfHeight * 1.4, &game.graphics());

        ++frameCounter;
        if ((frameCounter % 20) == 0) {
            const Aporia::CitySample sample = cityWorld.sampleAt(viewCenter.x, viewCenter.y);
            std::cout << "[" << districtName(sample.district) << " / " << zoneName(sample.zone)
                      << "] pos(" << static_cast<int>(viewCenter.x)
                      << ", " << static_cast<int>(viewCenter.y)
                      << ") blocks " << cityWorld.loadedChunkCount()
                      << " zoom " << static_cast<int>(zoom * 100) << "%\n";
        }

        const double halfWidth = halfHeight * static_cast<double>(windowWidth) / static_cast<double>(windowHeight);
        Aporia::CameraState camera;
        const Aporia::Mat4 view = Aporia::Mat4::lookAt(
            {viewCenter.x, viewCenter.y, 400.0},
            {viewCenter.x, viewCenter.y, 0.0},
            {0.0, 1.0, 0.0});
        const Aporia::Mat4 projection = Aporia::Mat4::orthographicVulkan(
            halfWidth * 2.0, halfHeight * 2.0, 1.0, 1000.0);
        std::memcpy(camera.viewMatrix, &view, sizeof(view));
        std::memcpy(camera.projectionMatrix, &projection, sizeof(projection));
        camera.position[0] = viewCenter.x;
        camera.position[1] = viewCenter.y;
        camera.position[2] = 400.0;
        game.setCamera(camera);

        std::vector<Aporia::RenderItem> items;
        for (const Aporia::CityWorld::Visible& chunk : cityWorld.visibleChunks()) {
            Aporia::RenderItem item;
            item.meshId = chunk.mesh.value();
            const Aporia::Mat4 model = Aporia::Mat4::identity();
            std::memcpy(item.modelMatrix, &model, sizeof(model));
            items.push_back(item);
        }

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
