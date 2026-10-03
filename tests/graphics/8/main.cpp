// Render test 8 — TTF/MSDF text.
// Headless: bakes a system TTF into an alpha coverage atlas, renders a text run
// through the TextRenderer, and checks that glyph pixels actually appear.
#include "Aporia/Game.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr int kSize = 64;
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
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/segoeui.ttf",
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

int main() {
    const std::string fontPath = firstExistingFont();
    if (fontPath.empty()) {
        std::cout << "Aporia render test 8 skipped: no system TTF found\n";
        return 0;
    }

    Aporia::Game game;
    Aporia::GraphicsConfig config;
    config.headless = true;
    config.enableValidation = true;
    config.quality = Aporia::GraphicsQuality::High;
    if (!game.initGraphics({}, config)) {
        std::cerr << "initGraphics failed: " << game.graphics().lastError() << '\n';
        return 1;
    }

    const Aporia::FontHandle font = game.graphics().createFontFromTtf(fontPath, 48.0f);
    if (!font.valid()) {
        std::cerr << "FAIL: TTF bake failed: " << game.graphics().lastError() << '\n';
        game.shutdownGraphics();
        return 1;
    }
    check(game.graphics().fontValid(font), "baked font must be registered");

    Aporia::CameraState camera;
    const Aporia::Mat4 identity = Aporia::Mat4::identity();
    std::memcpy(camera.viewMatrix, &identity, sizeof(identity));
    std::memcpy(camera.projectionMatrix, &identity, sizeof(identity));

    Aporia::UiDrawList ui;
    ui.font = font;
    Aporia::UiTextRun run;
    run.text = "HELLO";
    run.x = 4.0f;
    run.y = 4.0f;
    run.size = 48.0f;
    run.color = {1.0f, 1.0f, 1.0f, 1.0f};
    ui.textRuns.push_back(run);
    game.graphics().setUiDrawList(ui);

    if (!game.graphics().render(camera, {}, {})) {
        std::cerr << "FAIL: render failed: " << game.graphics().lastError() << '\n';
        game.shutdownGraphics();
        return 1;
    }

    std::vector<std::uint8_t> pixels;
    check(game.graphics().readbackPixels(pixels), "readback must succeed");
    if (pixels.size() == static_cast<std::size_t>(kSize) * kSize * 4) {
        int brightPixels = 0;
        for (std::size_t index = 0; index < pixels.size(); index += 4) {
            if (static_cast<int>(pixels[index]) + pixels[index + 1] + pixels[index + 2] > 200) {
                ++brightPixels;
            }
        }
        check(brightPixels > 0, "baked text must produce visible glyph pixels");
        std::cout << "text bright pixels: " << brightPixels << '\n';
    }

    // Orientation probe: "L" has its spine on the left and foot at the bottom,
    // so a mirrored render would flip the quadrant distribution.
    Aporia::UiDrawList probe;
    probe.font = font;
    probe.textRuns.push_back({"L", 4.0f, 4.0f, 48.0f, {1.0f, 1.0f, 1.0f, 1.0f}});
    game.graphics().setUiDrawList(probe);
    if (game.graphics().render(camera, {}, {})) {
        std::vector<std::uint8_t> probePixels;
        if (game.graphics().readbackPixels(probePixels) &&
            probePixels.size() == static_cast<std::size_t>(kSize) * kSize * 4) {
            int leftCount = 0, rightCount = 0, topCount = 0, bottomCount = 0;
            for (int y = 0; y < kSize; ++y) {
                for (int x = 0; x < kSize; ++x) {
                    const std::size_t offset = (static_cast<std::size_t>(y) * kSize + x) * 4;
                    const int sum = probePixels[offset] + probePixels[offset + 1] + probePixels[offset + 2];
                    if (sum <= 200) {
                        continue;
                    }
                    (x < kSize / 2 ? leftCount : rightCount)++;
                    (y < kSize / 2 ? topCount : bottomCount)++;
                }
            }
            std::cout << "L probe left=" << leftCount << " right=" << rightCount
                      << " top=" << topCount << " bottom=" << bottomCount << '\n';
            check(leftCount > rightCount, "glyph must not be mirrored horizontally");
            check(bottomCount > topCount, "glyph must not be mirrored vertically");
        }
    }

    // Rounded-rect probe: a red panel must cover its area in the final image.
    Aporia::UiDrawList rectProbe;
    rectProbe.cornerRadius = 8.0f;
    rectProbe.roundedRects.push_back({{10.0f, 10.0f, 40.0f, 40.0f}, {1.0f, 0.0f, 0.0f, 1.0f}});
    game.graphics().setUiDrawList(rectProbe);
    if (game.graphics().render(camera, {}, {})) {
        std::vector<std::uint8_t> rectPixels;
        if (game.graphics().readbackPixels(rectPixels) &&
            rectPixels.size() == static_cast<std::size_t>(kSize) * kSize * 4) {
            const std::size_t offset = (static_cast<std::size_t>(30) * kSize + 30) * 4;
            const bool red = rectPixels[offset] > 150 && rectPixels[offset + 1] < 100 && rectPixels[offset + 2] < 100;
            std::cout << "rect probe center rgb=" << static_cast<int>(rectPixels[offset]) << ','
                      << static_cast<int>(rectPixels[offset + 1]) << ',' << static_cast<int>(rectPixels[offset + 2]) << '\n';
            check(red, "rounded rect must render its fill color");
        }
    }

    // Multi-run probe: every text run must survive batched rendering (a per-run
    // buffer write would leave only the last run visible).
    Aporia::UiDrawList multi;
    multi.font = font;
    multi.textRuns.push_back({"TOP", 4.0f, 2.0f, 20.0f, {1.0f, 1.0f, 1.0f, 1.0f}});
    multi.textRuns.push_back({"BOTTOM", 4.0f, 40.0f, 20.0f, {1.0f, 1.0f, 1.0f, 1.0f}});
    game.graphics().setUiDrawList(multi);
    if (game.graphics().render(camera, {}, {})) {
        std::vector<std::uint8_t> multiPixels;
        if (game.graphics().readbackPixels(multiPixels) &&
            multiPixels.size() == static_cast<std::size_t>(kSize) * kSize * 4) {
            int topBright = 0;
            int bottomBright = 0;
            for (int y = 0; y < kSize; ++y) {
                for (int x = 0; x < kSize; ++x) {
                    const std::size_t offset = (static_cast<std::size_t>(y) * kSize + x) * 4;
                    if (int(multiPixels[offset]) + multiPixels[offset + 1] + multiPixels[offset + 2] <= 200) {
                        continue;
                    }
                    (y < kSize / 2 ? topBright : bottomBright)++;
                }
            }
            std::cout << "multi probe top=" << topBright << " bottom=" << bottomBright << '\n';
            check(topBright > 0, "first text run must render");
            check(bottomBright > 0, "second text run must render");
        }
    }

    game.shutdownGraphics();
    if (failures != 0) {
        return 1;
    }
    std::cout << "Aporia render test 8 (text) passed\n";
    return 0;
}
