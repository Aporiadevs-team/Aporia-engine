#pragma once

#include "Aporia/Core/Export.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Aporia {

struct MeshVertex {
    float position[3];
    float normal[3];
    float uv[2];
};

struct MeshData {
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
};

struct GraphicsContext {
    std::uintptr_t nativeWindow = 0;
};

enum class TextureFormat : std::uint8_t {
    Rgba8Unorm,
    Bgra8Unorm,
    Rgba8Srgb,
};

struct TextureDesc {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    TextureFormat format = TextureFormat::Rgba8Unorm;
    const void* pixels = nullptr;
    std::size_t dataSize = 0;
};

class APORIA_API TextureHandle {
public:
    constexpr TextureHandle() noexcept = default;
    constexpr explicit TextureHandle(std::uint64_t value) noexcept
        : value_(value) {
    }

    [[nodiscard]] constexpr std::uint64_t value() const noexcept {
        return value_;
    }

    [[nodiscard]] constexpr bool valid() const noexcept {
        return value_ != 0;
    }

    friend constexpr bool operator==(TextureHandle, TextureHandle) noexcept = default;

private:
    std::uint64_t value_ = 0;
};

inline constexpr TextureHandle invalidTextureHandle{};

class APORIA_API MeshHandle {
public:
    constexpr MeshHandle() noexcept = default;
    constexpr explicit MeshHandle(std::uint64_t value) noexcept
        : value_(value) {
    }

    [[nodiscard]] constexpr std::uint64_t value() const noexcept {
        return value_;
    }

    [[nodiscard]] constexpr bool valid() const noexcept {
        return value_ != 0;
    }

    friend constexpr bool operator==(MeshHandle, MeshHandle) noexcept = default;

private:
    std::uint64_t value_ = 0;
};

inline constexpr MeshHandle invalidMeshHandle{};

// View parameters for one frame. Matrices are column-major float32 in Vulkan
// clip space (see Mat4::perspectiveVulkan); copy from Mat4 with memcpy.
struct CameraState {
    float viewMatrix[16] = {};
    float projectionMatrix[16] = {};
    double position[3] = {0.0, 0.0, 0.0};
};

// One drawable object for a frame.
struct RenderItem {
    std::uint64_t meshId = 0;
    float modelMatrix[16] = {};
};

struct GraphicsConfig {
    bool headless = true;
    bool enableValidation = false;
};

struct GraphicsStats {
    std::uint64_t frameCount = 0;
    std::uint64_t presentCount = 0;
    std::uint64_t textureCount = 0;
    std::uint64_t boundTextureId = 0;
    std::uint64_t textureUploadCount = 0;
    std::uint64_t deviceLocalMemory = 0;
    std::uint32_t apiVersion = 0;
    std::uint32_t driverVersion = 0;
    std::uint32_t memoryTypeCount = 0;
    std::uint32_t surfaceWidth = 0;
    std::uint32_t surfaceHeight = 0;
    bool headless = true;
    bool nativeWindow = false;
    bool validationEnabled = false;
    std::uint32_t validationMessageCount = 0;
    std::string deviceName;
};

class APORIA_API Graphics {
public:
    Graphics();
    ~Graphics();

    Graphics(const Graphics&) = delete;
    Graphics& operator=(const Graphics&) = delete;
    Graphics(Graphics&&) noexcept;
    Graphics& operator=(Graphics&&) noexcept;

    bool initialize(
        const GraphicsContext& context = {},
        const GraphicsConfig& config = {});
    void shutdown() noexcept;

    [[nodiscard]] bool initialized() const noexcept;
    [[nodiscard]] bool renderFrame() noexcept;
    [[nodiscard]] bool renderHeadlessFrame() noexcept;

    // Draws the given items with the given camera into the current target.
    bool render(const CameraState& camera, const std::vector<RenderItem>& items);

    // Copies the last rendered headless color target into host memory (RGBA8,
    // renderWidth * renderHeight * 4 bytes). Native targets are not readable.
    [[nodiscard]] bool readbackPixels(std::vector<std::uint8_t>& pixels) const;

    TextureHandle createTexture2D(const TextureDesc& description);
    bool updateTexture2D(
        TextureHandle handle,
        const void* pixels,
        std::size_t dataSize);
    bool setTexture2D(TextureHandle handle) noexcept;
    void destroyTexture2D(TextureHandle handle) noexcept;

    MeshHandle createMesh(const MeshData& data);
    void destroyMesh(MeshHandle handle) noexcept;
    [[nodiscard]] std::uint64_t meshVertexCount(MeshHandle handle) const noexcept;

    [[nodiscard]] GraphicsStats stats() const noexcept;
    [[nodiscard]] const std::string& lastError() const noexcept;

private:
    struct Impl;

    bool renderNativeFrame() noexcept;

    std::unique_ptr<Impl> impl_;
};

}
