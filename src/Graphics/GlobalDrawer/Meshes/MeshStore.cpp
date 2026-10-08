#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "MeshStore.hpp"

#include "Device/MemoryManager/MemoryManager.hpp"
#include "Graphics/GlobalDrawer/Support/Support.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace Aporia::graphics_internal {

bool MeshStore::createBufferWithData(
    VkDeviceSize size,
    const void* data,
    VkBufferUsageFlags usage,
    VkBuffer& buffer,
    VkDeviceMemory& memory,
    std::string& error) {
    MemoryManager::Context context;
    context.device = device_.device;
    context.memoryProperties = device_.memoryProperties;
    context.commandBuffer = device_.commandBuffer;
    context.queue = device_.queue;
    if (!MemoryManager::uploadBuffer(context, size, data, usage, buffer, memory)) {
        error = "Buffer upload failed";
        return false;
    }
    return true;
}

MeshHandle MeshStore::create(const MeshData& data, std::string& error) {
    if (data.vertices.empty() || data.indices.empty()) {
        error = "Mesh data must contain vertices and indices";
        return invalidMeshHandle;
    }
    for (std::uint32_t index : data.indices) {
        if (index >= data.vertices.size()) {
            error = "Mesh index references a vertex outside the vertex buffer";
            return invalidMeshHandle;
        }
    }
    for (const MeshVertex& vertex : data.vertices) {
        for (float component : vertex.position) {
            if (!std::isfinite(component)) {
                error = "Mesh vertex position contains a non-finite value";
                return invalidMeshHandle;
            }
        }
    }
    if (data.vertices.size() > std::numeric_limits<std::uint32_t>::max() ||
        data.indices.size() > std::numeric_limits<std::uint32_t>::max()) {
        error = "Mesh data exceeds 32-bit index limits";
        return invalidMeshHandle;
    }

    MeshRenderer::Resource mesh;
    const VkDeviceSize vertexSize = data.vertices.size() * sizeof(MeshVertex);
    const VkDeviceSize indexSize = data.indices.size() * sizeof(std::uint32_t);
    if (!createBufferWithData(
            vertexSize,
            data.vertices.data(),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            mesh.vertexBuffer,
            mesh.vertexMemory,
            error)) {
        MeshRenderer::destroyResource(device_.device, mesh);
        return invalidMeshHandle;
    }
    if (!createBufferWithData(
            indexSize,
            data.indices.data(),
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            mesh.indexBuffer,
            mesh.indexMemory,
            error)) {
        MeshRenderer::destroyResource(device_.device, mesh);
        return invalidMeshHandle;
    }
    mesh.vertexCount = static_cast<std::uint32_t>(data.vertices.size());
    mesh.indexCount = static_cast<std::uint32_t>(data.indices.size());

    const std::uint64_t id = nextId_++;
    try {
        const auto result = meshes_.emplace(id, std::move(mesh));
        if (!result.second) {
            error = "Mesh handle allocation failed";
            return invalidMeshHandle;
        }
    } catch (...) {
        error = "Mesh resource allocation failed";
        return invalidMeshHandle;
    }
    engineLog("mesh created: id=%llu vertices=%zu indices=%zu",
              static_cast<unsigned long long>(id), data.vertices.size(), data.indices.size());
    return MeshHandle{id};
}

bool MeshStore::uploadLevels(const std::vector<MeshData>& levels, std::vector<std::uint64_t>& outIds, std::string& error) {
    for (const MeshData& level : levels) {
        const MeshHandle levelHandle = create(level, error);
        if (!levelHandle.valid()) {
            for (std::uint64_t created : outIds) {
                destroy(MeshHandle{created});
            }
            outIds.clear();
            return false;
        }
        outIds.push_back(levelHandle.value());
    }
    return true;
}

