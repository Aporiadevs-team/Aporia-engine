// Aporia render test 11 вЂ” multithreaded render lab (SDL3 window).
//
// Three panes over one frame:
//   left   - cubes falling under physics (physics + worker-thread workload)
//   center - text rotating clockwise (graphics; future: per-pass sub-buffers)
//   right  - a circle driven by WASD, or automatically with --auto
//
// Full frame accounting is measured (input, physics step, item build, UI build,
// render, total loop), reported live on the HUD and summarized on exit. Press M
// to toggle multithreaded recording (experimental) and compare live.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <SDL3/SDL.h>

#include "Aporia/Game.hpp"
#include "Aporia/Body.hpp"
#include "Aporia/Graphics/MeshPrimitives.hpp"
#include "Aporia/Math/Matrix4.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr int kWindowWidth = 1280;
constexpr int kWindowHeight = 720;
constexpr int kCubeCount = 8;

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

double nowSeconds() {
    LARGE_INTEGER counter{};
    LARGE_INTEGER frequency{};
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    return static_cast<double>(counter.QuadPart) / static_cast<double>(frequency.QuadPart);
}

struct Series {
    std::vector<double> samples;
    void add(double ms) { samples.push_back(ms); }
    double avg() const {
        if (samples.empty()) return 0.0;
        double sum = 0.0;
        for (double value : samples) sum += value;
        return sum / static_cast<double>(samples.size());
    }
    double percentile(double p) const {
        if (samples.empty()) return 0.0;
        std::vector<double> sorted = samples;
        std::sort(sorted.begin(), sorted.end());
        const std::size_t index = static_cast<std::size_t>(static_cast<double>(sorted.size() - 1) * p);
        return sorted[index];
    }
    double minimum() const {
        return samples.empty() ? 0.0 : *std::min_element(samples.begin(), samples.end());
    }
    double maximum() const {
        return samples.empty() ? 0.0 : *std::max_element(samples.begin(), samples.end());
    }
};

struct Meters {
    Series input;
    Series physics;
    Series build;
    Series ui;
    Series render;
    Series loop;
};

void report(const char* label, const Series& series) {
    std::printf("  %-8s avg %7.3f  p95 %7.3f  min %7.3f  max %7.3f ms\n",
                label, series.avg(), series.percentile(0.95), series.minimum(), series.maximum());
}

Aporia::CameraState labCamera() {
    const Aporia::Vec3 eye{4.0, -10.0, 5.0};
    const Aporia::Vec3 target{4.0, 0.0, 3.0};
    Aporia::CameraState camera;
    const Aporia::Mat4 view = Aporia::Mat4::lookAt(eye, target, {0.0, 0.0, 1.0});
    const Aporia::Mat4 projection = Aporia::Mat4::perspectiveVulkan(
        0.9, static_cast<double>(kWindowWidth) / static_cast<double>(kWindowHeight), 0.1, 200.0);
    std::memcpy(camera.viewMatrix, &view, sizeof(view));
    std::memcpy(camera.projectionMatrix, &projection, sizeof(projection));
    camera.position[0] = eye.x;
    camera.position[1] = eye.y;
    camera.position[2] = eye.z;
    return camera;
}

} // namespace

