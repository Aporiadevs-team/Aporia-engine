﻿﻿﻿// Aporia render test 12 — night alley lighting lab (SDL3 window).
//
// A dim night alley: a ground strip, two walls and a row of lamp posts. Every
// lamp carries a warm point light plus an emissive head, so the fixtures glow
// and pool light on the ground. Colour, intensity, soft falloff radius and
// softness are tweakable live; the quality preset caps how many lights reach
// the shader (Low 0, Medium 8, High 16).
//
// Headless self-check (--headless) renders the scene with and without a light
// and asserts the pixels changed; the default path is the interactive GUI.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <SDL3/SDL.h>

#include "Aporia/Game.hpp"
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
#include <vector>

namespace {

constexpr int kWindowWidth = 1280;
constexpr int kWindowHeight = 720;
constexpr int kMaxLights = 16;

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

double nowMs() {
    LARGE_INTEGER counter{};
    LARGE_INTEGER frequency{};
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    return static_cast<double>(counter.QuadPart) * 1000.0 / static_cast<double>(frequency.QuadPart);
}
double nowSeconds() { return nowMs() / 1000.0; }

Aporia::Mat4 makeModel(const Aporia::Vec3& position, const Aporia::Vec3& scale) {
    Aporia::Mat4 model = Aporia::Mat4::translation(position);
    model.m[0][0] = scale.x;
    model.m[1][1] = scale.y;
    model.m[2][2] = scale.z;
    return model;
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
        return sorted[static_cast<std::size_t>(static_cast<double>(sorted.size() - 1) * p)];
    }
};

// Headless proof that a dynamic light actually changes the shaded pixels.
bool headlessLightsCheck() {
    Aporia::Graphics graphics;
    Aporia::GraphicsConfig cfg;
    cfg.headless = true;
    cfg.headlessWidth = 256;
    cfg.headlessHeight = 256;
    cfg.quality = Aporia::GraphicsQuality::High;
    if (!graphics.initialize({}, cfg)) {
        std::cerr << "headless init failed: " << graphics.lastError() << '\n';
        return false;
    }
    // A big floor filling the view, lit by one overhead light so the radial
    // falloff is measurable: a small and a large radius must differ.
    const Aporia::MeshHandle floor = graphics.createMesh(Aporia::apr::makeBoxMesh(0.5f, 0.5f, 0.5f));
    Aporia::RenderItem item;
    item.meshId = floor.value();
    const Aporia::Mat4 identity = Aporia::Mat4::identity();
    std::memcpy(item.modelMatrix, &identity, sizeof(identity));
    item.modelMatrix[0] = 12.0f;  // scale the 1x1x1 box into a big slab
    item.modelMatrix[5] = 12.0f;
    item.modelMatrix[10] = 0.2f;

    Aporia::CameraState camera;
    const Aporia::Vec3 eye{0.0, -5.0, 5.0};
    const Aporia::Mat4 view = Aporia::Mat4::lookAt(eye, {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0});
    const Aporia::Mat4 projection = Aporia::Mat4::perspectiveVulkan(0.9, 1.0, 0.1, 100.0);
    std::memcpy(camera.viewMatrix, &view, sizeof(view));
    std::memcpy(camera.projectionMatrix, &projection, sizeof(projection));
    camera.position[0] = eye.x;
    camera.position[1] = eye.y;
    camera.position[2] = eye.z;

    const auto renderAndRead = [&](const std::vector<Aporia::LightDesc>& lights, std::vector<std::uint8_t>& out) {
        graphics.setLights(lights);
        for (int frame = 0; frame < 4; ++frame) {
            (void)graphics.render(camera, {item});
        }
        (void)graphics.readbackPixels(out);
    };
    const auto differingBytes = [](const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) {
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

    Aporia::LightDesc light;
    light.type = Aporia::LightType::Point;
    light.position[2] = 2.5f;
    light.color[0] = 1.0f;
    light.color[1] = 0.9f;
    light.color[2] = 0.8f;
    light.intensity = 5.0f;

    std::vector<std::uint8_t> dark;
    std::vector<std::uint8_t> tight;
    std::vector<std::uint8_t> wide;
    renderAndRead({}, dark);
    light.radius = 5.0f;
    renderAndRead({light}, tight);
    light.radius = 50.0f;
    renderAndRead({light}, wide);

    const Aporia::GraphicsStats stats = graphics.stats();
    graphics.shutdown();

    const std::size_t darkVsLit = differingBytes(dark, tight);
    const std::size_t radiusMatters = differingBytes(tight, wide);
    std::cout << "Aporia lighting self-check: dark-vs-lit=" << darkVsLit
              << ", radius-sensitivity=" << radiusMatters
              << ", validation " << stats.validationMessageCount << '\n';
    return darkVsLit > 0 && radiusMatters > 0 && stats.validationMessageCount == 0;
}

} // namespace

