// Aporia render test 10 — graphics load lab.
// SDL3 owns the window; Aporia owns render, physics, input and text. Bodies are
// spawned in the air over time and fall onto the terrain, so every quality
// preset (shadows, HDR, reflections, AA, LOD) can be stressed while the HUD
// reports FPS, frame time, body count and the render/pipeline stats that show
// where the frame budget actually goes.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <SDL3/SDL.h>

#include "Aporia/Game.hpp"
#include "Aporia/Body.hpp"
#include "Aporia/Graphics/MeshPrimitives.hpp"
#include "Aporia/Math/Matrix4.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr std::size_t kMaxBodies = 6000;
constexpr std::uint32_t kBurst = 100;

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

// Deterministic cheap RNG so runs are comparable.
struct Rng {
    std::uint64_t state = 0x9E3779B97F4A7C15ull;
    double next() {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return static_cast<double>(state >> 11) / static_cast<double>(1ull << 53);
    }
    double range(double low, double high) {
        return low + (high - low) * next();
    }
};

struct Spawned {
    Aporia::BodyHandle physics;
    Aporia::MeshHandle mesh;
    Aporia::Vec3 position;
    Aporia::Quat orientation;
    bool dynamic = false;
    float color[3];
};

struct Lab {
    std::unique_ptr<Aporia::Game> game;
    std::vector<Spawned> bodies;
    Aporia::MeshHandle boxMesh;
    Aporia::MeshHandle sphereMesh;
    Aporia::MeshHandle capsuleMesh;
    Aporia::Body terrain;
    Aporia::FontHandle font;
    Rng rng;
    bool staticBodies = false;
    std::size_t dynamicCount = 0;
};

bool initLab(Lab& lab, const Aporia::GraphicsContext& context, Aporia::GraphicsQuality quality,
             bool headless, bool shadowsEnabled = true, bool staticBodies = false) {
    lab.staticBodies = staticBodies;
    lab.game = std::make_unique<Aporia::Game>();
    Aporia::GraphicsConfig config;
    config.headless = headless;
    config.enableValidation = false;
    config.quality = quality;
    if (headless) {
        // Headless benchmark renders at a representative resolution, not 64x64.
        config.headlessWidth = 1280;
        config.headlessHeight = 720;
    }
    config.sunDirection[0] = 0.25f;
    config.sunDirection[1] = -0.6f;
    config.sunDirection[2] = 0.75f;
    if (!lab.game->initGraphics(headless ? Aporia::GraphicsContext{} : context, config)) {
        std::cerr << "initGraphics failed: " << lab.game->graphics().lastError() << '\n';
        return false;
    }
    if (!shadowsEnabled) {
        Aporia::ShadowConfig shadowConfig = lab.game->graphics().shadowConfig();
        shadowConfig.enabled = false;
        lab.game->graphics().setShadowConfig(shadowConfig);
    }
    if (!headless) {
        lab.game->graphics().setVsync(false); // load lab must not be capped at 60 Hz
    }

    Aporia::TerrainDesc terrain;
    terrain.seed = 21;
    terrain.gridResolution = 128;
    terrain.worldSize = 240.0f;
    terrain.heightScale = 2.0f;
    terrain.heightOffset = -0.5f;
    terrain.noiseFrequency = 1.4f;
    terrain.noiseOctaves = 4;
    lab.terrain = lab.game->createTerrain(terrain);
    lab.game->physics().setBodyFriction(lab.terrain.physicsBody(), 0.9);

    lab.boxMesh = lab.game->graphics().createMesh(Aporia::apr::makeBoxMesh(0.5f, 0.5f, 0.5f));
    lab.sphereMesh = lab.game->graphics().createMesh(Aporia::apr::makeSphereMesh(0.5f, 20, 12));
    lab.capsuleMesh = lab.game->graphics().createMesh(Aporia::apr::makeCapsuleMesh(0.35f, 0.5f, 16, 8));

    if (!headless) {
        const std::string fontPath = firstExistingFont();
        if (!fontPath.empty()) {
            lab.font = lab.game->graphics().createFontFromTtf(fontPath, 20.0f);
        }
    }
    lab.bodies.reserve(kMaxBodies);
    return true;
}

