// AporiaGraphicsMtTest — headless graphics benchmark comparing single-threaded
// vs multithreaded frame recording (the experimental shadow pass on a worker).
//
// Reports total time, average frame time (latency), p50/p95 frame time and FPS
// for both modes, then verifies both produce identical pixels and zero Vulkan
// validation messages.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "Aporia/Graphics/Graphics.hpp"
#include "Aporia/Graphics/MeshPrimitives.hpp"
#include "Aporia/Math/Matrix4.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
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

std::uint64_t parseNumber(const char* value, std::uint64_t fallback) {
    if (value == nullptr) {
        return fallback;
    }
    try {
        return std::stoull(value);
    } catch (...) {
        return fallback;
    }
}

double nowMs() {
    LARGE_INTEGER counter{};
    LARGE_INTEGER frequency{};
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    return static_cast<double>(counter.QuadPart) * 1000.0 / static_cast<double>(frequency.QuadPart);
}

struct Result {
    std::uint64_t frames = 0;
    double totalMs = 0.0;
    double avgMs = 0.0;
    double p50Ms = 0.0;
    double p95Ms = 0.0;
    double fps = 0.0;
};

Result run(Aporia::Graphics& graphics, const Aporia::CameraState& camera, const std::vector<Aporia::RenderItem>& items, std::uint64_t frames) {
    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(frames));
    const double start = nowMs();
    for (std::uint64_t frame = 0; frame < frames; ++frame) {
        const double before = nowMs();
        graphics.render(camera, items);
        samples.push_back(nowMs() - before);
    }
    const double total = nowMs() - start;

    std::sort(samples.begin(), samples.end());
    Result result;
    result.frames = frames;
    result.totalMs = total;
    result.avgMs = frames > 0 ? total / static_cast<double>(frames) : 0.0;
    if (!samples.empty()) {
        result.p50Ms = samples[samples.size() / 2];
        result.p95Ms = samples[static_cast<std::size_t>(static_cast<double>(samples.size() - 1) * 0.95)];
    }
    result.fps = total > 0.0 ? static_cast<double>(frames) * 1000.0 / total : 0.0;
    return result;
}

void print(const char* label, const Result& result) {
    std::cout << label
              << ": frames=" << result.frames
              << " total=" << result.totalMs << "ms"
              << " avg=" << result.avgMs << "ms"
              << " p50=" << result.p50Ms << "ms"
              << " p95=" << result.p95Ms << "ms"
              << " fps=" << result.fps << '\n';
}

} // namespace

int main(int argc, char** argv) {
    const std::uint64_t frames = argc > 1 ? parseNumber(argv[1], 600) : 600;
    const std::uint64_t warmupFrames = 64;
    constexpr int kItemCount = 512;

#if !defined(APORIA_EXPERIMENTAL)
    std::cout << "AporiaGraphicsMtTest: APORIA_EXPERIMENTAL off, nothing to compare\n";
    return 0;
#else
    Aporia::Graphics graphics;
    Aporia::GraphicsConfig config;
    config.enableValidation = true;
    if (!graphics.initialize({}, config)) {
        std::cerr << "initialization failed: " << graphics.lastError() << '\n';
        return 1;
    }

    const Aporia::MeshHandle box = graphics.createMesh(Aporia::apr::makeBoxMesh(0.5f, 0.5f, 0.5f));
    check(box.valid(), "box mesh must be created");

    std::vector<Aporia::RenderItem> items;
    items.reserve(kItemCount);
    for (int index = 0; index < kItemCount; ++index) {
        Aporia::Mat4 model = Aporia::Mat4::identity();
        model.m[3][0] = static_cast<float>(index % 16) - 8.0f;
        model.m[3][1] = static_cast<float>((index / 16) % 16) - 8.0f;
        model.m[3][2] = static_cast<float>(index % 5);
        Aporia::RenderItem item;
        item.meshId = box.value();
        std::memcpy(item.modelMatrix, &model, sizeof(model));
        items.push_back(item);
    }

    Aporia::CameraState camera;
    camera.position[0] = 0.0;
    camera.position[1] = -12.0;
    camera.position[2] = 8.0;

    // Warm up caches/pipelines in both modes.
    graphics.setMultithreadedRender(false);
    for (std::uint64_t frame = 0; frame < warmupFrames; ++frame) {
        graphics.render(camera, items);
    }
    std::vector<std::uint8_t> singleThreadPixels;
    const Result singleThread = run(graphics, camera, items, frames);
    (void)graphics.readbackPixels(singleThreadPixels);

    graphics.setMultithreadedRender(true);
    for (std::uint64_t frame = 0; frame < warmupFrames; ++frame) {
        graphics.render(camera, items);
    }
    std::vector<std::uint8_t> multiThreadPixels;
    const Result multiThread = run(graphics, camera, items, frames);
    (void)graphics.readbackPixels(multiThreadPixels);

    std::cout << "AporiaGraphicsMtTest items=" << kItemCount << " warmup=" << warmupFrames << '\n';
    print("  single-thread", singleThread);
    print("  multi-thread ", multiThread);
    const double speedup = multiThread.avgMs > 0.0 ? singleThread.avgMs / multiThread.avgMs : 0.0;
    std::cout << "  frame-time speedup (single/multi) = " << speedup << "x\n";

    const Aporia::GraphicsStats stats = graphics.stats();
    std::cout << "  validation messages = " << stats.validationMessageCount << '\n';
    check(stats.validationMessageCount == 0, "multithreaded recording must produce no validation errors");
    check(singleThread.frames == frames && multiThread.frames == frames, "both modes must run every frame");
    check(!singleThreadPixels.empty() && !multiThreadPixels.empty(), "readback must return pixels");
    check(singleThreadPixels.size() == multiThreadPixels.size(), "both modes must render the same target size");
    if (singleThreadPixels.size() == multiThreadPixels.size()) {
        check(std::memcmp(singleThreadPixels.data(), multiThreadPixels.data(), singleThreadPixels.size()) == 0,
              "single- and multi-threaded frames must match pixel-for-pixel");
    }

    graphics.shutdown();
    return failures == 0 ? 0 : 1;
#endif
}
