#include "Aporia/Game.hpp"

#include <cstdint>
#include <iostream>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (condition) {
        return;
    }
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

}

int main() {
    Aporia::Game game;
    Aporia::GraphicsConfig config;
    config.enableValidation = true;

    check(game.initGraphics({}, config), "headless Vulkan initialization must succeed");
    if (failures != 0) {
        std::cerr << game.graphics().lastError() << '\n';
        return 1;
    }

    check(game.graphics().initialized(), "graphics must report initialized state");

    constexpr std::uint8_t texturePixels[] = {
        255, 0, 0, 255,
        0, 255, 0, 255,
        0, 0, 255, 255,
        255, 255, 0, 255,
    };
    Aporia::TextureDesc textureDescription;
    textureDescription.width = 2;
    textureDescription.height = 2;
    textureDescription.format = Aporia::TextureFormat::Rgba8Unorm;
    textureDescription.pixels = texturePixels;
    textureDescription.dataSize = sizeof(texturePixels);
    const Aporia::TextureHandle texture = game.graphics().createTexture2D(textureDescription);
    check(texture.valid(), "texture creation must return a valid handle");
    check(game.graphics().setTexture2D(texture), "texture binding must succeed");
    check(game.graphics().updateTexture2D(texture, texturePixels, sizeof(texturePixels)), "texture update must succeed");

    const Aporia::GraphicsStats textureStats = game.graphics().stats();
    check(textureStats.textureCount == 2, "default and user textures must be counted");
    check(textureStats.boundTextureId == texture.value(), "bound texture must be reported");
    check(textureStats.textureUploadCount >= 3, "texture uploads must be reported");

    for (int frame = 0; frame < 8; ++frame) {
        check(game.graphics().renderFrame(), "headless Vulkan frame must submit successfully");
    }

    const Aporia::GraphicsStats stats = game.graphics().stats();
    check(stats.frameCount == 8, "graphics stats must count submitted frames");
    check(stats.apiVersion >= 0x00401000, "Vulkan API version must be reported");
    check(!stats.deviceName.empty(), "Vulkan device name must be reported");
    check(stats.memoryTypeCount > 0, "Vulkan memory types must be reported");
    if (stats.validationEnabled) {
        std::cerr << "Vulkan validation messages: " << stats.validationMessageCount << '\n';
    }

    game.graphics().destroyTexture2D(texture);
    check(game.graphics().stats().textureCount == 1, "destroyed texture must be removed");
    check(game.graphics().stats().boundTextureId != texture.value(), "destroying a bound texture must restore the default");
    check(game.graphics().renderFrame(), "default texture frame must render after texture destruction");

    game.shutdownGraphics();
    check(!game.graphics().initialized(), "graphics must report shutdown state");

    for (int cycle = 0; cycle < 3; ++cycle) {
        check(game.initGraphics({}, config), "repeated headless Vulkan initialization must succeed");
        check(game.graphics().renderHeadlessFrame(), "repeated headless Vulkan frame must succeed");
        game.shutdownGraphics();
    }

    if (failures != 0) {
        return 1;
    }
    std::cout << "Aporia graphics smoke test passed\n";
    return 0;
}