int main(int argc, char** argv) {
    bool autoInput = false;
    bool syncAfterRender = false; // --sync: vkDeviceWaitIdle each frame (accurate GPU ms, caps fps)
    bool noPhysics = false;       // --nophys: skip the physics step (bisect the frame budget)
    bool noRender = false;        // --norender: skip submit/present (bisect the frame budget)
    int shadowOverride = 0;       // --shadow N: override the shadow map size
    Aporia::GraphicsQuality quality = Aporia::GraphicsQuality::High;
    double durationSeconds = 0.0; // 0 -> run until ESC
    for (int index = 1; index < argc; ++index) {
        if (std::strcmp(argv[index], "--auto") == 0) {
            autoInput = true;
        } else if (std::strcmp(argv[index], "--sync") == 0) {
            syncAfterRender = true;
        } else if (std::strcmp(argv[index], "--nophys") == 0) {
            noPhysics = true;
        } else if (std::strcmp(argv[index], "--norender") == 0) {
            noRender = true;
        } else if (std::strcmp(argv[index], "--low") == 0) {
            quality = Aporia::GraphicsQuality::Low;
        } else if (std::strcmp(argv[index], "--medium") == 0) {
            quality = Aporia::GraphicsQuality::Medium;
        } else if (std::strcmp(argv[index], "--high") == 0) {
            quality = Aporia::GraphicsQuality::High;
        } else if (std::strcmp(argv[index], "--shadow") == 0 && index + 1 < argc) {
            shadowOverride = std::atoi(argv[++index]);
        } else if (std::strcmp(argv[index], "--duration") == 0 && index + 1 < argc) {
            durationSeconds = std::atof(argv[++index]);
        }
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
        return 1;
    }
    SDL_Window* window = SDL_CreateWindow("Aporia Render Test 11 - Multithreaded Lab",
                                          kWindowWidth, kWindowHeight, 0);
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
    config.quality = quality;
    config.enableValidation = false;
    config.sunDirection[0] = 0.25f;
    config.sunDirection[1] = -0.6f;
    config.sunDirection[2] = 0.75f;
    if (!game.initGraphics(context, config)) {
        std::cerr << "initGraphics failed: " << game.graphics().lastError() << '\n';
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    game.graphics().setVsync(false); // measure raw throughput
    if (shadowOverride > 0) {
        Aporia::ShadowConfig shadowConfig = game.graphics().shadowConfig();
        shadowConfig.mapSize = static_cast<std::uint32_t>(shadowOverride);
        game.graphics().setShadowConfig(shadowConfig);
    }

    const Aporia::MeshHandle boxMesh = game.graphics().createMesh(Aporia::apr::makeBoxMesh(0.5f, 0.5f, 0.5f));
    const Aporia::FontHandle font = game.graphics().createFontFromTtf(firstExistingFont(), 48.0f);

    // Physics: a static ground and a row of falling cubes on the left pane.
    game.physics().setGravity(Aporia::Vec3{0.0, 0.0, -9.81});
    Aporia::BoxBodyConfig groundConfig;
    groundConfig.halfExtents = Aporia::Vec3{8.0, 8.0, 0.5};
    groundConfig.position = Aporia::Vec3{0.0, 0.0, -0.5};
    groundConfig.isStatic = true;
    groundConfig.friction = 0.8;
    const Aporia::BodyHandle ground = game.physics().createBox(groundConfig);

    std::vector<Aporia::BodyHandle> cubes;
    cubes.reserve(kCubeCount);
    const auto resetCube = [&](std::size_t index) {
        Aporia::BoxBodyConfig cubeConfig;
        cubeConfig.halfExtents = Aporia::Vec3{0.5, 0.5, 0.5};
        cubeConfig.position = Aporia::Vec3{
            static_cast<double>(index) - static_cast<double>(kCubeCount) * 0.5, 0.0, 6.0 + static_cast<double>(index)};
        cubeConfig.mass = 20.0;
        cubeConfig.restitution = 0.25;
        cubeConfig.friction = 0.6;
        if (index < cubes.size() && cubes[index].valid()) {
            game.physics().setBodyPosition(cubes[index], cubeConfig.position);
            game.physics().setBodyVelocity(cubes[index], Aporia::Vec3{0.0, 0.0, 0.0});
        } else {
            cubes.push_back(game.physics().createBox(cubeConfig));
        }
    };
    for (std::size_t index = 0; index < kCubeCount; ++index) {
        resetCube(index);
    }

    // Input pane player circle.
    float playerX = 1060.0f;
    float playerY = 360.0f;
    constexpr float kPlayerRadius = 20.0f;

    static constexpr std::array<std::string_view, 8> kRingWords{
        "APORIA", "MT", "RENDER", "TEST", "11", "PHYSICS", "LIGHT", "GPU"};

    Meters meters;
    bool running = true;
    bool multithreaded = true;
    double elapsed = 0.0;
    double lastReport = 0.0;
    double smoothedFps = 60.0;
    double previousLoop = nowSeconds();
    std::array<std::string, 4> hudLines;

    while (running) {
        const double frameStart = nowSeconds();
        const double dt = frameStart - previousLoop;
        previousLoop = frameStart;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)) {
                running = false;
            }
        }
        if (!running) {
            break;
        }

        const double sectionStart = nowSeconds();
        game.updateInput();
        Aporia::Input& input = game.input();

        if (input.keyPressed(Aporia::KeyCode::M)) {
#if defined(APORIA_EXPERIMENTAL)
            multithreaded = !multithreaded;
            game.graphics().setMultithreadedRender(multithreaded);
#endif
        }

        const double inputMs = (nowSeconds() - sectionStart) * 1000.0;

        // Right pane: WASD (or automatic lissajous motion with --auto).
        const double moveSpeed = 340.0 * dt;
        if (autoInput) {
            playerX = 1060.0f + static_cast<float>(std::sin(elapsed * 1.7) * 120.0);
            playerY = 360.0f + static_cast<float>(std::cos(elapsed * 1.1) * 180.0);
        } else {
            if (input.key(Aporia::KeyCode::W)) playerY -= static_cast<float>(moveSpeed);
            if (input.key(Aporia::KeyCode::S)) playerY += static_cast<float>(moveSpeed);
            if (input.key(Aporia::KeyCode::A)) playerX -= static_cast<float>(moveSpeed);
            if (input.key(Aporia::KeyCode::D)) playerX += static_cast<float>(moveSpeed);
        }
        playerX = std::clamp(playerX, 880.0f + kPlayerRadius, static_cast<float>(kWindowWidth) - kPlayerRadius);
        playerY = std::clamp(playerY, 64.0f + kPlayerRadius, static_cast<float>(kWindowHeight) - kPlayerRadius);

        // Left pane: physics.
        const double physicsStart = nowSeconds();
        if (!noPhysics) {
            // Real delta: Game::step runs 1/300 fixed substeps internally; a fixed
            // 1/60 argument would force ~5 substeps every frame.
            game.step(std::min(dt, 1.0 / 30.0));
            for (std::size_t index = 0; index < cubes.size(); ++index) {
                const Aporia::Vec3 position = game.physics().bodyPosition(cubes[index]);
                if (position.z < 0.5 || position.z > 12.0) {
                    resetCube(index);
                }
            }
        }
        const double physicsMs = (nowSeconds() - physicsStart) * 1000.0;

        // Left pane render items.
        const double buildStart = nowSeconds();
        std::vector<Aporia::RenderItem> items;
        items.reserve(cubes.size());
        for (const Aporia::BodyHandle handle : cubes) {
            if (!handle.valid()) continue;
            const Aporia::Vec3 position = game.physics().bodyPosition(handle);
            const Aporia::Quat orientation = game.physics().bodyOrientation(handle);
            const Aporia::Mat4 model = Aporia::Mat4::translation(position) * Aporia::Mat4::fromQuaternion(orientation);
            Aporia::RenderItem item;
            item.meshId = boxMesh.value();
            std::memcpy(item.modelMatrix, &model, sizeof(model));
            item.color[0] = 0.85f;
            item.color[1] = 0.72f;
            item.color[2] = 0.45f;
            items.push_back(item);
        }
        const Aporia::CameraState camera = labCamera();
        const double buildMs = (nowSeconds() - buildStart) * 1000.0;

        // UI: rotating text ring (center) + player circle (right) + HUD.
        const double uiStart = nowSeconds();
        Aporia::UiDrawList ui;
        ui.font = font;
        ui.cornerRadius = kPlayerRadius;
        const float centerX = static_cast<float>(kWindowWidth) * 0.5f;
        const float centerY = static_cast<float>(kWindowHeight) * 0.5f;
        for (std::size_t index = 0; index < kRingWords.size(); ++index) {
            const double angle = elapsed * 1.6 - static_cast<double>(index) *
                (6.283185307179586 / static_cast<double>(kRingWords.size()));
            Aporia::UiTextRun run;
            run.text = kRingWords[index];
            run.x = centerX + static_cast<float>(std::cos(angle) * 150.0) - 30.0f;
            run.y = centerY + static_cast<float>(std::sin(angle) * 150.0) - 10.0f;
            run.size = 22.0f;
            run.color = {0.65f, 0.85f, 1.0f, 1.0f};
            ui.textRuns.push_back(run);
        }
        ui.roundedRects.push_back({
            Aporia::UiRect{playerX - kPlayerRadius, playerY - kPlayerRadius, kPlayerRadius * 2.0f, kPlayerRadius * 2.0f},
            Aporia::UiColor{0.35f, 0.95f, 0.55f, 1.0f}});

        const double fps = smoothedFps;
        hudLines[0] = "MT " + std::string(multithreaded ? "ON " : "OFF") +
            "   FPS " + std::to_string(static_cast<int>(fps)) +
            "   loop " + std::to_string(meters.loop.avg()) + " ms" +
            "   render " + std::to_string(meters.render.avg()) + " ms";
        hudLines[1] = "physics " + std::to_string(meters.physics.avg()) +
            " ms   build " + std::to_string(meters.build.avg()) +
            " ms   ui " + std::to_string(meters.ui.avg()) + " ms";
        hudLines[2] = autoInput ? "MOVE auto (lissajous)"
                                : "MOVE W/A/S/D   M toggle MT   ESC quit";
        hudLines[3] = "cubes " + std::to_string(cubes.size()) +
            "   collisions " + std::to_string(game.physics().lastStepCollisionCount());
        for (std::size_t index = 0; index < hudLines.size(); ++index) {
            Aporia::UiTextRun line;
            line.text = hudLines[index];
            line.x = 14.0f;
            line.y = 12.0f + static_cast<float>(index) * 22.0f;
            line.size = 16.0f;
            line.color = {1.0f, 0.93f, 0.4f, 1.0f};
            ui.textRuns.push_back(line);
        }
        game.graphics().setUiDrawList(std::move(ui));
        const double uiMs = (nowSeconds() - uiStart) * 1000.0;

        const double renderStart = nowSeconds();
        bool rendered = true;
        if (!noRender) {
            rendered = game.graphics().render(camera, items);
            if (syncAfterRender) {
                game.graphics().waitIdle();
            }
        }
        const double renderMs = (nowSeconds() - renderStart) * 1000.0;
        if (!rendered) {
            const std::string& error = game.graphics().lastError();
            if (error.find("out of date") == std::string::npos &&
                error.find("recreated") == std::string::npos) {
                std::cerr << "render failed: " << error << '\n';
                break;
            }
        }

        const double frameMs = (nowSeconds() - frameStart) * 1000.0;
        elapsed += frameMs / 1000.0;
        if (frameMs > 0.0) {
            smoothedFps = smoothedFps * 0.9 + (1000.0 / frameMs) * 0.1;
        }
        meters.input.add(inputMs);
        meters.physics.add(physicsMs);
        meters.build.add(buildMs);
        meters.ui.add(uiMs);
        meters.render.add(renderMs);
        meters.loop.add(frameMs);

        if (elapsed - lastReport >= 1.0) {
            lastReport = elapsed;
            std::printf("[t+%.1fs] %s fps %.1f  loop %.2f  render %.2f  phys %.2f  build %.2f  ui %.2f ms  coll %zu\n",
                        elapsed, multithreaded ? "MT" : "ST", smoothedFps, meters.loop.avg(), meters.render.avg(),
                        meters.physics.avg(), meters.build.avg(), meters.ui.avg(),
                        game.physics().lastStepCollisionCount());
        }

        if (durationSeconds > 0.0 && elapsed >= durationSeconds) {
            running = false;
        }
    }

    const char* qualityName = quality == Aporia::GraphicsQuality::Low ? "Low"
                            : quality == Aporia::GraphicsQuality::Medium ? "Medium" : "High";
    const Aporia::GraphicsStats summaryStats = game.graphics().stats();
    std::printf("Aporia render test 11 summary (frames=%zu, %s, preset=%s, shadow=%u, hdr=%d, refl=%d)\n",
                meters.loop.samples.size(), multithreaded ? "MT" : "ST", qualityName,
                summaryStats.shadowMapSize, summaryStats.hdrEnabled ? 1 : 0, summaryStats.reflectionsEnabled ? 1 : 0);
    report("input", meters.input);
    report("physics", meters.physics);
    report("build", meters.build);
    report("ui", meters.ui);
    report("render", meters.render);
    report("loop", meters.loop);
    const double avgLoop = meters.loop.avg();
    std::printf("  fps avg %.1f\n", avgLoop > 0.0 ? 1000.0 / avgLoop : 0.0);

    (void)ground;
    game.graphics().destroyMesh(boxMesh);
    game.shutdownGraphics();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
