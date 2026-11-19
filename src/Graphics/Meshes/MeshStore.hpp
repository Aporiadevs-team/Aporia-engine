#pragma once

#include "Aporia/Graphics/Graphics.hpp"
#include "MeshRenderer.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.h>

namespace Aporia::graphics_internal {

// Owns mesh and LOD-group handles: geometry buffers, the id space and the
// per-frame LOD selection. The facade talks to this store instead of touching
// the handle maps directly.
class MeshStore {
public:
    struct Device {
        VkDevice device = VK_NULL_HANDLE;
        VkPhysicalDeviceMemoryProperties memoryProperties{};
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkQueue queue = VK_NULL_HANDLE;
    };

    // Level-of-detail group. A handle resolves to one level mesh per frame,
    // picked by camera distance and the boundary shape.
    struct LodGroup {
        std::vector<std::uint64_t> meshes;  // level mesh ids, near -> far
        std::vector<float> maxDistances;    // range for every level except the last
        LodBoundary boundary = LodBoundary::Sphere;
        float center[3] = {0.0f, 0.0f, 0.0f};      // bounding volume center (mesh-local)
        float halfExtents[3] = {0.5f, 0.5f, 0.5f}; // bounding volume (mesh-local)
        float boundingRadius = 0.5f;
        std::uint32_t lastLevel = 0;
    };

    void setDevice(const Device& device) noexcept { device_ = device; }

    // Validates and uploads a mesh. `error` is set on failure.
    [[nodiscard]] MeshHandle create(const MeshData& data, std::string& error);
    // Uploads every level and registers the group. `error` is set on failure.
    [[nodiscard]] MeshHandle createLod(
        const std::vector<MeshData>& levels,
        const std::vector<float>& maxDistances,
        LodBoundary boundary,
        std::string& error);
    void destroy(MeshHandle handle) noexcept;
    void destroyLod(MeshHandle handle) noexcept;

    [[nodiscard]] std::uint64_t vertexCount(MeshHandle handle) const noexcept;
    [[nodiscard]] bool lodValid(MeshHandle handle) const noexcept;
    [[nodiscard]] std::uint32_t lodLevelCount(MeshHandle handle) const noexcept;

    // Rewrites `items` in place: every LOD group id becomes the level id for
    // the current camera distance. Non-LOD items are left alone.
    void resolve(const CameraState& camera, std::vector<RenderItem>& items) noexcept;

    // Destroys every geometry buffer and clears all handles (safe on null device).
    void release(VkDevice device) noexcept;
    void clear() noexcept;

    [[nodiscard]] const std::unordered_map<std::uint64_t, MeshRenderer::Resource>& resources() const noexcept {
        return meshes_;
    }
    [[nodiscard]] std::size_t lodGroupCount() const noexcept { return lodGroups_.size(); }
    [[nodiscard]] std::uint64_t lodDrawTriangles() const noexcept { return lodDrawTriangles_; }
    [[nodiscard]] std::uint64_t lodFullTriangles() const noexcept { return lodFullTriangles_; }
    [[nodiscard]] std::uint64_t lodSwitchCount() const noexcept { return lodSwitchCount_; }

private:
    bool createBufferWithData(
        VkDeviceSize size,
        const void* data,
        VkBufferUsageFlags usage,
        VkBuffer& buffer,
        VkDeviceMemory& memory,
        std::string& error);
    [[nodiscard]] bool uploadLevels(const std::vector<MeshData>& levels, std::vector<std::uint64_t>& outIds, std::string& error);

    Device device_{};
    std::unordered_map<std::uint64_t, MeshRenderer::Resource> meshes_;
    std::unordered_map<std::uint64_t, LodGroup> lodGroups_;
    std::uint64_t nextId_ = 1;
    std::uint64_t lodDrawTriangles_ = 0;
    std::uint64_t lodFullTriangles_ = 0;
    std::uint64_t lodSwitchCount_ = 0;
};

} // namespace Aporia::graphics_internal