void spawnBodies(Lab& lab, std::uint32_t count) {
    for (std::uint32_t index = 0; index < count && lab.bodies.size() < kMaxBodies; ++index) {
        const int kind = static_cast<int>(lab.rng.range(0.0, 3.0));
        Aporia::MeshHandle mesh = lab.boxMesh;
        Aporia::BoxBodyConfig box;
        Aporia::SphereBodyConfig sphere;
        Aporia::CapsuleBodyConfig capsule;
        Aporia::BodySettings* settings = nullptr;
        if (kind == 1) {
            mesh = lab.sphereMesh;
            sphere.radius = 0.5;
            settings = &sphere;
        } else if (kind == 2) {
            mesh = lab.capsuleMesh;
            capsule.radius = 0.35;
            capsule.halfHeight = 0.5;
            settings = &capsule;
        } else {
            box.halfExtents = Aporia::Vec3{0.5, 0.5, 0.5};
            settings = &box;
        }
        settings->position = Aporia::Vec3{
            lab.rng.range(-70.0, 70.0),
            lab.rng.range(-70.0, 70.0),
            lab.rng.range(12.0, 55.0)};
        settings->mass = 20.0;
        settings->restitution = 0.05;
        settings->friction = 0.7;
        settings->isStatic = lab.staticBodies;

        Aporia::BodyHandle handle;
        if (kind == 1) {
            handle = lab.game->physics().createSphere(sphere);
        } else if (kind == 2) {
            handle = lab.game->physics().createCapsule(capsule);
        } else {
            handle = lab.game->physics().createBox(box);
        }
        if (!handle.valid()) {
            continue;
        }
        Spawned spawned;
        spawned.physics = handle;
        spawned.mesh = mesh;
        spawned.position = settings->position;
        spawned.orientation = Aporia::Quat::identity();
        spawned.dynamic = !lab.staticBodies;
        spawned.color[0] = static_cast<float>(lab.rng.range(0.3, 1.0));
        spawned.color[1] = static_cast<float>(lab.rng.range(0.3, 1.0));
        spawned.color[2] = static_cast<float>(lab.rng.range(0.3, 1.0));
        lab.bodies.push_back(spawned);
        if (!lab.staticBodies) {
            ++lab.dynamicCount;
        }
    }
}

Aporia::CameraState labCamera(double seconds) {
    const double angle = 0.35 + seconds * 0.05;
    const Aporia::Vec3 eye{95.0 * std::cos(angle), 95.0 * std::sin(angle), 70.0};
    const Aporia::Vec3 target{0.0, 0.0, 6.0};
    const Aporia::Mat4 view = Aporia::Mat4::lookAt(eye, target, {0.0, 0.0, 1.0});
    const Aporia::Mat4 projection = Aporia::Mat4::perspectiveVulkan(0.95, 1280.0 / 720.0, 0.1, 600.0);
    Aporia::CameraState camera;
    std::memcpy(camera.viewMatrix, &view, sizeof(view));
    std::memcpy(camera.projectionMatrix, &projection, sizeof(projection));
    camera.position[0] = eye.x;
    camera.position[1] = eye.y;
    camera.position[2] = eye.z;
    return camera;
}