MeshHandle MeshStore::createLod(
    const std::vector<MeshData>& levels,
    const std::vector<float>& maxDistances,
    LodBoundary boundary,
    std::string& error) {
    if (levels.size() < 2) {
        error = "LOD group needs at least two levels";
        return invalidMeshHandle;
    }
    if (maxDistances.size() + 1 != levels.size()) {
        error = "LOD group needs one range per level except the last";
        return invalidMeshHandle;
    }
    for (std::size_t index = 0; index < maxDistances.size(); ++index) {
        if (!std::isfinite(maxDistances[index]) || maxDistances[index] < 0.0f) {
            error = "LOD range must be finite and non-negative";
            return invalidMeshHandle;
        }
        if (index > 0 && maxDistances[index] <= maxDistances[index - 1]) {
            error = "LOD ranges must be strictly ascending";
            return invalidMeshHandle;
        }
    }

    // Bounds come from level 0 so the boundary is the same for every level.
    LodGroup group;
    for (int axis = 0; axis < 3; ++axis) {
        group.center[axis] = 0.0f;
        group.halfExtents[axis] = 0.0f;
    }
    float minimum[3] = {
        std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::infinity()};
    float maximum[3] = {
        -std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity()};
    for (const MeshVertex& vertex : levels.front().vertices) {
        for (int axis = 0; axis < 3; ++axis) {
            minimum[axis] = std::min(minimum[axis], vertex.position[axis]);
            maximum[axis] = std::max(maximum[axis], vertex.position[axis]);
        }
    }
    for (int axis = 0; axis < 3; ++axis) {
        group.center[axis] = (minimum[axis] + maximum[axis]) * 0.5f;
        group.halfExtents[axis] = std::max((maximum[axis] - minimum[axis]) * 0.5f, 1.0e-3f);
    }
    group.boundingRadius = std::sqrt(
        group.halfExtents[0] * group.halfExtents[0] +
        group.halfExtents[1] * group.halfExtents[1] +
        group.halfExtents[2] * group.halfExtents[2]);
    group.boundary = boundary;
    group.maxDistances = maxDistances;

    if (!uploadLevels(levels, group.meshes, error)) {
        return invalidMeshHandle;
    }

    const std::uint64_t id = nextId_++;
    try {
        const auto result = lodGroups_.emplace(id, std::move(group));
        if (!result.second) {
            error = "LOD group handle allocation failed";
            return invalidMeshHandle;
        }
    } catch (...) {
        error = "LOD group allocation failed";
        return invalidMeshHandle;
    }
    engineLog("lod group created: id=%llu levels=%zu", static_cast<unsigned long long>(id), levels.size());
    return MeshHandle{id};
}

void MeshStore::destroy(MeshHandle handle) noexcept {
    if (!handle.valid()) {
        return;
    }
    if (lodGroups_.find(handle.value()) != lodGroups_.end()) {
        destroyLod(handle);
        return;
    }
    const auto meshIt = meshes_.find(handle.value());
    if (meshIt == meshes_.end()) {
        return;
    }
    if (device_.device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_.device);
    }
    MeshRenderer::destroyResource(device_.device, meshIt->second);
    meshes_.erase(meshIt);
}

void MeshStore::destroyLod(MeshHandle handle) noexcept {
    if (!handle.valid()) {
        return;
    }
    const auto groupIt = lodGroups_.find(handle.value());
    if (groupIt == lodGroups_.end()) {
        return;
    }
    const std::vector<std::uint64_t> levels = groupIt->second.meshes;
    lodGroups_.erase(groupIt);
    for (std::uint64_t level : levels) {
        destroy(MeshHandle{level});
    }
}

std::uint64_t MeshStore::vertexCount(MeshHandle handle) const noexcept {
    if (!handle.valid()) {
        return 0;
    }
    const auto groupIt = lodGroups_.find(handle.value());
    if (groupIt != lodGroups_.end()) {
        if (groupIt->second.meshes.empty()) {
            return 0;
        }
        const auto meshIt = meshes_.find(groupIt->second.meshes.front());
        return meshIt == meshes_.end() ? 0 : meshIt->second.vertexCount;
    }
    const auto meshIt = meshes_.find(handle.value());
    return meshIt == meshes_.end() ? 0 : meshIt->second.vertexCount;
}

bool MeshStore::lodValid(MeshHandle handle) const noexcept {
    return handle.valid() && lodGroups_.count(handle.value()) != 0;
}

std::uint32_t MeshStore::lodLevelCount(MeshHandle handle) const noexcept {
    if (!handle.valid()) {
        return 0;
    }
    const auto groupIt = lodGroups_.find(handle.value());
    return groupIt == lodGroups_.end()
        ? 0
        : static_cast<std::uint32_t>(groupIt->second.meshes.size());
}

