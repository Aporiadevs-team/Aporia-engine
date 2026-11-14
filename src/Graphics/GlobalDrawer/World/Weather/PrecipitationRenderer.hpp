#pragma once

#if defined(_WIN32) && !defined(VK_USE_PLATFORM_WIN32_KHR)
#define VK_USE_PLATFORM_WIN32_KHR
#endif

#include "Aporia/Graphics/Weather.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Aporia::graphics_internal {

#if defined(APORIA_SEASONS)
// Camera-following precipitation batch driven by WeatherState.
//
// One instanced draw covers every particle: four billboard corners are
// generated from gl_VertexIndex and each instance supplies its world position,
// fall velocity and size. The CPU integrates positions into a single mapped
// vertex buffer per frame slot (no staging copy), so the whole system costs one
// buffer upload and one draw call per frame. The box wraps around the camera;
// particles fade toward its far edge and respect scene depth through the
// depth-test (no depth write). Rain uses velocity-slanted streaks, snow soft
// round flakes, sleet a blend.
class PrecipitationRenderer {
public:
    static constexpr std::uint32_t kSlots = 2;
    static constexpr float kBoxRadius = 26.0f;
    static constexpr float kBoxAbove = 18.0f;
    static constexpr float kBoxBelow = 4.0f;

    bool initialize(
        VkDevice device,
        const VkPhysicalDeviceMemoryProperties& memoryProperties,
        VkRenderPass renderPass,
        VkRenderPass hdrRenderPass,
        const std::string& shaderDirectory,
        std::string& error);
    void shutdown(VkDevice device) noexcept;

    [[nodiscard]] bool valid() const noexcept { return pipeline_ != VK_NULL_HANDLE; }
    [[nodiscard]] std::uint32_t particleCount() const noexcept { return activeCount_; }

    // Integrates and uploads one frame of particles for the given frame slot.
    void update(
        std::uint32_t slot,
        const WeatherState& weather,
        const double cameraPosition[3],
        float deltaSeconds) noexcept;

    // Records the single instanced draw. Must run inside the main render pass.
    void record(
        VkCommandBuffer cmd,
        bool hdr,
        std::uint32_t slot,
        const float viewMatrix[16],
        const float viewProj[16],
        const double cameraPosition[3],
        const float fogColor[3],
        float fogDensity) const noexcept;

private:
    struct Particle {
        float position[3] = {};
        float velocity[3] = {};
        float width = 0.0f;
        float length = 0.0f;
        float alpha = 1.0f;
        float phase = 0.0f;
    };

    // Matches the instance attributes in shaders/world/precipitation.vert.
    struct Instance {
        float position[4]; // xyz world, w kind (0 rain, 1 snow, 2 sleet)
        float velocity[4]; // xyz m/s, w half-width
        float size[4];     // x half-length, y alpha, z phase, w unused
    };

    // Matches the push constant block in shaders/world/precipitation.vert.
    struct PushConstants {
        float viewProj[16];
        float cameraRight[4];
        float cameraUp[4];
        float cameraPosition[4]; // xyz camera, w box radius
        float fog[4];            // rgb fog colour, a density
    };

    static_assert(sizeof(Instance) == 48, "Precipitation instance layout must match precipitation.vert");
    static_assert(sizeof(PushConstants) == 128, "Push constants must fit the 128-byte Vulkan minimum");

    bool createPipeline(
        VkDevice device,
        VkRenderPass renderPass,
        VkShaderModule vertex,
        VkShaderModule fragment,
        VkPipeline& out,
        std::string& error);
    static float randomFloat(std::uint32_t& state) noexcept;
    void respawn(std::uint32_t first, std::uint32_t last, const double camera[3]);

    VkDevice device_ = VK_NULL_HANDLE;
    VkShaderModule vertexShader_ = VK_NULL_HANDLE;
    VkShaderModule fragmentShader_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipeline hdrPipeline_ = VK_NULL_HANDLE;
    VkBuffer buffers_[kSlots] = {};
    VkDeviceMemory memory_[kSlots] = {};
    void* mapped_[kSlots] = {};

    std::vector<Particle> particles_;
    std::uint32_t activeCount_ = 0;
    PrecipitationKind kind_ = PrecipitationKind::None;
    std::uint32_t randomState_ = 0x9e3779b9u;
    float elapsed_ = 0.0f;
    double lastCamera_[3] = {0.0, 0.0, 0.0};
    bool cameraKnown_ = false;
};
#endif

} // namespace Aporia::graphics_internal