// Headless proof that the lab can render a body pile at the chosen preset.
bool headlessSelfCheck() {
    Lab lab;
    if (!initLab(lab, {}, Aporia::GraphicsQuality::High, true)) {
        return false;
    }
    spawnBodies(lab, 500);
    const Aporia::CameraState camera = labCamera(0.0);
    bool ok = true;
    for (int frame = 0; frame < 20; ++frame) {
        std::vector<Aporia::RenderItem> items;
        items.reserve(lab.bodies.size());
        for (const Spawned& body : lab.bodies) {
            Aporia::RenderItem item;
            item.meshId = body.mesh.value();
            const Aporia::Vec3 position = lab.game->physics().bodyPosition(body.physics);
            const Aporia::Quat orientation = lab.game->physics().bodyOrientation(body.physics);
            const Aporia::Mat4 model = Aporia::Mat4::translation(position) * Aporia::Mat4::fromQuaternion(orientation);
            std::memcpy(item.modelMatrix, &model, sizeof(model));
            item.color[0] = body.color[0];
            item.color[1] = body.color[1];
            item.color[2] = body.color[2];
            items.push_back(item);
        }
        lab.game->step(1.0 / 60.0);
        ok = ok && lab.game->graphics().render(camera, items);
    }
    const Aporia::GraphicsStats stats = lab.game->graphics().stats();
    std::cout << "[labcheck] bodies=" << lab.bodies.size()
              << " avgFrameMs=" << stats.avgFrameMs
              << " deviceLocalMB=" << (stats.deviceLocalMemory / (1024ull * 1024ull)) << '\n';
        lab.game->shutdownGraphics();
    return ok;
}

// Builds the render items: the terrain floor first, then every body. Dynamic
// bodies refresh their transform from physics; static ones reuse the spawn
// transform, so no per-body O(n) physics lookup is needed for the bulk.
std::vector<Aporia::RenderItem> buildItems(Lab& lab) {
    std::vector<Aporia::RenderItem> items;
    items.reserve(lab.bodies.size() + 1);
    if (lab.terrain.mesh().valid()) {
        Aporia::RenderItem terrainItem;
        terrainItem.meshId = lab.terrain.mesh().value();
        const Aporia::Mat4 identity = Aporia::Mat4::identity();
        std::memcpy(terrainItem.modelMatrix, &identity, sizeof(identity));
        terrainItem.color[0] = 0.35f;
        terrainItem.color[1] = 0.42f;
        terrainItem.color[2] = 0.30f;
        items.push_back(terrainItem);
    }
    for (Spawned& body : lab.bodies) {
        if (body.dynamic) {
            body.position = lab.game->physics().bodyPosition(body.physics);
            body.orientation = lab.game->physics().bodyOrientation(body.physics);
        }
        Aporia::RenderItem item;
        item.meshId = body.mesh.value();
        const Aporia::Mat4 model = Aporia::Mat4::translation(body.position) * Aporia::Mat4::fromQuaternion(body.orientation);
        std::memcpy(item.modelMatrix, &model, sizeof(model));
        item.color[0] = body.color[0];
        item.color[1] = body.color[1];
        item.color[2] = body.color[2];
        items.push_back(item);
    }
    return items;
}

// Measures wall-clock ms per frame including the GPU (render + waitIdle).
double measureFrameMs(Lab& lab, const Aporia::CameraState& camera,
                      const std::vector<Aporia::RenderItem>& items, int frames) {
    LARGE_INTEGER frequency{}, begin{}, end{};
    QueryPerformanceFrequency(&frequency);
    double total = 0.0;
    for (int frame = 0; frame < frames; ++frame) {
        QueryPerformanceCounter(&begin);
        (void)lab.game->graphics().render(camera, items);
        lab.game->graphics().waitIdle();
        QueryPerformanceCounter(&end);
        total += static_cast<double>(end.QuadPart - begin.QuadPart) /
                 static_cast<double>(frequency.QuadPart);
    }
    return frames > 0 ? total / frames * 1000.0 : 0.0;
}