int main(int argc, char** argv) {
    bool autoMode = false;
    bool headlessOnly = false;
    bool syncAfterRender = false;
    double durationSeconds = 0.0;
    Aporia::GraphicsQuality quality = Aporia::GraphicsQuality::High;
    for (int index = 1; index < argc; ++index) {
        if (std::strcmp(argv[index], "--auto") == 0) {
            autoMode = true;
        } else if (std::strcmp(argv[index], "--headless") == 0) {
            headlessOnly = true;
        } else if (std::strcmp(argv[index], "--sync") == 0) {
            syncAfterRender = true;
        } else if (std::strcmp(argv[index], "--low") == 0) {
            quality = Aporia::GraphicsQuality::Low;
        } else if (std::strcmp(argv[index], "--medium") == 0) {
            quality = Aporia::GraphicsQuality::Medium;
        } else if (std::strcmp(argv[index], "--high") == 0) {
            quality = Aporia::GraphicsQuality::High;
        } else if (std::strcmp(argv[index], "--duration") == 0 && index + 1 < argc) {
            durationSeconds = std::atof(argv[++index]);
        }
    }

    if (headlessOnly) {
        if (!headlessLightsCheck()) {
            std::cerr << "Aporia render test 12 headless lighting check failed\n";
            return 1;
        }
        std::cout << "Aporia render test 12 headless self-check passed\n";
        return 0;
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
        return 1;
    }
    SDL_Window* window = SDL_CreateWindow("Aporia Render Test 12 - Night Alley",
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
    if (!game.initGraphics(context, config)) {
        std::cerr << "initGraphics failed: " << game.graphics().lastError() << '\n';
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    game.graphics().setVsync(false);

    const Aporia::MeshHandle boxMesh = game.graphics().createMesh(Aporia::apr::makeBoxMesh(0.5f, 0.5f, 0.5f));
    const Aporia::FontHandle font = game.graphics().createFontFromTtf(firstExistingFont(), 44.0f);

    // Night environment: dim sun + soft ambient so lamps can be measured against
    // a visible baseline.  geometry is dim but not black; lamps provide the warm
    // pools that make the difference between “dark” and “lit”.
    Aporia::EnvironmentState environment;
    environment.sunDirection[0] = 0.2f;
    environment.sunDirection[1] = -0.3f;
    environment.sunDirection[2] = 0.93f;
    environment.sunIntensity = 0.15f;          // dim blue-grey fill from the sky
    environment.ambient = 0.12f;               // enough to see albedo without lamps

    // Alley: ground + two long walls.
    struct Block {
        Aporia::Mat4 model;
        float color[3];
        float emissive = 0.0f;
    };
    std::vector<Block> blocks;
    const auto addBlock = [&](const Aporia::Vec3& position, const Aporia::Vec3& scale,
                              float r, float g, float b, float emiss = 0.0f) {
        Block block;
        block.model = makeModel(position, scale);
        block.color[0] = r;
        block.color[1] = g;
        block.color[2] = b;
        block.emissive = emiss;
        blocks.push_back(block);
    };
    addBlock({0.0, 0.0, -0.1}, {5.0, 26.0, 0.2}, 0.05f, 0.05f, 0.06f);    // ground
    addBlock({-5.0, 0.0, 4.0}, {1.6, 26.0, 6.0}, 0.18f, 0.17f, 0.16f);    // left building
    addBlock({5.0, 0.0, 4.0}, {1.6, 26.0, 6.0}, 0.18f, 0.17f, 0.16f);     // right building
    addBlock({0.0, -15.0, 4.0}, {5.0, 1.6, 6.0}, 0.16f, 0.15f, 0.15f);    // far facade
    addBlock({0.0, 15.0, 4.0}, {5.0, 1.6, 6.0}, 0.16f, 0.15f, 0.15f);     // near facade

    // Lamp posts down the middle; each has a post, a glowing head and a light.
    struct Lamp {
        Aporia::Vec3 position;
    };
    std::vector<Lamp> lamps;
    for (int index = 0; index < 5; ++index) {
        const double y = -12.0 + static_cast<double>(index) * 6.0;
        addBlock({0.0, y, 0.25}, {0.5, 0.5, 0.5}, 0.12f, 0.12f, 0.13f);     // base
        addBlock({0.0, y, 2.1}, {0.22, 0.22, 2.1}, 0.09f, 0.09f, 0.10f);    // post
        addBlock({0.0, y, 4.35}, {0.85, 0.85, 0.12}, 0.06f, 0.06f, 0.07f);  // cap
        addBlock({0.0, y, 3.85}, {0.75, 0.75, 0.62}, 1.0f, 0.9f, 0.62f,
                 2.5f);                                                     // glowing lantern
        lamps.push_back({Aporia::Vec3{0.0, y, 3.8}});
    }
    int litLamps = static_cast<int>(lamps.size());
    float lampRadius = 7.5f;
    float lampSoftness = 1.2f;
    float lampIntensity = 13.0f;

    bool running = true;
    bool multithreaded = true;
    double elapsed = 0.0;
    double lastReport = 0.0;
    double smoothedFps = 60.0;
    double previousLoop = nowMs();
    Series renderMs, loopMs;
    std::array<std::string, 4> hud;

    while (running) {
        const double frameStart = nowMs();
        const double dt = (frameStart - previousLoop) / 1000.0;
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

        game.updateInput();
        Aporia::Input& input = game.input();
        if (input.keyPressed(Aporia::KeyCode::Digit1)) { quality = Aporia::GraphicsQuality::Low; game.graphics().setQuality(quality); }
        if (input.keyPressed(Aporia::KeyCode::Digit2)) { quality = Aporia::GraphicsQuality::Medium; game.graphics().setQuality(quality); }
        if (input.keyPressed(Aporia::KeyCode::Digit3)) { quality = Aporia::GraphicsQuality::High; game.graphics().setQuality(quality); }
        if (input.keyPressed(Aporia::KeyCode::Digit4)) { litLamps = std::max(0, litLamps - 1); }
        if (input.keyPressed(Aporia::KeyCode::Digit5)) { litLamps = std::min(static_cast<int>(lamps.size()), litLamps + 1); }
        if (input.key(Aporia::KeyCode::Q)) { lampRadius = std::max(1.0f, lampRadius - static_cast<float>(dt) * 8.0f); }
        if (input.key(Aporia::KeyCode::E)) { lampRadius = std::min(30.0f, lampRadius + static_cast<float>(dt) * 8.0f); }
        if (input.key(Aporia::KeyCode::Down)) { lampSoftness = std::max(0.2f, lampSoftness - static_cast<float>(dt) * 2.0f); }
        if (input.key(Aporia::KeyCode::Up)) { lampSoftness = std::min(6.0f, lampSoftness + static_cast<float>(dt) * 2.0f); }
        if (input.keyPressed(Aporia::KeyCode::R)) { lampIntensity = std::max(0.1f, lampIntensity - 0.4f); }
        if (input.keyPressed(Aporia::KeyCode::T)) { lampIntensity += 0.4f; }
        if (input.keyPressed(Aporia::KeyCode::M)) {
#if defined(APORIA_EXPERIMENTAL)
            multithreaded = !multithreaded;
            game.graphics().setMultithreadedRender(multithreaded);
#endif
        }

        std::vector<Aporia::LightDesc> lights;
        lights.reserve(static_cast<std::size_t>(litLamps));
        for (int index = 0; index < litLamps; ++index) {
            Aporia::LightDesc light;
            light.type = Aporia::LightType::Point; // lights its own head + a tight pool below
            light.position[0] = lamps[index].position.x;
            light.position[1] = lamps[index].position.y;
            light.position[2] = lamps[index].position.z;
            light.color[0] = 1.0f;
            light.color[1] = 0.78f;
            light.color[2] = 0.45f;
            light.intensity = lampIntensity;
            light.radius = lampRadius;
            light.softness = lampSoftness;
            lights.push_back(light);
        }
        game.graphics().setLights(lights);

        std::vector<Aporia::RenderItem> items;
        items.reserve(blocks.size());
        for (const Block& block : blocks) {
            Aporia::RenderItem item;
            item.meshId = boxMesh.value();
            std::memcpy(item.modelMatrix, &block.model, sizeof(block.model));
            item.color[0] = block.color[0];
            item.color[1] = block.color[1];
            item.color[2] = block.color[2];
            item.emissive = block.emissive;
            items.push_back(item);
        }

        const double camY = autoMode ? -14.0 + (elapsed * 2.0) : -14.0;
        const Aporia::Vec3 eye{0.0, camY, 2.6};
        const Aporia::Vec3 target{0.0, camY + 12.0, 3.4};
        Aporia::CameraState camera;
        const Aporia::Mat4 view = Aporia::Mat4::lookAt(eye, target, {0.0, 0.0, 1.0});
        const Aporia::Mat4 projection = Aporia::Mat4::perspectiveVulkan(
            0.85, static_cast<double>(kWindowWidth) / static_cast<double>(kWindowHeight), 0.1, 200.0);
        std::memcpy(camera.viewMatrix, &view, sizeof(view));
        std::memcpy(camera.projectionMatrix, &projection, sizeof(projection));
        camera.position[0] = eye.x;
        camera.position[1] = eye.y;
        camera.position[2] = eye.z;

        Aporia::UiDrawList ui;
        ui.font = font;
        const char* presetName = quality == Aporia::GraphicsQuality::Low ? "Low"
                               : quality == Aporia::GraphicsQuality::Medium ? "Medium" : "High";
        char line[160];
        std::snprintf(line, sizeof(line), "NIGHT ALLEY   lamps %d/%zu   preset %s   MT %s   FPS %d",
                      litLamps, lamps.size(), presetName, multithreaded ? "ON" : "OFF",
                      static_cast<int>(smoothedFps));
        hud[0] = line;
        std::snprintf(line, sizeof(line), "intensity %.1f   radius %.1f   softness %.1f   render %.2f ms",
                      lampIntensity, lampRadius, lampSoftness, renderMs.avg());
        hud[1] = line;
        hud[2] = autoMode ? "camera auto" : "4/5 lamps   R/T intensity   Q/E radius   Up/Down softness";
        hud[3] = "1/2/3 preset   M MT   ESC quit";
        for (std::size_t index = 0; index < hud.size(); ++index) {
            Aporia::UiTextRun run;
            run.text = hud[index];
            run.x = 14.0f;
            run.y = 12.0f + static_cast<float>(index) * 22.0f;
            run.size = 16.0f;
            run.color = {1.0f, 0.86f, 0.55f, 1.0f};
            ui.textRuns.push_back(run);
        }
        game.graphics().setUiDrawList(std::move(ui));

        const double renderStart = nowMs();
        const bool rendered = game.graphics().render(camera, environment, items);
        if (syncAfterRender) {
            game.graphics().waitIdle();
        }
        const double renderSection = nowMs() - renderStart;
        if (!rendered) {
            const std::string& error = game.graphics().lastError();
            if (error.find("out of date") == std::string::npos &&
                error.find("recreated") == std::string::npos) {
                std::cerr << "render failed: " << error << '\n';
                break;
            }
        }

        const double frameMs = nowMs() - frameStart;
        elapsed += dt;
        if (frameMs > 0.0) {
            smoothedFps = smoothedFps * 0.9 + (1000.0 / frameMs) * 0.1;
        }
        renderMs.add(renderSection);
        loopMs.add(frameMs);

        if (elapsed - lastReport >= 1.0) {
            lastReport = elapsed;
            std::printf("[t+%.1fs] lamps=%d preset=%s fps %.1f  loop %.2f  render %.2f ms\n",
                        elapsed, litLamps, presetName, smoothedFps, loopMs.avg(), renderMs.avg());
        }
        if (durationSeconds > 0.0 && elapsed >= durationSeconds) {
            running = false;
        }
    }

    std::printf("Aporia render test 12 summary (frames=%zu, lamps=%d)\n", loopMs.samples.size(), litLamps);
    std::printf("  render avg %.3f ms  p95 %.3f\n", renderMs.avg(), renderMs.percentile(0.95));
    std::printf("  loop   avg %.3f ms  p95 %.3f  fps %.1f\n", loopMs.avg(), loopMs.percentile(0.95),
                loopMs.avg() > 0.0 ? 1000.0 / loopMs.avg() : 0.0);

    game.graphics().clearLights();
    game.graphics().destroyMesh(boxMesh);
    game.shutdownGraphics();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
