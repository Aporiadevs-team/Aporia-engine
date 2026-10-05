#include "Aporia/Game.hpp"
#include "Aporia/Graphics/MeshPrimitives.hpp"
#include "Aporia/Math/Matrix4.hpp"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

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

    // Level-of-detail: two sphere levels, switch at 5 m. The near camera must
    // draw level 0, the far camera level 1, and the full-detail triangle count
    // must stay constant.
    const Aporia::MeshData highDetail = Aporia::apr::makeSphereMesh(0.5f, 32, 20);
    const Aporia::MeshData lowDetail = Aporia::apr::makeSphereMesh(0.5f, 8, 6);
    const std::uint64_t highTriangles = highDetail.indices.size() / 3;
    const std::uint64_t lowTriangles = lowDetail.indices.size() / 3;
    check(highTriangles > lowTriangles, "high LOD level must carry more triangles");

    const Aporia::MeshHandle lod = game.graphics().createLodMesh(
        {highDetail, lowDetail}, {5.0f}, Aporia::LodBoundary::Sphere);
    check(lod.valid(), "LOD group creation must return a valid handle");
    check(game.graphics().lodLevelCount(lod) == 2, "LOD group must report two levels");

    Aporia::RenderItem item;
    item.meshId = lod.value();
    const Aporia::Mat4 identity = Aporia::Mat4::identity();
    std::memcpy(item.modelMatrix, &identity, sizeof(identity));

    Aporia::CameraState farCamera;
    farCamera.position[2] = 10.0;
    check(game.graphics().render(farCamera, {item}), "LOD far frame must render");
    const Aporia::GraphicsStats farStats = game.graphics().stats();
    check(farStats.lodDrawTriangles == lowTriangles, "far camera must select the low LOD level");
    check(farStats.lodFullTriangles == highTriangles, "full-detail triangle count must be tracked");

    Aporia::CameraState nearCamera;
    nearCamera.position[2] = 1.0;
    check(game.graphics().render(nearCamera, {item}), "LOD near frame must render");
    const Aporia::GraphicsStats nearStats = game.graphics().stats();
    check(nearStats.lodDrawTriangles == highTriangles, "near camera must select the high LOD level");
    check(nearStats.lodGroupCount == 1, "LOD group count must be reported");

    game.graphics().destroyLodMesh(lod);
    check(!game.graphics().lodMeshValid(lod), "destroyed LOD group must be invalid");

    // Anti-aliasing strength is a live knob.
    game.graphics().setAntiAliasingStrength(0.25f);
    check(game.graphics().antiAliasingStrength() > 0.24f && game.graphics().antiAliasingStrength() < 0.26f,
          "AA strength must be settable");
    check(game.graphics().stats().antiAliasingStrength > 0.24f &&
          game.graphics().stats().antiAliasingStrength < 0.26f,
          "AA strength must be reported in stats");

    // TAA path: HDR target + temporal resolve must run and read back cleanly.
    const Aporia::MeshHandle taaBox = game.graphics().createMesh(Aporia::apr::makeBoxMesh(0.5f, 0.5f, 0.5f));
    check(taaBox.valid(), "TAA test mesh must be created");
    Aporia::RenderItem taaItem;
    taaItem.meshId = taaBox.value();
    std::memcpy(taaItem.modelMatrix, &identity, sizeof(identity));

    // Diagnostic: SMAA must measurably change hard-edge pixels.
    {
        Aporia::CameraState edgeCamera;
        const Aporia::Mat4 view = Aporia::Mat4::lookAt({0.0, -4.0, 1.2}, {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0});
        const Aporia::Mat4 projection = Aporia::Mat4::perspectiveVulkan(0.8, 1.0, 0.1, 100.0);
        std::memcpy(edgeCamera.viewMatrix, &view, sizeof(view));
        std::memcpy(edgeCamera.projectionMatrix, &projection, sizeof(projection));
        edgeCamera.position[0] = 0.0;
        edgeCamera.position[1] = -4.0;
        edgeCamera.position[2] = 1.2;

        Aporia::RenderItem edgeItem;
        edgeItem.meshId = taaBox.value();
        const Aporia::Mat4 edgeModel = Aporia::Mat4::identity();
        std::memcpy(edgeItem.modelMatrix, &edgeModel, sizeof(edgeModel));
        edgeItem.color[0] = 1.0f;
        edgeItem.color[1] = 1.0f;
        edgeItem.color[2] = 1.0f;

        std::vector<std::uint8_t> noAa;
        std::vector<std::uint8_t> smaa;
        game.graphics().setAntiAliasing(Aporia::AntiAliasingMode::None);
        if (game.graphics().render(edgeCamera, {edgeItem})) {
            (void)game.graphics().readbackPixels(noAa);
        }
        game.graphics().setAntiAliasing(Aporia::AntiAliasingMode::SMAA);
        game.graphics().setAntiAliasingStrength(1.0f);
        if (game.graphics().render(edgeCamera, {edgeItem})) {
            (void)game.graphics().readbackPixels(smaa);
        }
        std::size_t differing = 0;
        if (noAa.size() == smaa.size()) {
            for (std::size_t index = 0; index < noAa.size(); ++index) {
                if (noAa[index] != smaa[index]) {
                    ++differing;
                }
            }
        }
        std::cout << "SMAA pixel difference: " << differing << " bytes\n";
        check(differing > 0, "SMAA must change hard-edge pixels");
    }

    game.graphics().setAntiAliasing(Aporia::AntiAliasingMode::TAA);
    check(game.graphics().antiAliasing() == Aporia::AntiAliasingMode::TAA, "AA mode must switch to TAA");
    bool taaFramesOk = true;
    for (int frame = 0; frame < 6; ++frame) {
        Aporia::CameraState moving = farCamera;
        moving.position[0] = 0.02 * static_cast<double>(frame);
        taaFramesOk = taaFramesOk && game.graphics().render(moving, {taaItem});
    }
    check(taaFramesOk, "TAA frames must render");
    std::vector<std::uint8_t> taaPixels;
    check(game.graphics().readbackPixels(taaPixels), "TAA readback must succeed");
    check(taaPixels.size() == 64u * 64u * 4u, "TAA readback must match the target size");
    game.graphics().destroyMesh(taaBox);