// Prints a legibility-first table: baseline (0 bodies), then the same scene at
// growing body counts, with shadows on and off, for every preset. This is the
// "floor" that shows which knob costs what and where the frame budget breaks.
int runBenchmark() {
    const Aporia::GraphicsQuality presets[3] = {
        Aporia::GraphicsQuality::Low,
        Aporia::GraphicsQuality::Medium,
        Aporia::GraphicsQuality::High};
    const char* presetNames[3] = {"Low", "Medium", "High"};
    const std::size_t counts[4] = {0, 500, 1000, 2000};
    const int warmupFrames = 8;
    const int measureFrames = 30;
    const double budgetSeconds = 10.0;
    LARGE_INTEGER clockFreq{}, clockStart{}, clockNow{};
    QueryPerformanceFrequency(&clockFreq);
    QueryPerformanceCounter(&clockStart);
    const auto elapsed = [&]() {
        QueryPerformanceCounter(&clockNow);
        return static_cast<double>(clockNow.QuadPart - clockStart.QuadPart) /
               static_cast<double>(clockFreq.QuadPart);
    };
    bool stopped = false;

    std::printf("%-7s %-8s %-7s %-9s\n", "preset", "shadows", "bodies", "avg_ms");
    for (int preset = 0; preset < 3 && !stopped; ++preset) {
        for (int shadowPass = 0; shadowPass < 2 && !stopped; ++shadowPass) {
            const bool shadows = shadowPass == 0;
            Lab lab;
            if (!initLab(lab, {}, presets[preset], true, shadows, true)) {
                return 1;
            }
            const Aporia::CameraState camera = labCamera(0.0);
            for (std::size_t target : counts) {
                if (elapsed() > budgetSeconds) {
                    std::printf("(stopped at %.0fs budget)\n", budgetSeconds);
                    stopped = true;
                    break;
                }
                while (lab.bodies.size() < target) {
                    spawnBodies(lab, 64);
                }
                const std::vector<Aporia::RenderItem> items = buildItems(lab);
                const double ms = measureFrameMs(lab, camera, items, warmupFrames + measureFrames);
                std::printf("%-7s %-8s %-7zu %-9.2f\n", presetNames[preset],
                            shadows ? "on" : "off", lab.bodies.size(), ms);
                std::fflush(stdout);
            }
            lab.game->shutdownGraphics();
        }
    }

    // Runtime preset switch: proves setQuality recompiles the mesh pipeline so
    // High actually costs more than Low on the same live scene.
    {
        Lab lab;
        if (!initLab(lab, {}, Aporia::GraphicsQuality::Low, true, true, true)) {
            return 1;
        }
        while (lab.bodies.size() < 1000) {
            spawnBodies(lab, 64);
        }
        const Aporia::CameraState camera = labCamera(0.0);
        const std::vector<Aporia::RenderItem> items = buildItems(lab);
        const double lowMs = measureFrameMs(lab, camera, items, warmupFrames + measureFrames);
        (void)lab.game->graphics().setQuality(Aporia::GraphicsQuality::High);
        const double highMs = measureFrameMs(lab, camera, items, warmupFrames + measureFrames);
        std::printf("\nruntime Low->High (1000 bodies): %.2f ms -> %.2f ms\n", lowMs, highMs);
        std::printf("%s\n", highMs > lowMs * 1.02 ? "preset switch OK" : "WARNING: High did not cost more");
        lab.game->shutdownGraphics();
    }
    return 0;
}

