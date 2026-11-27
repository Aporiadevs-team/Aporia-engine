// Throwaway probe: isolates which render pass hangs the GPU.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "Aporia/Game.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    const int scenario = argc > 1 ? std::atoi(argv[1]) : 0;

    Aporia::Game game;
    Aporia::GraphicsConfig config;
    config.headless = true;
    config.headlessWidth = 64;
    config.headlessHeight = 64;
    config.enableValidation = false;

    switch (scenario) {
        case 0:  // plain frames, no AA, no shadows
            config.antiAliasing = Aporia::AntiAliasingMode::None;
            config.shadows.enabled = false;
            break;
        case 1:  // shadows only
            config.antiAliasing = Aporia::AntiAliasingMode::None;
            break;
        case 2:  // SMAA only
            config.antiAliasing = Aporia::AntiAliasingMode::SMAA;
            config.shadows.enabled = false;
            break;
        case 3:  // SMAA + shadows
            config.antiAliasing = Aporia::AntiAliasingMode::SMAA;
            break;
        case 4:  // TAA + shadows
            config.antiAliasing = Aporia::AntiAliasingMode::TAA;
            break;
        default:
            break;
    }

    if (!game.initGraphics({}, config)) {
        std::printf("scenario %d: init failed: %s\n", scenario, game.graphics().lastError().c_str());
        return 1;
    }

    const Aporia::MeshHandle mesh = game.graphics().createMesh(Aporia::apr::makeBoxMesh(0.5f, 0.5f, 0.5f));
    Aporia::RenderItem item;
    item.meshId = mesh.value();
    const Aporia::Mat4 identity = Aporia::Mat4::identity();
    std::memcpy(item.modelMatrix, &identity, sizeof(identity));

    if (scenario == 5) {
        // Texture upload path: a user texture with pixels, bound as albedo.
        constexpr std::uint8_t pixels[16] = {255, 0, 0, 255, 0, 255, 0, 255,
                                             0, 0, 255, 255, 255, 255, 0, 255};
        Aporia::TextureDesc description;
        description.width = 2;
        description.height = 2;
        description.format = Aporia::TextureFormat::Rgba8Unorm;
        description.pixels = pixels;
        description.dataSize = sizeof(pixels);
        const Aporia::TextureHandle texture = game.graphics().createTexture2D(description);
        std::printf("scenario 5: texture valid=%d\n", texture.valid() ? 1 : 0);
        std::fflush(stdout);
        game.graphics().setTexture2D(texture);
        game.graphics().updateTexture2D(texture, pixels, sizeof(pixels));
        std::printf("scenario 5: texture bound\n");
        std::fflush(stdout);
    }

    Aporia::CameraState camera;
    const Aporia::Mat4 view = Aporia::Mat4::lookAt({0.0, -4.0, 1.2}, {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0});
    const Aporia::Mat4 projection = Aporia::Mat4::perspectiveD3D(0.8, 1.0, 0.1, 100.0);
    std::memcpy(camera.viewMatrix, &view, sizeof(view));
    std::memcpy(camera.projectionMatrix, &projection, sizeof(projection));

    const int frameCount = scenario == 6 ? 40 : 3;
    if (scenario == 7) {
        // The default camera: zero matrices, exactly what renderFrame() uses
        // when the host never set one.
        for (int frame = 0; frame < 8; ++frame) {
            if (!game.graphics().renderFrame()) {
                std::printf("scenario 7: frame %d failed: %s\n", frame,
                            game.graphics().lastError().c_str());
                return 1;
            }
            std::printf("scenario 7: frame %d ok\n", frame);
            std::fflush(stdout);
        }
        std::vector<std::uint8_t> pixels;
        const bool ok = game.graphics().readbackPixels(pixels);
        std::printf("scenario 7: readback ok=%d size=%zu\n", ok ? 1 : 0, pixels.size());
        std::fflush(stdout);
        return 0;
    }
    for (int frame = 0; frame < frameCount; ++frame) {
        if (!game.graphics().render(camera, {item})) {
            std::printf("scenario %d: frame %d failed: %s\n", scenario, frame,
                        game.graphics().lastError().c_str());
            return 1;
        }
        std::printf("scenario %d: frame %d ok\n", scenario, frame);
        std::fflush(stdout);
    }

    std::vector<std::uint8_t> pixels;
    const bool ok = game.graphics().readbackPixels(pixels);
    std::printf("scenario %d: readback ok=%d size=%zu\n", scenario, ok ? 1 : 0, pixels.size());
    std::fflush(stdout);
    return 0;
}