#if defined(APORIA_EXPERIMENTAL)
    // EXPERIMENTAL multi-layer materials: four diffuse layers + masks bound per
    // draw item, so a single render() can show several textures at once.
    {
        const auto makeSolid = [&](std::uint8_t r, std::uint8_t g, std::uint8_t b) {
            const std::uint8_t pixels[4] = {r, g, b, 255};
            Aporia::TextureDesc desc;
            desc.width = 1;
            desc.height = 1;
            desc.format = Aporia::TextureFormat::Rgba8Unorm;
            desc.pixels = pixels;
            desc.dataSize = sizeof(pixels);
            return game.graphics().createTexture2D(desc);
        };
        Aporia::TextureHandle layers[4] = {
            makeSolid(24, 24, 28), makeSolid(230, 230, 230),
            makeSolid(120, 80, 40), makeSolid(60, 120, 60)};
        Aporia::TextureHandle masks[2] = {makeSolid(255, 0, 0), makeSolid(0, 0, 0)};

        Aporia::MaterialDesc materialDesc;
        for (int index = 0; index < 4; ++index) {
            materialDesc.diffuse[index] = layers[index];
        }
        materialDesc.mask[0] = masks[0];
        materialDesc.mask[1] = masks[1];
        const Aporia::MaterialHandle material = game.graphics().createMaterial(materialDesc);
        check(material.valid() && game.graphics().materialValid(material), "exp material must be created");

        const Aporia::MeshHandle expMesh = game.graphics().createMesh(Aporia::apr::makeBoxMesh(0.5f, 0.5f, 0.5f));
        Aporia::RenderItem expItem;
        expItem.meshId = expMesh.value();
        std::memcpy(expItem.modelMatrix, &identity, sizeof(identity));
        expItem.material = material;
        check(game.graphics().render(farCamera, {expItem}), "exp material frame must render");

        game.graphics().destroyMaterial(material);
        check(!game.graphics().materialValid(material), "exp material must be destroyed");
        game.graphics().destroyMesh(expMesh);
        for (Aporia::TextureHandle handle : layers) {
            game.graphics().destroyTexture2D(handle);
        }
        for (Aporia::TextureHandle handle : masks) {
            game.graphics().destroyTexture2D(handle);
        }
    }
#endif

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