// Headless physics-only scaling table: how many dynamic bodies a frame step
// can handle before the O(N^2) cliff (now softened by the broad phase).
int runPhysicsBench() {
    const std::size_t counts[5] = {500, 1000, 2000, 4000, 8000};
    const int steps = 30;
    const int settle = 10;
    const double dt = 1.0 / 60.0;
    const double budgetSeconds = 10.0;
    LARGE_INTEGER clockFreq{}, clockStart{}, clockNow{};
    QueryPerformanceFrequency(&clockFreq);
    QueryPerformanceCounter(&clockStart);
    const auto elapsed = [&]() {
        QueryPerformanceCounter(&clockNow);
        return static_cast<double>(clockNow.QuadPart - clockStart.QuadPart) /
               static_cast<double>(clockFreq.QuadPart);
    };
    std::printf("%-9s %-10s %-10s %-12s\n", "bodies", "ms_frame", "fps_cap", "collisions");
    std::fflush(stdout);
    for (std::size_t count : counts) {
        if (elapsed() > budgetSeconds) {
            std::printf("(stopped at %.0fs budget)\n", budgetSeconds);
            break;
        }
        Aporia::PhysicsWorld world(Aporia::Vec3{0.0, 0.0, -9.81});
        Rng rng;
        for (std::size_t index = 0; index < count; ++index) {
            Aporia::BoxBodyConfig box;
            box.position = Aporia::Vec3{
                rng.range(-18.0, 18.0), rng.range(-18.0, 18.0), rng.range(1.0, 40.0)};
            box.halfExtents = Aporia::Vec3{0.5, 0.5, 0.5};
            box.mass = 10.0;
            box.friction = 0.6;
            box.restitution = 0.0;
            world.createBox(box);
        }
        for (int frame = 0; frame < settle; ++frame) {
            world.step(dt);
        }
        LARGE_INTEGER frequency{}, begin{}, end{};
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&begin);
        int measured = 0;
        for (int frame = 0; frame < steps; ++frame) {
            if (elapsed() > budgetSeconds) {
                break;
            }
            world.step(dt);
            ++measured;
        }
        QueryPerformanceCounter(&end);
        const double totalSeconds = static_cast<double>(end.QuadPart - begin.QuadPart) /
                                    static_cast<double>(frequency.QuadPart);
        const double msPerFrame = measured > 0 ? totalSeconds / measured * 1000.0 : 0.0;
        std::printf("%-9zu %-10.2f %-10.1f %-12zu\n", count, msPerFrame,
                    msPerFrame > 0.0 ? 1000.0 / msPerFrame : 0.0,
                    world.lastStepCollisionCount());
        std::fflush(stdout);
    }
    return 0;
}

}