void MeshStore::resolve(const CameraState& camera, std::vector<RenderItem>& items) noexcept {
    lodDrawTriangles_ = 0;
    lodFullTriangles_ = 0;
    if (lodGroups_.empty()) {
        return;
    }
    const double cameraX = camera.position[0];
    const double cameraY = camera.position[1];
    const double cameraZ = camera.position[2];
    for (RenderItem& item : items) {
        const auto groupIt = lodGroups_.find(item.meshId);
        if (groupIt == lodGroups_.end()) {
            continue;
        }
        LodGroup& group = groupIt->second;
        const float* m = item.modelMatrix;
        const std::array<double, 3> axisX{m[0], m[1], m[2]};
        const std::array<double, 3> axisY{m[4], m[5], m[6]};
        const std::array<double, 3> axisZ{m[8], m[9], m[10]};
        const auto lengthOf = [](const std::array<double, 3>& v) {
            return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        };
        const double scaleX = lengthOf(axisX);
        const double scaleY = lengthOf(axisY);
        const double scaleZ = lengthOf(axisZ);
        const double centerX = m[12] + axisX[0] * group.center[0] + axisY[0] * group.center[1] + axisZ[0] * group.center[2];
        const double centerY = m[13] + axisX[1] * group.center[0] + axisY[1] * group.center[1] + axisZ[1] * group.center[2];
        const double centerZ = m[14] + axisX[2] * group.center[0] + axisY[2] * group.center[1] + axisZ[2] * group.center[2];
        const double deltaX = cameraX - centerX;
        const double deltaY = cameraY - centerY;
        const double deltaZ = cameraZ - centerZ;

        double distance = 0.0;
        if (group.boundary == LodBoundary::Box && scaleX > 1.0e-6 && scaleY > 1.0e-6 && scaleZ > 1.0e-6) {
            const auto unit = [&](const std::array<double, 3>& v, double length) {
                return std::array<double, 3>{v[0] / length, v[1] / length, v[2] / length};
            };
            const std::array<double, 3> ux = unit(axisX, scaleX);
            const std::array<double, 3> uy = unit(axisY, scaleY);
            const std::array<double, 3> uz = unit(axisZ, scaleZ);
            const double extentX = scaleX * group.halfExtents[0];
            const double extentY = scaleY * group.halfExtents[1];
            const double extentZ = scaleZ * group.halfExtents[2];
            const auto project = [&](const std::array<double, 3>& u) {
                return deltaX * u[0] + deltaY * u[1] + deltaZ * u[2];
            };
            const double px = std::clamp(project(ux), -extentX, extentX);
            const double py = std::clamp(project(uy), -extentY, extentY);
            const double pz = std::clamp(project(uz), -extentZ, extentZ);
            const double closestX = ux[0] * px + uy[0] * py + uz[0] * pz;
            const double closestY = ux[1] * px + uy[1] * py + uz[1] * pz;
            const double closestZ = ux[2] * px + uy[2] * py + uz[2] * pz;
            const double ox = deltaX - closestX;
            const double oy = deltaY - closestY;
            const double oz = deltaZ - closestZ;
            distance = std::sqrt(ox * ox + oy * oy + oz * oz);
        } else {
            const double radius = group.boundingRadius * std::max({scaleX, scaleY, scaleZ});
            const double centerDistance = std::sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
            distance = std::max(0.0, centerDistance - radius);
        }

        std::uint32_t level = static_cast<std::uint32_t>(group.meshes.size() - 1);
        for (std::uint32_t candidate = 0; candidate + 1 < group.meshes.size(); ++candidate) {
            if (distance <= group.maxDistances[candidate]) {
                level = candidate;
                break;
            }
        }
        if (level != group.lastLevel) {
            ++lodSwitchCount_;
            group.lastLevel = level;
        }

        const std::uint64_t fullId = group.meshes.front();
        const std::uint64_t drawId = group.meshes[level];
        item.meshId = drawId;
        const auto drawIt = meshes_.find(drawId);
        const auto fullIt = meshes_.find(fullId);
        lodDrawTriangles_ += drawIt != meshes_.end() ? drawIt->second.indexCount / 3u : 0u;
        lodFullTriangles_ += fullIt != meshes_.end() ? fullIt->second.indexCount / 3u : 0u;
    }
}

void MeshStore::clear() noexcept {
    meshes_.clear();
    lodGroups_.clear();
    nextId_ = 1;
    lodDrawTriangles_ = 0;
    lodFullTriangles_ = 0;
    lodSwitchCount_ = 0;
}

void MeshStore::release(VkDevice device) noexcept {
    for (auto& entry : meshes_) {
        MeshRenderer::destroyResource(device, entry.second);
    }
    meshes_.clear();
    lodGroups_.clear();
    nextId_ = 1;
    lodDrawTriangles_ = 0;
    lodFullTriangles_ = 0;
    lodSwitchCount_ = 0;
}

} // namespace Aporia::graphics_internal
