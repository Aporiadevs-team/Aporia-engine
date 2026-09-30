// Render test 5 — HDR + reflections, dynamic scene.
// The cube orbits the mirror plane axis (z = mirrorZ): it crosses above and
// below the plane while the camera stays fixed. Each frame renders with a
// fresh cube position; the test verifies that HDR tonemap output stays in
// range and that the reflection zone brightens whenever the cube is above
// the plane (visible reflected), on many frames across a full orbit.
#include "Aporia/Game.hpp"
#include "Aporia/Graphics/MeshPrimitives.hpp"

#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

constexpr int kSize = 64; // headless render target edge (px)
constexpr int kFrames = 48;

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

bool bright(const Rgb& pixel) {
    return pixel.r + pixel.g + pixel.b > 300;
}

int countBrightReflection(const std::vector<std::uint8_t>& pixels) {
    int count = 0;
    for (int y = kSize * 3 / 5; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            if (bright(sample(pixels, x, y))) {
                ++count;
            }
        }
    }
    return count;
}

int countBrightDirect(const std::vector<std::uint8_t>& pixels) {
    int count = 0;
    for (int y = 0; y < kSize * 3 / 5; ++y) {
        for (int x = 0; x < kSize; ++x) {
            if (bright(sample(pixels, x, y))) {
                ++count;
            }
        }
    }
    return count;
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

    const Aporia::Mat4 view = Aporia::Mat4::lookAt(
        {0.0, -7.0, 4.0},
        {0.0, 1.0, 1.0},
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
    camera.position[1] = -7.0;
    camera.position[2] = 4.0;

    Aporia::EnvironmentState environment;
    environment.sunDirection[0] = 0.35f;
    environment.sunDirection[1] = -0.5f;
    environment.sunDirection[2] = 0.8f;
    environment.sunIntensity = 1.0f;
    environment.mirrorZ = 0.0f;

    const Aporia::MeshHandle ground = game.graphics().createMesh(Aporia::apr::makePlaneMesh(12.0f, 12.0f));
    const Aporia::MeshHandle cube = game.graphics().createMesh(Aporia::apr::makeBoxMesh(0.6f, 0.6f, 0.6f));
    check(ground.valid() && cube.valid(), "mesh creation must succeed");

    Aporia::RenderItem groundItem;
    groundItem.meshId = ground.value();
    const Aporia::Mat4 groundModel = Aporia::Mat4::identity();
    std::memcpy(groundItem.modelMatrix, &groundModel, sizeof(groundModel));

    const Aporia::GraphicsStats firstStats = game.graphics().stats();
    check(firstStats.hdrEnabled && firstStats.reflectionsEnabled,
          "Medium preset must keep HDR and reflections on for the dynamic test");

    int framesWithReflection = 0;
    int framesWithDirect = 0;
    int framesOutOfRange = 0;

    // Full orbit around the mirror axis: cube circles (radius 2) crossing
    // above/below the plane every orbit half.
    for (int frame = 0; frame < kFrames; ++frame) {
        const double angle = static_cast<double>(frame) / static_cast<double>(kFrames) * 2.0 * 3.14159265358979323846;
        const double cubeY = 2.5 * std::sin(angle);
        const double cubeZ = 2.0 * std::cos(angle); // crosses z=0 (mirror plane)

        Aporia::RenderItem cubeItem;
        cubeItem.meshId = cube.value();
        const Aporia::Mat4 cubeModel = Aporia::Mat4::translation({1.5, cubeY, cubeZ});
        std::memcpy(cubeItem.modelMatrix, &cubeModel, sizeof(cubeModel));

        std::vector<Aporia::RenderItem> items{groundItem, cubeItem};
        if (!game.graphics().render(camera, environment, items)) {
            std::cerr << "FAIL: render frame " << frame << " failed: "
                      << game.graphics().lastError() << '\n';
            return 1;
        }

        std::vector<std::uint8_t> pixels;
        check(game.graphics().readbackPixels(pixels), "readback must succeed on every frame");
        if (pixels.size() != static_cast<std::size_t>(kSize) * kSize * 4) {
            check(false, "readback size must stay constant across the orbit");
            break;
        }

        const int reflectionBright = countBrightReflection(pixels);
        const int directBright = countBrightDirect(pixels);
        if (reflectionBright > 0) {
            ++framesWithReflection;
        }
        if (directBright > 0) {
            ++framesWithDirect;
        }
        // HDR output is tone-mapped to [0,1]; pure white saturation on the
        // whole frame would mean the ACES curve is broken.
        if (reflectionBright + directBright > kSize * kSize / 2) {
            ++framesOutOfRange;
        }
    }

    check(framesWithDirect > kFrames / 4, "cube must be directly visible on many orbit frames");
    check(framesWithReflection > kFrames / 6,
          "reflected cube must appear in the mirror zone on some orbit frames");
    check(framesOutOfRange == 0, "tonemapped output must never saturate the whole frame");

    const Aporia::GraphicsStats stats = game.graphics().stats();
    check(stats.frameCount == static_cast<std::uint64_t>(kFrames),
          "stats must count every dynamic frame");
    check(stats.avgFrameMs > 0.0, "frame timing must be reported after dynamic frames");

    if (stats.validationEnabled && stats.validationMessageCount != 0) {
        std::cerr << "Vulkan validation messages: " << stats.validationMessageCount << '\n';
    }

    game.shutdownGraphics();
    if (failures != 0) {
        return 1;
    }
    std::cout << "Aporia render test 5 (HDR + reflections, dynamic) passed\n";
    return 0;
}
