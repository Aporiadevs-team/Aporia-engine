// AporiaTextGuiTest — windowed text throughput test.
// SDL3 owns the window; Aporia bakes a TTF and renders thousands of random
// strings per frame for N seconds, showing live strings/sec and fps on screen.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <SDL3/SDL.h>

#include "Aporia/Game.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

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

}

int main(int argc, char** argv) {
    double durationSeconds = 10.0;
    if (argc > 1) {
        durationSeconds = std::max(1.0, std::atof(argv[1]));
    }

    const std::string fontPath = firstExistingFont();
    if (fontPath.empty()) {
        std::cout << "AporiaTextGuiTest skipped: no system TTF found\n";
        return 0;
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("Aporia Text GUI Test", 1280, 720, 0);
    if (window == nullptr) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << '\n';
        SDL_Quit();
        return 1;
    }

    const HWND hwnd = static_cast<HWND>(SDL_GetPointerProperty(
        SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    if (hwnd == nullptr) {
        std::cerr << "SDL window has no Win32 handle\n";
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    Aporia::Game game;
    Aporia::GraphicsContext context;
    context.nativeWindow = reinterpret_cast<std::uintptr_t>(hwnd);
    Aporia::GraphicsConfig config;
    config.headless = false;
    config.enableValidation = false;
    config.quality = Aporia::GraphicsQuality::High;
    if (!game.initGraphics(context, config)) {
        std::cerr << "initGraphics failed: " << game.graphics().lastError() << '\n';
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Measure raw throughput, not the refresh limit.
    game.graphics().setVsync(false);
    std::cout << "vsync=" << (game.graphics().vsync() ? "on" : "off") << '\n';

    const Aporia::FontHandle font = game.graphics().createFontFromTtf(fontPath, 26.0f);
    if (!font.valid()) {
        std::cerr << "TTF bake failed: " << game.graphics().lastError() << '\n';
        game.shutdownGraphics();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    Aporia::CameraState camera;
    const Aporia::Mat4 identity = Aporia::Mat4::identity();
    std::memcpy(camera.viewMatrix, &identity, sizeof(identity));
    std::memcpy(camera.projectionMatrix, &identity, sizeof(identity));

    std::mt19937 rng(2026u);
    std::uniform_int_distribution<int> lengthDist(6, 16);
    std::uniform_int_distribution<int> letterDist(0, 26);
    const auto randomWord = [&]() {
        const int length = lengthDist(rng);
        std::string word;
        word.reserve(static_cast<std::size_t>(length));
        for (int index = 0; index < length; ++index) {
            const int pick = letterDist(rng);
            word.push_back(pick < 26 ? static_cast<char>('A' + pick) : ' ');
        }
        return word;
    };

    // Pre-generate all words once into one buffer; the frame loop only points
    // string_views at them (zero allocation / RNG in the hot path).
    std::string wordBuffer;
    wordBuffer.reserve(100000 * 17);
    std::vector<std::pair<std::uint32_t, std::uint32_t>> spans;
    spans.reserve(100000);
    for (int index = 0; index < 100000; ++index) {
        const std::string word = randomWord();
        const std::uint32_t offset = static_cast<std::uint32_t>(wordBuffer.size());
        wordBuffer += word;
        spans.emplace_back(offset, static_cast<std::uint32_t>(word.size()));
    }
    std::vector<std::string_view> words;
    words.reserve(spans.size());
    for (const auto& [offset, length] : spans) {
        words.emplace_back(wordBuffer.data() + offset, length);
    }

    const std::uint64_t startTicks = SDL_GetTicks();
    const double startSeconds = static_cast<double>(startTicks) / 1000.0;
    std::uint64_t totalStrings = 0;
    std::uint64_t totalGlyphs = 0;
    std::uint64_t frames = 0;
    double lastReportSeconds = startSeconds;
    std::uint64_t framesAtLastReport = 0;
    double liveStringsPerSecond = 0.0;

    bool running = true;
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

        const double nowSec = static_cast<double>(SDL_GetTicks()) / 1000.0;
        if (nowSec - startSeconds >= durationSeconds) {
            break;
        }

        Aporia::UiDrawList ui;
        ui.font = font;
        ui.textRuns.reserve(180);
        std::uint32_t glyphBudget = 1800;
        float y = 40.0f;
        std::size_t wordIndex = (frames * 37u) % words.size();
        while (glyphBudget > 0) {
            const std::string_view word = words[wordIndex];
            const std::size_t length = word.size();
            Aporia::UiTextRun run;
            run.text = word;
            run.x = 8.0f;
            run.y = y;
            run.size = 20.0f;
            run.color = {0.85f, 0.9f, 1.0f, 1.0f};
            totalStrings += 1;
            totalGlyphs += length;
            glyphBudget = length < glyphBudget ? glyphBudget - static_cast<std::uint32_t>(length) : 0;
            ui.textRuns.push_back(run);
            y += 22.0f;
            wordIndex = (wordIndex + 1) % words.size();
        }

        // Live counters as a HUD line on top.
        const double elapsed = nowSec - startSeconds;
        if (nowSec - lastReportSeconds >= 0.5) {
            const std::uint64_t framesSince = frames - framesAtLastReport;
            liveStringsPerSecond = static_cast<double>(totalStrings) /
                std::max(0.001, elapsed);
            (void)framesSince;
            lastReportSeconds = nowSec;
            framesAtLastReport = frames;
        }
        char hud[128];
        std::snprintf(hud, sizeof(hud),
                      "strings/sec: %lld   glyphs: %lld   elapsed: %.1fs",
                      static_cast<long long>(liveStringsPerSecond),
                      static_cast<long long>(totalGlyphs), elapsed);
        Aporia::UiTextRun hudRun;
        hudRun.text = hud;
        hudRun.x = 8.0f;
        hudRun.y = 8.0f;
        hudRun.size = 22.0f;
        hudRun.color = {1.0f, 0.85f, 0.3f, 1.0f};
        ui.textRuns.insert(ui.textRuns.begin(), std::move(hudRun));

        game.graphics().setUiDrawList(std::move(ui));
        if (!game.graphics().render(camera, {}, {})) {
            std::cerr << "render failed: " << game.graphics().lastError() << '\n';
            break;
        }
        ++frames;
    }

    const double elapsedSeconds = static_cast<double>(SDL_GetTicks()) / 1000.0 - startSeconds;
    const double stringsPerSecond = elapsedSeconds > 0.0 ? static_cast<double>(totalStrings) / elapsedSeconds : 0.0;
    const double glyphsPerSecond = elapsedSeconds > 0.0 ? static_cast<double>(totalGlyphs) / elapsedSeconds : 0.0;
    std::cout << "AporiaTextGuiTest: elapsed=" << elapsedSeconds << "s"
              << " frames=" << frames
              << " strings=" << totalStrings
              << " glyphs=" << totalGlyphs << '\n'
              << "  strings/sec=" << static_cast<long long>(stringsPerSecond)
              << " glyphs/sec=" << static_cast<long long>(glyphsPerSecond)
              << " avg frame ms=" << (frames > 0 ? elapsedSeconds * 1000.0 / static_cast<double>(frames) : 0.0)
              << '\n';

    game.shutdownGraphics();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
