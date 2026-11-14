// Aporia render test 13 — flat world free-look (SDL3 window).
//
// A flat ground plane with landmark blocks. Hold the RIGHT mouse button to
// capture the mouse and look around (down at the world, up into the procedural
// sky); WASD walks, Space/Ctrl fly up/down, Shift sprints.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <SDL3/SDL.h>

#include "Aporia/Game.hpp"
#include "Aporia/Graphics/MeshPrimitives.hpp"
#include "Aporia/Math/Matrix4.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr int kWindowWidth = 1280;
constexpr int kWindowHeight = 720;

std::string firstExistingFont() {
    const char* candidates[] = {
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/consola.ttf",
        "C:/Windows/Fonts/calibri.ttf",
    };
    for (const char* path : candidates) {
        std::ifstream file(path);
        if (file.good()) {
            return path;
        }
    }
    return {};
}

Aporia::Mat4 makeModel(const Aporia::Vec3& position, const Aporia::Vec3& scale) {
    Aporia::Mat4 model = Aporia::Mat4::translation(position);
    model.m[0][0] = scale.x;
    model.m[1][1] = scale.y;
    model.m[2][2] = scale.z;
    return model;
}

// The sky is at infinity: translating the camera must not change it, only
// rotating may. Renders the empty sky from different camera positions/angles.
bool headlessSkyCheck() {
    Aporia::Graphics graphics;
    Aporia::GraphicsConfig cfg;
    cfg.headless = true;
    cfg.headlessWidth = 256;
    cfg.headlessHeight = 256;
    cfg.quality = Aporia::GraphicsQuality::High;
    cfg.sky = true;
    if (!graphics.initialize({}, cfg)) {
        std::cerr << "headless init failed: " << graphics.lastError() << '\n';
        return false;
    }
    Aporia::EnvironmentState environment; // day sun

    const auto renderSky = [&](const Aporia::Vec3& eye, const Aporia::Vec3& target, std::vector<std::uint8_t>& out) {
        Aporia::CameraState camera;
        const Aporia::Mat4 view = Aporia::Mat4::lookAt(eye, target, {0.0, 0.0, 1.0});
        const Aporia::Mat4 projection = Aporia::Mat4::perspectiveVulkan(0.9, 1.0, 0.1, 1000.0);
        std::memcpy(camera.viewMatrix, &view, sizeof(view));
        std::memcpy(camera.projectionMatrix, &projection, sizeof(projection));
        camera.position[0] = eye.x;
        camera.position[1] = eye.y;
        camera.position[2] = eye.z;
        for (int frame = 0; frame < 3; ++frame) {
            (void)graphics.render(camera, environment, {});
        }
        (void)graphics.readbackPixels(out);
    };
    const auto differing = [](const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) {
        if (a.size() != b.size() || a.empty()) {
            return std::size_t{0};
        }
        std::size_t count = 0;
        for (std::size_t index = 0; index < a.size(); ++index) {
            if (a[index] != b[index]) {
                ++count;
            }
        }
        return count;
    };

    std::vector<std::uint8_t> base;
    std::vector<std::uint8_t> translated;
    std::vector<std::uint8_t> rotated;
    renderSky({0.0, 0.0, 5.0}, {0.0, 4.0, 5.6}, base);         // look forward+up
    renderSky({120.0, -80.0, 5.0}, {120.0, -76.0, 5.6}, translated); // same angle, moved
    renderSky({0.0, 0.0, 5.0}, {4.0, 0.0, 5.6}, rotated);       // same spot, turned

    graphics.shutdown();
    const std::size_t moveDiff = differing(base, translated);
    const std::size_t turnDiff = differing(base, rotated);
    std::cout << "Aporia sky self-check: translation-diff=" << moveDiff
              << " (~0 = world-anchored), rotation-diff=" << turnDiff << " (>0 = turns with the camera)\n";
    // Translation may differ only by float rounding; rotation must clearly change it.
    return turnDiff > 0 && moveDiff * 20 < turnDiff;
}

} // namespace

