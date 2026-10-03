// AporiaTextTest — maximum text throughput.
// Measures the render path only: strings are pre-generated once into a single
// character buffer (xorshift RNG), and the frame loop just points views at them
// — no std::string, no RNG, no allocation per frame in the harness.
#include "Aporia/Game.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (condition) {
        return;
    }
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

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

struct WordPool {
    std::string buffer;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> spans; // offset, length
    std::vector<std::string_view> words;

    explicit WordPool(std::size_t count, std::uint32_t seed) {
        std::uint32_t state = seed;
        const auto next = [&state]() noexcept {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return state;
        };
        buffer.reserve(count * 12);
        spans.reserve(count);
        words.reserve(count);
        for (std::size_t index = 0; index < count; ++index) {
            const std::uint32_t length = 6 + (next() % 11);
            const std::uint32_t offset = static_cast<std::uint32_t>(buffer.size());
            for (std::uint32_t c = 0; c < length; ++c) {
                const std::uint32_t pick = next() % 27;
                buffer.push_back(pick < 26 ? static_cast<char>('A' + pick) : ' ');
            }
            spans.emplace_back(offset, length);
        }
        for (const auto& [offset, length] : spans) {
            words.emplace_back(buffer.data() + offset, length);
        }
    }
};

}

int main(int argc, char** argv) {
    int frames = 6000;
    if (argc > 1) {
        frames = std::max(1, std::atoi(argv[1]));
    }

    const std::string fontPath = firstExistingFont();
    if (fontPath.empty()) {
        std::cout << "AporiaTextTest skipped: no system TTF found\n";
        return 0;
    }

    Aporia::Game game;
    Aporia::GraphicsConfig config;
    config.headless = true;
    config.enableValidation = false;
    config.quality = Aporia::GraphicsQuality::High;
    if (!game.initGraphics({}, config)) {
        std::cerr << "initGraphics failed: " << game.graphics().lastError() << '\n';
        return 1;
    }

    const Aporia::FontHandle font = game.graphics().createFontFromTtf(fontPath, 28.0f);
    if (!font.valid()) {
        std::cerr << "FAIL: TTF bake failed: " << game.graphics().lastError() << '\n';
        game.shutdownGraphics();
        return 1;
    }

    const WordPool pool(100000, 2026u);

    Aporia::CameraState camera;
    const Aporia::Mat4 identity = Aporia::Mat4::identity();
    std::memcpy(camera.viewMatrix, &identity, sizeof(identity));
    std::memcpy(camera.projectionMatrix, &identity, sizeof(identity));

    const std::uint64_t memoryBefore = game.graphics().stats().deviceLocalMemory;

    LARGE_INTEGER frequency{};
    LARGE_INTEGER startCounter{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&startCounter);

    std::uint64_t totalGlyphs = 0;
    std::uint64_t totalStrings = 0;
    int renderedFrames = 0;

    for (int frame = 0; frame < frames; ++frame) {
        Aporia::UiDrawList ui;
        ui.font = font;
        ui.textRuns.reserve(160);

        std::uint32_t glyphBudget = 1900;
        std::size_t wordIndex = (static_cast<std::size_t>(frame) * 37u) % pool.words.size();
        float y = 2.0f;
        while (glyphBudget > 0) {
            const std::string_view word = pool.words[wordIndex];
            Aporia::UiTextRun run;
            run.text = word;
            run.x = 2.0f;
            run.y = y;
            run.size = 18.0f;
            run.color = {0.9f, 0.92f, 1.0f, 1.0f};
            ui.textRuns.push_back(run);
            totalStrings += 1;
            totalGlyphs += word.size();
            glyphBudget = word.size() < glyphBudget ? glyphBudget - static_cast<std::uint32_t>(word.size()) : 0;
            y += 20.0f;
            wordIndex = (wordIndex + 1) % pool.words.size();
        }

        game.graphics().setUiDrawList(std::move(ui));
        if (!game.graphics().render(camera, {}, {})) {
            std::cerr << "FAIL: render frame " << frame << " failed: " << game.graphics().lastError() << '\n';
            game.shutdownGraphics();
            return 1;
        }
        ++renderedFrames;
    }

    LARGE_INTEGER endCounter{};
    QueryPerformanceCounter(&endCounter);
    const double wallSeconds = static_cast<double>(endCounter.QuadPart - startCounter.QuadPart) /
                               static_cast<double>(frequency.QuadPart);

    // Static-text phase: identical draw list every frame. The persistent batch
    // keeps the laid-out glyphs and skips the UBO upload, so this is the pure
    // draw-call cost (menus/HUD) versus the dynamic above.
    Aporia::UiDrawList staticUi;
    staticUi.font = font;
    {
        std::uint32_t glyphBudget = 1900;
        float y = 2.0f;
        std::size_t wordIndex = 0;
        while (glyphBudget > 0) {
            const std::string_view word = pool.words[wordIndex];
            Aporia::UiTextRun run;
            run.text = word;
            run.x = 2.0f;
            run.y = y;
            run.size = 18.0f;
            run.color = {0.9f, 0.92f, 1.0f, 1.0f};
            staticUi.textRuns.push_back(run);
            glyphBudget = word.size() < glyphBudget ? glyphBudget - static_cast<std::uint32_t>(word.size()) : 0;
            y += 20.0f;
            wordIndex = (wordIndex + 1) % pool.words.size();
        }
    }
    game.graphics().setUiDrawList(staticUi);

    LARGE_INTEGER staticStart{};
    QueryPerformanceCounter(&staticStart);
    for (int frame = 0; frame < frames; ++frame) {
        game.graphics().render(camera, {}, {});
    }
    LARGE_INTEGER staticEnd{};
    QueryPerformanceCounter(&staticEnd);
    const double staticSeconds = static_cast<double>(staticEnd.QuadPart - staticStart.QuadPart) /
                                  static_cast<double>(frequency.QuadPart);
    const double staticStringsPerSecond = staticSeconds > 0.0
        ? static_cast<double>(staticUi.textRuns.size()) * frames / staticSeconds : 0.0;
    const double staticFrameMs = staticSeconds * 1000.0 / static_cast<double>(std::max(1, frames));

    const std::uint64_t memoryAfter = game.graphics().stats().deviceLocalMemory;
    const Aporia::GraphicsStats stats = game.graphics().stats();

    const double stringsPerSecond = wallSeconds > 0.0 ? static_cast<double>(totalStrings) / wallSeconds : 0.0;
    const double glyphsPerSecond = wallSeconds > 0.0 ? static_cast<double>(totalGlyphs) / wallSeconds : 0.0;
    const double avgFrameMs = wallSeconds * 1000.0 / static_cast<double>(std::max(1, renderedFrames));

    std::cout << "AporiaTextTest (dynamic): frames=" << renderedFrames
              << " strings=" << totalStrings
              << " glyphs=" << totalGlyphs << '\n'
              << "  strings/sec=" << static_cast<long long>(stringsPerSecond)
              << " glyphs/sec=" << static_cast<long long>(glyphsPerSecond)
              << " avg frame ms=" << avgFrameMs << '\n'
              << "AporiaTextTest (static batch): strings/frame=" << staticUi.textRuns.size()
              << " strings/sec=" << static_cast<long long>(staticStringsPerSecond)
              << " avg frame ms=" << staticFrameMs << '\n'
              << "  device-local memory " << memoryBefore << " -> " << memoryAfter
              << " (frameCount=" << stats.frameCount << ")\n";

    check(renderedFrames == frames, "every requested frame must render");
    check(totalStrings > 0 && totalGlyphs > 0, "text load must be generated");
    check(stats.frameCount == static_cast<std::uint64_t>(frames) * 2, "stats must count every text frame");
    check(avgFrameMs > 0.0, "frame time must be reported");
    check(staticFrameMs > 0.0, "static frame time must be reported");
    check(memoryAfter <= memoryBefore, "text rendering must not grow device memory");

    game.shutdownGraphics();
    if (failures != 0) {
        return 1;
    }
    std::cout << "AporiaTextTest passed\n";
    return 0;
}
