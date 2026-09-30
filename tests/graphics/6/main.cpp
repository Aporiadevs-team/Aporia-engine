// Render test 6 — shadows.
// Headless scene: ground plane with a box standing on it, sun low to the
// side so the box casts a visible shadow streak on the ground. Runs the
// scene on every quality preset (setQuality) and checks:
//   - preset stats (shadow map size, taps via shadowStrength, HDR flags),
//   - shadow pixels actually appear (darker than the lit ground),
//   - higher presets produce larger shadow maps without validation errors.
#include "Aporia/Game.hpp"
#include "Aporia/Graphics/MeshPrimitives.hpp"

#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

constexpr int kSize = 64; // headless render target edge (px)

int failures = 0;

void check(bool condition, const char* message) {
    if (condition) {
        return;
    }
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

struct Rgb {
    std::uint8_t r, g, b;
};

Rgb sample(const std::vector<std::uint8_t>& pixels, int x, int y) {
    const std::size_t offset = (static_cast<std::size_t>(y) * kSize + static_cast<std::size_t>(x)) * 4;
    return {pixels[offset], pixels[offset + 1], pixels[offset + 2]};
}

int luminance(const Rgb& pixel) {
    return (static_cast<int>(pixel.r) + pixel.g + pixel.b) / 3;
}

}

int main() {
    Aporia::Game game;
    Aporia::GraphicsConfig config;
    config.enableValidation = true;
    // Sun low from -Y so the box shadow falls toward +Y (toward the camera).
    config.sunDirection[0] = 0.0f;
    config.sunDirection[1] = -0.55f;
    config.sunDirection[2] = 0.85f;
    config.shadowExtent = 10.0f;
    if (!game.initGraphics({}, config)) {
        std::cerr << "initGraphics failed: " << game.graphics().lastError() << '\n';
        return 1;
    }

    const Aporia::Mat4 view = Aporia::Mat4::lookAt(
        {0.0, -5.0, 4.5},
        {0.0, 1.5, 0.0},
        {0.0, 0.0, 1.0});
    const Aporia::Mat4 projection = Aporia::Mat4::perspectiveVulkan(
        70.0 * 3.14159265358979323846 / 180.0,
        static_cast<double>(kSize) / static_cast<double>(kSize),
        0.1,
        200.0);
    Aporia::CameraState camera;
    std::memcpy(camera.viewMatrix, &view, sizeof(view));
    std::memcpy(camera.projectionMatrix, &projection, sizeof(projection));
    camera.position[0] = 0.0;
    camera.position[1] = -5.0;
    camera.position[2] = 4.5;

    Aporia::EnvironmentState environment;
    environment.sunDirection[0] = 0.0f;
    environment.sunDirection[1] = -0.55f;
    environment.sunDirection[2] = 0.85f;
    environment.sunIntensity = 1.0f;
    environment.mirrorZ = 0.0f;

    const Aporia::MeshHandle ground = game.graphics().createMesh(Aporia::apr::makePlaneMesh(8.0f, 8.0f));
    const Aporia::MeshHandle box = game.graphics().createMesh(Aporia::apr::makeBoxMesh(0.6f, 0.6f, 1.5f));
    check(ground.valid() && box.valid(), "mesh creation must succeed");

    Aporia::RenderItem groundItem;
    groundItem.meshId = ground.value();
    const Aporia::Mat4 groundModel = Aporia::Mat4::identity();
    std::memcpy(groundItem.modelMatrix, &groundModel, sizeof(groundModel));

    Aporia::RenderItem boxItem;
    boxItem.meshId = box.value();
    const Aporia::Mat4 boxModel = Aporia::Mat4::translation({0.0, 0.0, 1.5});
    std::memcpy(boxItem.modelMatrix, &boxModel, sizeof(boxModel));

    const std::vector<Aporia::RenderItem> items{groundItem, boxItem};

    const char* presetNames[] = {"Low", "Medium", "High"};
    const std::uint32_t expectedShadowSizes[] = {512, 1024, 2048};
    const bool expectedHdr[] = {false, true, true};
    const bool expectedReflections[] = {false, true, true};
    const std::uint32_t expectedTextureScale[] = {50, 75, 100};

    int totalShadowPixels = 0;

    for (int preset = 0; preset < 3; ++preset) {
        const Aporia::GraphicsQuality quality = static_cast<Aporia::GraphicsQuality>(preset);
        if (!game.graphics().setQuality(quality)) {
            std::cerr << "FAIL: setQuality(" << presetNames[preset] << ") failed: "
                      << game.graphics().lastError() << '\n';
            ++failures;
            break;
        }

        if (!game.graphics().render(camera, environment, items)) {
            std::cerr << "FAIL: render (" << presetNames[preset] << ") failed: "
                      << game.graphics().lastError() << '\n';
            return 1;
        }

        const Aporia::GraphicsStats stats = game.graphics().stats();
        check(stats.quality == static_cast<std::uint32_t>(preset),
              "stats must report the active preset");
        check(stats.shadowMapSize == expectedShadowSizes[preset],
              "shadow map size must follow the preset");
        check(stats.hdrEnabled == expectedHdr[preset],
              "HDR flag must follow the preset");
        check(stats.reflectionsEnabled == expectedReflections[preset],
              "reflection flag must follow the preset");
        check(stats.textureScalePercent == expectedTextureScale[preset],
              "texture scale must follow the preset");

        std::vector<std::uint8_t> pixels;
        check(game.graphics().readbackPixels(pixels), "readback must succeed on every preset");

        // Ground in front of the box (toward the camera) should be in shadow:
        // the sun comes from -Y, so the shadow streak lands around +Y of the
        // box base. Compare its luminance to the lit ground on the sides.
        int shadowPixels = 0;
        int litPixels = 0;
        int shadowSum = 0;
        int litSum = 0;
        for (int y = kSize * 3 / 5; y < kSize; ++y) {
            for (int x = 0; x < kSize; ++x) {
                const Rgb pixel = sample(pixels, x, y);
                const int lum = luminance(pixel);
                // Center column strip below the box = shadow direction.
                if (x > kSize * 2 / 5 && x < kSize * 3 / 5) {
                    shadowPixels += 1;
                    shadowSum += lum;
                } else {
                    litPixels += 1;
                    litSum += lum;
                }
            }
        }
        check(shadowPixels > 0 && litPixels > 0, "ground must be visible on every preset");
        const double shadowAvg = static_cast<double>(shadowSum) / shadowPixels;
        const double litAvg = static_cast<double>(litSum) / litPixels;
        check(shadowAvg < litAvg,
              "shadowed ground strip must be darker than lit ground on the preset");
        totalShadowPixels += shadowPixels;

        std::cout << presetNames[preset] << ": shadow avg " << static_cast<int>(shadowAvg)
                  << " vs lit avg " << static_cast<int>(litAvg)
                  << ", shadow map " << stats.shadowMapSize << "px\n";
    }

    check(totalShadowPixels > 0, "shadow comparison must have run on every preset");

    if (const Aporia::GraphicsStats stats = game.graphics().stats();
        stats.validationEnabled && stats.validationMessageCount != 0) {
        std::cerr << "Vulkan validation messages: " << stats.validationMessageCount << '\n';
    }

    game.shutdownGraphics();
    if (failures != 0) {
        return 1;
    }
    std::cout << "Aporia render test 6 (shadows, all presets) passed\n";
    return 0;
}