int main(int argc, char** argv) {
    bool headlessOnly = false;
    bool benchmarkOnly = false;
    bool physicsOnly = false;
    double durationSeconds = 20.0;
    for (int index = 1; index < argc; ++index) {
        if (std::strcmp(argv[index], "--headless") == 0) {
            headlessOnly = true;
        } else if (std::strcmp(argv[index], "--bench") == 0) {
            benchmarkOnly = true;
        } else if (std::strcmp(argv[index], "--physbench") == 0) {
            physicsOnly = true;
        } else {
            durationSeconds = std::max(1.0, std::atof(argv[index]));
        }
    }

    if (physicsOnly) {
        return runPhysicsBench();
    }
    if (benchmarkOnly) {
        return runBenchmark();
    }
    if (!headlessSelfCheck()) {
        return 1;
    }
    if (headlessOnly) {
        std::cout << "Aporia render test 10 headless self-check passed\n";
        return 0;
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
        return 1;
    }
    SDL_Window* window = SDL_CreateWindow("Aporia Render Test 10 - Graphics Load Lab", 1280, 720, 0);
    if (window == nullptr) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << '\n';
        SDL_Quit();
        return 1;
    }
    const HWND hwnd = static_cast<HWND>(SDL_GetPointerProperty(
        SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    Aporia::GraphicsContext context;
    context.nativeWindow = reinterpret_cast<std::uintptr_t>(hwnd);

    Aporia::GraphicsQuality quality = Aporia::GraphicsQuality::High;
    Lab lab;
    // Default: static floating bodies so this measures graphics (shadows/HDR/
    // AA/LOD), not the O(n^2) physics solver. Press P to switch new bodies to
    // dynamic and watch the physics cliff.
    if (!initLab(lab, context, quality, false, true, true)) {
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    bool running = true;
    bool spawning = true;
    Aporia::AntiAliasingMode aaMode = Aporia::AntiAliasingMode::SMAA;
    double spawnTimer = 0.0;
    double spawnInterval = 0.05;
    double elapsed = 0.0;
    double smoothedFps = 60.0;
    float renderMs = 0.0f;
    float loopFrameMs = 0.0f;
    float bestMs = 1.0e9f;
    float worstMs = 0.0f;
    std::uint64_t lastSpawnFrame = 0;
    struct Sample { std::size_t bodies; double loopMs; double renderMs; double spawnMs; double stepMs; double buildMs; double uiMs; };
    std::vector<Sample> samples;
    double sampleTimer = 0.0;

    LARGE_INTEGER frequency{}, previous{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&previous);

    while (running) {
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
        if (elapsed >= durationSeconds) {
            break;
        }

        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        const double frameDelta = static_cast<double>(now.QuadPart - previous.QuadPart) /
                                  static_cast<double>(frequency.QuadPart);
        previous = now;
        elapsed += frameDelta;
        LARGE_INTEGER tCpu0{}, tCpu1{}, tCpu2{}, tCpu3{}, tCpu4{};
        QueryPerformanceCounter(&tCpu0);

        lab.game->updateInput();
        Aporia::Input& input = lab.game->input();

        if (input.keyPressed(Aporia::KeyCode::Space)) {
            spawning = !spawning;
        }
        if (input.keyPressed(Aporia::KeyCode::N)) {
            lastSpawnFrame = static_cast<std::uint64_t>(lab.bodies.size());
            spawnBodies(lab, kBurst);
        }
        if (input.keyPressed(Aporia::KeyCode::Digit1)) {
            quality = Aporia::GraphicsQuality::Low;
            lab.game->graphics().setQuality(quality);
        }
        if (input.keyPressed(Aporia::KeyCode::Digit2)) {
            quality = Aporia::GraphicsQuality::Medium;
            lab.game->graphics().setQuality(quality);
        }
        if (input.keyPressed(Aporia::KeyCode::Digit3)) {
            quality = Aporia::GraphicsQuality::High;
            lab.game->graphics().setQuality(quality);
        }
        if (input.keyPressed(Aporia::KeyCode::A)) {
            aaMode = aaMode == Aporia::AntiAliasingMode::None ? Aporia::AntiAliasingMode::SMAA
                   : aaMode == Aporia::AntiAliasingMode::SMAA ? Aporia::AntiAliasingMode::TAA
                   : Aporia::AntiAliasingMode::None;
            lab.game->graphics().setAntiAliasing(aaMode);
        }
        if (input.keyPressed(Aporia::KeyCode::R)) {
            lab.bodies.clear();
            lab.dynamicCount = 0;
            lab.rng = Rng{};
            lastSpawnFrame = 0;
            bestMs = 1.0e9f;
            worstMs = 0.0f;
        }
        if (input.keyPressed(Aporia::KeyCode::P)) {
            lab.staticBodies = !lab.staticBodies; // applies to newly spawned bodies
        }

        if (spawning) {
            spawnTimer += frameDelta;
            while (spawnTimer >= spawnInterval && lab.bodies.size() < kMaxBodies) {
                spawnTimer -= spawnInterval;
                spawnBodies(lab, 4);
            }
        }

        (void)lastSpawnFrame;
        QueryPerformanceCounter(&tCpu1);
        if (lab.dynamicCount > 0) {
            lab.game->step(frameDelta);
        }
        QueryPerformanceCounter(&tCpu2);

        const std::vector<Aporia::RenderItem> items = buildItems(lab);
        QueryPerformanceCounter(&tCpu3);

        const Aporia::CameraState camera = labCamera(elapsed);
        const Aporia::GraphicsStats stats = lab.game->graphics().stats();

        Aporia::UiDrawList ui;
        ui.font = lab.font;
        const char* presetName = quality == Aporia::GraphicsQuality::Low ? "Low"
                               : quality == Aporia::GraphicsQuality::Medium ? "Medium" : "High";
        const char* aaName = aaMode == Aporia::AntiAliasingMode::None ? "None"
                           : aaMode == Aporia::AntiAliasingMode::SMAA ? "SMAA" : "TAA";
        char line[256];
        std::snprintf(line, sizeof(line), "PRESET %s   BODIES %zu   FPS %.1f   LOOP %.2f ms   RENDER %.2f ms   min %.2f  max %.2f",
                      presetName, lab.bodies.size(), smoothedFps, loopFrameMs, renderMs, bestMs, worstMs);
        ui.textRuns.push_back({line, 14.0f, 12.0f, 18.0f, {1.0f, 0.93f, 0.4f, 1.0f}});
        std::snprintf(line, sizeof(line), "shadow %u   LOD tri %llu/%llu   GPU heap %llu MB   coll %zu   AA %s",
                      stats.shadowMapSize,
                      static_cast<unsigned long long>(stats.lodDrawTriangles),
                      static_cast<unsigned long long>(stats.lodFullTriangles),
                      static_cast<unsigned long long>(stats.deviceLocalMemory / (1024ull * 1024ull)),
                      lab.game->physics().lastStepCollisionCount(),
                      aaName);
        ui.textRuns.push_back({line, 14.0f, 36.0f, 16.0f, {0.85f, 0.9f, 0.98f, 1.0f}});
        std::snprintf(line, sizeof(line), "SPACE spawn:%s   N +%u   R clear   P physics:%s   A AA   1/2/3 preset   ESC quit",
                      spawning ? "ON" : "OFF", kBurst, lab.staticBodies ? "off" : "on");
        ui.textRuns.push_back({line, 14.0f, 58.0f, 15.0f, {0.7f, 0.76f, 0.86f, 1.0f}});
        lab.game->graphics().setUiDrawList(std::move(ui));
        QueryPerformanceCounter(&tCpu4);

        LARGE_INTEGER frameStart{};
        QueryPerformanceCounter(&frameStart);
        const bool rendered = lab.game->graphics().render(camera, items);
        lab.game->graphics().waitIdle();
        LARGE_INTEGER frameEnd{};
        QueryPerformanceCounter(&frameEnd);
        if (!rendered) {
            const std::string& error = lab.game->graphics().lastError();
            if (error.find("out of date") == std::string::npos &&
                error.find("recreated") == std::string::npos) {
                std::cerr << "render failed: " << error << '\n';
                break;
            }
        }
        const double measuredMs = static_cast<double>(frameEnd.QuadPart - frameStart.QuadPart) /
                                  static_cast<double>(frequency.QuadPart) * 1000.0;
        renderMs = static_cast<float>(measuredMs);
        loopFrameMs = static_cast<float>(frameDelta * 1000.0);
        bestMs = std::min(bestMs, loopFrameMs);
        worstMs = std::max(worstMs, loopFrameMs);
        if (frameDelta > 0.0) {
            smoothedFps = smoothedFps * 0.9 + (1.0 / frameDelta) * 0.1;
        }
        sampleTimer += frameDelta;
        if (sampleTimer >= 1.0) {
            sampleTimer -= 1.0;
            const double perTick = 1000.0 / static_cast<double>(frequency.QuadPart);
            samples.push_back(Sample{
                lab.bodies.size(),
                static_cast<double>(loopFrameMs),
                measuredMs,
                static_cast<double>(tCpu1.QuadPart - tCpu0.QuadPart) * perTick,
                static_cast<double>(tCpu2.QuadPart - tCpu1.QuadPart) * perTick,
                static_cast<double>(tCpu3.QuadPart - tCpu2.QuadPart) * perTick,
                static_cast<double>(tCpu4.QuadPart - tCpu3.QuadPart) * perTick});
        }
    }

    lab.game->shutdownGraphics();
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::printf("%-8s %-9s %-10s %-8s %-8s %-8s %-8s\n", "bodies", "loop_ms", "render_ms", "spawn_ms", "step_ms", "build_ms", "ui_ms");
    for (const Sample& sample : samples) {
        std::printf("%-8zu %-9.2f %-10.2f %-8.2f %-8.2f %-8.2f %-8.2f\n",
                    sample.bodies, sample.loopMs, sample.renderMs,
                    sample.spawnMs, sample.stepMs, sample.buildMs, sample.uiMs);
    }
    std::cout << "Aporia render test 10 finished, peak bodies " << lab.bodies.size() << '\n';
    return 0;
}