int main(int argc, char** argv) {
    double durationSeconds = 0.0;
    double startTimeHours = 7.0;
    bool headlessOnly = false;
    for (int index = 1; index < argc; ++index) {
        if (std::strcmp(argv[index], "--duration") == 0 && index + 1 < argc) {
            durationSeconds = std::atof(argv[++index]);
        } else if (std::strcmp(argv[index], "--headless") == 0) {
            headlessOnly = true;
        } else if (std::strcmp(argv[index], "--time") == 0 && index + 1 < argc) {
            const char* spec = argv[++index];
            startTimeHours = std::atof(spec);
            const char* colon = std::strchr(spec, ':');
            if (colon != nullptr) {
                startTimeHours += std::atof(colon + 1) / 60.0;
            }
        }
    }
    if (headlessOnly) {
        if (!headlessSkyCheck()) {
            std::cerr << "Aporia render test 13 sky self-check failed\n";
            return 1;
        }
        std::cout << "Aporia render test 13 headless self-check passed\n";
        return 0;
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
        return 1;
    }
    SDL_Window* window = SDL_CreateWindow("Aporia Render Test 13 - Flat World", kWindowWidth, kWindowHeight, 0);
    if (window == nullptr) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << '\n';
        SDL_Quit();
        return 1;
    }
    const HWND hwnd = static_cast<HWND>(SDL_GetPointerProperty(
        SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    Aporia::GraphicsContext context;
    context.nativeWindow = reinterpret_cast<std::uintptr_t>(hwnd);

    Aporia::Game game;
    Aporia::GraphicsConfig config;
    config.headless = false;
    config.quality = Aporia::GraphicsQuality::High;
    config.enableValidation = false;
    config.sky = true;
    config.reflections = false;
    config.sunDirection[0] = 0.3f;
    config.sunDirection[1] = -0.5f;
    config.sunDirection[2] = 0.8f;
    if (!game.initGraphics(context, config)) {
        std::cerr << "initGraphics failed: " << game.graphics().lastError() << '\n';
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    game.graphics().setVsync(false);
    // Sun follows the clock: start at morning, one day per two real minutes.
    game.graphics().setTimeOfDay(startTimeHours);
    game.graphics().setGameClock(2.0f, 1.0f);
    game.graphics().setTimeDrivenSun(true);
    int demoMonth = 6;
    game.graphics().setDate(demoMonth, 21); // summer solstice by default
    double dayLengthBias = 0.0;  // U/I: day-length trim in hours
    float gameDayMinutes = 2.0f; // O/P: real minutes per in-game day

    const Aporia::MeshHandle boxMesh = game.graphics().createMesh(Aporia::apr::makeBoxMesh(0.5f, 0.5f, 0.5f));
    const Aporia::FontHandle font = game.graphics().createFontFromTtf(firstExistingFont(), 44.0f);

    Aporia::EnvironmentState environment; // day sun: bright sky above

    // Flat world: one huge ground slab plus a grid of landmark blocks.
    struct Block {
        Aporia::Mat4 model;
        float color[3];
    };
    std::vector<Block> blocks;
    const auto addBlock = [&](const Aporia::Vec3& position, const Aporia::Vec3& scale, float r, float g, float b) {
        Block block;
        block.model = makeModel(position, scale);
        block.color[0] = r;
        block.color[1] = g;
        block.color[2] = b;
        blocks.push_back(block);
    };
    addBlock({0.0, 0.0, -0.1}, {4000.0, 4000.0, 0.2}, 0.30f, 0.32f, 0.30f); // ground (near true horizon)
    for (int gx = -10; gx <= 10; ++gx) {
        for (int gy = -10; gy <= 10; ++gy) {
            if ((gx & 1) || (gy & 1)) {
                continue; // sparse grid of markers
            }
            const float h = 0.5f + static_cast<float>((gx * 7 + gy * 3) & 3);
            const float tint = ((gx + gy) & 2) ? 0.8f : 0.6f;
            addBlock({static_cast<double>(gx) * 6.0, static_cast<double>(gy) * 6.0, h * 0.5},
                     {1.0, 1.0, h}, tint, tint * 0.9f + 0.1f, 0.5f);
        }
    }
    // A tall pillar right at the origin as a home marker.
    addBlock({0.0, 0.0, 6.0}, {1.2, 1.2, 12.0}, 0.85f, 0.35f, 0.2f);

    double position[3] = {0.0, -8.0, 1.7};
    double yaw = 3.14159265 * 0.5; // look toward +Y
    double pitch = -0.05;
    const double sensitivity = 0.0022;
    bool lookActive = false;
    bool running = true;
    double elapsed = 0.0;
    double previous = 0.0;
    LARGE_INTEGER frequency{};
    LARGE_INTEGER counter{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    previous = static_cast<double>(counter.QuadPart) / static_cast<double>(frequency.QuadPart);
    double smoothedFps = 60.0;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)) {
                running = false;
            } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_RIGHT) {
                lookActive = true;
                SDL_SetWindowRelativeMouseMode(window, true);
            } else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_RIGHT) {
                lookActive = false;
                SDL_SetWindowRelativeMouseMode(window, false);
            } else if (event.type == SDL_EVENT_KEY_DOWN) {
                switch (event.key.key) {
                    case SDLK_1: demoMonth = 12; break; // winter
                    case SDLK_2: demoMonth = 3; break;  // spring
                    case SDLK_3: demoMonth = 6; break;  // summer
                    case SDLK_4: demoMonth = 9; break;  // autumn
                    case SDLK_U: dayLengthBias += 1.0; break;  // day +1h
                    case SDLK_I: dayLengthBias -= 1.0; break;  // day -1h
                    case SDLK_O: gameDayMinutes += 1.0f; break; // game day +1 min
                    case SDLK_P: gameDayMinutes = std::max(1.0f, gameDayMinutes - 1.0f); break;
                    default: break;
                }
                if (event.key.key == SDLK_1 || event.key.key == SDLK_2 ||
                    event.key.key == SDLK_3 || event.key.key == SDLK_4) {
                    game.graphics().setDate(demoMonth, 21);
                }
                game.graphics().setDayLengthBias(dayLengthBias);
                game.graphics().setGameClock(gameDayMinutes, 1.0f);
            }
        }
        if (!running) {
            break;
        }

        QueryPerformanceCounter(&counter);
        const double now = static_cast<double>(counter.QuadPart) / static_cast<double>(frequency.QuadPart);
        const double dt = now - previous;
        previous = now;
        elapsed += dt;

        game.updateInput();

        if (lookActive) {
            float dx = 0.0f;
            float dy = 0.0f;
            (void)SDL_GetRelativeMouseState(&dx, &dy);
            yaw -= static_cast<double>(dx) * sensitivity;
            pitch -= static_cast<double>(dy) * sensitivity;
            pitch = std::clamp(pitch, -1.55, 1.55);
        }

        const bool* keys = SDL_GetKeyboardState(nullptr);
        const double speed = (keys[SDL_SCANCODE_LSHIFT] ? 40.0 : 12.0) * dt;
        const double forwardX = std::cos(yaw);
        const double forwardY = std::sin(yaw);
        if (keys[SDL_SCANCODE_W]) { position[0] += forwardX * speed; position[1] += forwardY * speed; }
        if (keys[SDL_SCANCODE_S]) { position[0] -= forwardX * speed; position[1] -= forwardY * speed; }
        if (keys[SDL_SCANCODE_D]) { position[0] += forwardY * speed; position[1] -= forwardX * speed; }
        if (keys[SDL_SCANCODE_A]) { position[0] -= forwardY * speed; position[1] += forwardX * speed; }
        if (keys[SDL_SCANCODE_SPACE]) { position[2] += speed; }
        if (keys[SDL_SCANCODE_LCTRL]) { position[2] -= speed; }

        const double cp = std::cos(pitch);
        const Aporia::Vec3 eye{position[0], position[1], position[2]};
        const Aporia::Vec3 forward{cp * std::cos(yaw), cp * std::sin(yaw), std::sin(pitch)};
        const Aporia::Vec3 target{eye.x + forward.x, eye.y + forward.y, eye.z + forward.z};
        const Aporia::Mat4 view = Aporia::Mat4::lookAt(eye, target, {0.0, 0.0, 1.0});
        int windowWidth = kWindowWidth;
        int windowHeight = kWindowHeight;
        SDL_GetWindowSize(window, &windowWidth, &windowHeight);
        const double aspect = windowHeight > 0 ? static_cast<double>(windowWidth) / static_cast<double>(windowHeight) : 1.0;
        const Aporia::Mat4 projection = Aporia::Mat4::perspectiveVulkan(0.9, aspect, 0.1, 20000.0);
        Aporia::CameraState camera;
        std::memcpy(camera.viewMatrix, &view, sizeof(view));
        std::memcpy(camera.projectionMatrix, &projection, sizeof(projection));
        camera.position[0] = eye.x;
        camera.position[1] = eye.y;
        camera.position[2] = eye.z;

        std::vector<Aporia::RenderItem> items;
        items.reserve(blocks.size());
        for (const Block& block : blocks) {
            Aporia::RenderItem item;
            item.meshId = boxMesh.value();
            std::memcpy(item.modelMatrix, &block.model, sizeof(block.model));
            item.color[0] = block.color[0];
            item.color[1] = block.color[1];
            item.color[2] = block.color[2];
            items.push_back(item);
        }

        Aporia::UiDrawList ui;
        ui.font = font;
        char line[192];
        const double tod = game.graphics().timeOfDay();
        const double rise = game.graphics().sunriseHour();
        const double set = game.graphics().sunsetHour();
        std::snprintf(line, sizeof(line),
                      "FLAT WORLD  pos %.1f %.1f %.1f  time %02d:%02d  sun %02d:%02d-%02d:%02d  day%+.0fh gday%.0fm  FPS %d",
                      position[0], position[1], position[2],
                      static_cast<int>(tod), static_cast<int>((tod - static_cast<int>(tod)) * 60.0),
                      static_cast<int>(rise), static_cast<int>((rise - static_cast<int>(rise)) * 60.0),
                      static_cast<int>(set), static_cast<int>((set - static_cast<int>(set)) * 60.0),
                      dayLengthBias, gameDayMinutes, static_cast<int>(smoothedFps));
        Aporia::UiTextRun run;
        run.text = line;
        run.x = 14.0f;
        run.y = 12.0f;
        run.size = 16.0f;
        run.color = {0.9f, 0.95f, 1.0f, 1.0f};
        ui.textRuns.push_back(run);
        Aporia::UiTextRun hint;
        hint.text = "HOLD RMB look (up = sky)  WASD move  Space/Ctrl up-down  1/2/3/4 season  U/I day +/-1h  O/P gameday +/-1m  ESC";
        hint.x = 14.0f;
        hint.y = 34.0f;
        hint.size = 14.0f;
        hint.color = {0.7f, 0.8f, 0.9f, 1.0f};
        ui.textRuns.push_back(hint);
        game.graphics().setUiDrawList(std::move(ui));

        const bool rendered = game.graphics().render(camera, environment, items);
        if (!rendered) {
            const std::string& error = game.graphics().lastError();
            if (error.find("out of date") == std::string::npos &&
                error.find("recreated") == std::string::npos) {
                std::cerr << "render failed: " << error << '\n';
                break;
            }
        }
        if (dt > 0.0) {
            smoothedFps = smoothedFps * 0.9 + (1.0 / dt) * 0.1;
        }
        if (durationSeconds > 0.0 && elapsed >= durationSeconds) {
            running = false;
        }
    }

    std::printf("Aporia render test 13 finished after %.1fs\n", elapsed);
    game.graphics().destroyMesh(boxMesh);
    game.shutdownGraphics();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
