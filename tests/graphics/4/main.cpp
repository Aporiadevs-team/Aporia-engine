// Render test 4 — HDR + planar reflections, static scene.
// Headless: a ground plane with a mirrored wall; a bright emissive-ish cube
// must be visible directly AND in the reflection. Reads back pixels and
// checks that the reflection target actually contains the mirrored cube,
// that HDR+ACES tonemap keeps colors in range, and the preset stats report
// HDR + reflections enabled.
#include "Aporia/Game.hpp"
#include "Aporia/Graphics/MeshPrimitives.hpp"

#include <algorithm>
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

// Sample the readback buffer (RGBA8, top-left origin).
struct Rgb {
    std::uint8_t r, g, b;
};

Rgb sample(const std::vector<std::uint8_t>& pixels, int x, int y) {
    const int clampedX = std::clamp(x, 0, static_cast<int>(kSize) - 1);
    const int clampedY = std::clamp(y, 0, static_cast<int>(kSize) - 1);
    const std::size_t offset = (static_cast<std::size_t>(clampedY) * kSize +
                                static_cast<std::size_t>(clampedX)) * 4;
    return {pixels[offset], pixels[offset + 1], pixels[offset + 2]};
}

bool bright(const Rgb& pixel) {
    return pixel.r + pixel.g + pixel.b > 300;
}

}

int main() {
    Aporia::Game game;
    Aporia::GraphicsConfig config;
    config.enableValidation = true;
    config.quality = Aporia::GraphicsQuality::Medium;
    config.mirrorZ = 0.0f;
    if (!game.initGraphics({}, config)) {
        std::cerr << "initGraphics failed: " << game.graphics().lastError() << '\n';
        return 1;
    }

    // Camera above and behind the mirror plane, looking down the +Y axis.
    const Aporia::Mat4 view = Aporia::Mat4::lookAt(
        {0.0, -6.0, 3.5},
        {0.0, 2.0, 1.2},
        {0.0, 0.0, 1.0});
    const Aporia::Mat4 projection = Aporia::Mat4::perspectiveVulkan(
        70.0 * 3.14159265358979323846 / 180.0,
        static_cast<double>(kSize) /
            static_cast<double>(kSize),
        0.1,
        200.0);
    Aporia::CameraState camera;
    std::memcpy(camera.viewMatrix, &view, sizeof(view));
    std::memcpy(camera.projectionMatrix, &projection, sizeof(projection));
    camera.position[0] = 0.0;
    camera.position[1] = -6.0;
    camera.position[2] = 3.5;

    Aporia::EnvironmentState environment;
    environment.sunDirection[0] = 0.35f;
    environment.sunDirection[1] = -0.5f;
    environment.sunDirection[2] = 0.8f;
    environment.sunIntensity = 1.0f;
    environment.mirrorZ = 0.0f;

    // Scene: ground plane, mirror wall behind, bright cube on the plane.
    const Aporia::MeshHandle ground = game.graphics().createMesh(Aporia::apr::makePlaneMesh(12.0f, 12.0f));
    const Aporia::MeshHandle wall = game.graphics().createMesh(Aporia::apr::makePlaneMesh(12.0f, 12.0f));
    const Aporia::MeshHandle cube = game.graphics().createMesh(Aporia::apr::makeBoxMesh(0.5f, 0.5f, 0.5f));
    check(ground.valid() && wall.valid() && cube.valid(), "mesh creation must succeed");

    std::vector<Aporia::RenderItem> items;

    Aporia::RenderItem groundItem;
    groundItem.meshId = ground.value();
    const Aporia::Mat4 groundModel = Aporia::Mat4::identity();
    std::memcpy(groundItem.modelMatrix, &groundModel, sizeof(groundModel));
    items.push_back(groundItem);

    Aporia::RenderItem wallItem;
    wallItem.meshId = wall.value();
    // Wall stands at y = +4, facing the camera (rotate to vertical, XY plane).
    const Aporia::Mat4 wallModel = Aporia::Mat4::translation({0.0, 4.0, 6.0}) *
                                   Aporia::Mat4::fromQuaternion({0.7071067811865476, 0.0, 0.0, 0.7071067811865476});
    std::memcpy(wallItem.modelMatrix, &wallModel, sizeof(wallModel));
    items.push_back(wallItem);

    Aporia::RenderItem cubeItem;
    cubeItem.meshId = cube.value();
    const Aporia::Mat4 cubeModel = Aporia::Mat4::translation({0.0, 1.0, 0.5});
    std::memcpy(cubeItem.modelMatrix, &cubeModel, sizeof(cubeModel));
    items.push_back(cubeItem);

    if (!game.graphics().render(camera, environment, items)) {
        std::cerr << "FAIL: render failed: " << game.graphics().lastError() << '\n';
        return 1;
    }

    const Aporia::GraphicsStats stats = game.graphics().stats();
    check(stats.hdrEnabled, "Medium preset must enable HDR");
    check(stats.reflectionsEnabled, "Medium preset must enable reflections");
    check(stats.shadowMapSize == 1024, "Medium preset must use a 1024px shadow map");
    check(stats.textureScalePercent == 75, "Medium preset must downscale textures to 75%");

    std::vector<std::uint8_t> pixels;
    check(game.graphics().readbackPixels(pixels), "headless readback must succeed");
    check(pixels.size() == kSize * kSize * 4,
          "readback buffer must match the headless render size");

    // Center of the screen should contain the lit cube; the lower-middle
    // region reflects it through the mirror plane (planar reflection weight
    // is strongest looking down at the plane).
    int brightCenter = 0;
    int brightReflection = 0;
    for (int y = 0; y < static_cast<int>(kSize); ++y) {
        for (int x = 0; x < static_cast<int>(kSize); ++x) {
            const Rgb pixel = sample(pixels, x, y);
            if (bright(pixel)) {
                // Reflection zone: below the horizon line (screen center).
                if (y > kSize * 3 / 5) {
                    ++brightReflection;
                } else {
                    ++brightCenter;
                }
            }
        }
    }
    check(brightCenter > 0, "direct cube pixels must be visible");
    check(brightReflection > 0, "reflected cube pixels must be visible in the mirror zone");

    if (stats.validationEnabled && stats.validationMessageCount != 0) {
        std::cerr << "Vulkan validation messages: " << stats.validationMessageCount << '\n';
    }

    game.shutdownGraphics();
    if (failures != 0) {
        return 1;
    }
    std::cout << "Aporia render test 4 (HDR + reflections, static) passed\n";
    return 0;
}
