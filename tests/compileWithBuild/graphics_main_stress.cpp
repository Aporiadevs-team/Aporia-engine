#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>

#include "Aporia/Graphics/Graphics.hpp"

#include <cstdint>
#include <iostream>
#include <string>

namespace {

struct MemorySnapshot {
    std::uint64_t workingSet = 0;
    std::uint64_t privateBytes = 0;
};

MemorySnapshot readMemory() {
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    MemorySnapshot result;
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters))) {
        result.workingSet = counters.WorkingSetSize;
        result.privateBytes = counters.PrivateUsage;
    }
    return result;
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

}

int main(int argc, char** argv) {
    const std::uint64_t frameLimit = argc > 1 ? parseNumber(argv[1], 10000) : 10000;
    const std::uint64_t cycleLimit = argc > 2 ? parseNumber(argv[2], 8) : 8;
    const std::uint64_t warmupFrames = 256;
    constexpr std::uint64_t memoryGrowthLimit = 64ull * 1024ull * 1024ull;

    Aporia::Graphics graphics;
    Aporia::GraphicsConfig config;
    config.enableValidation = false;

    if (!graphics.initialize({}, config)) {
        std::cerr << "initialization failed: " << graphics.lastError() << '\n';
        return 1;
    }

    for (std::uint64_t frame = 0; frame < warmupFrames; ++frame) {
        if (!graphics.renderHeadlessFrame()) {
            std::cerr << "warmup frame failed: " << graphics.lastError() << '\n';
            graphics.shutdown();
            return 1;
        }
    }

    const MemorySnapshot before = readMemory();
    for (std::uint64_t frame = 0; frame < frameLimit; ++frame) {
        if (!graphics.renderHeadlessFrame()) {
            std::cerr << "stress frame " << frame << " failed: " << graphics.lastError() << '\n';
            graphics.shutdown();
            return 1;
        }
    }
    const MemorySnapshot after = readMemory();
    if (after.privateBytes > before.privateBytes + memoryGrowthLimit) {
        std::cerr << "private memory grew by more than 64 MiB during frame loop\n";
        graphics.shutdown();
        return 1;
    }

    const std::uint64_t renderedFrames = graphics.stats().frameCount;
    graphics.shutdown();
    if (graphics.initialized() || renderedFrames != warmupFrames + frameLimit) {
        std::cerr << "graphics shutdown or frame accounting failed: initialized="
                  << graphics.initialized() << " frames=" << renderedFrames
                  << " expected=" << warmupFrames + frameLimit << '\n';
        return 1;
    }

    const MemorySnapshot cycleBaseline = readMemory();
    for (std::uint64_t cycle = 0; cycle < cycleLimit; ++cycle) {
        if (!graphics.initialize({}, config)) {
            std::cerr << "cycle " << cycle << " initialization failed: " << graphics.lastError() << '\n';
            return 1;
        }
        for (std::uint64_t frame = 0; frame < 32; ++frame) {
            if (!graphics.renderHeadlessFrame()) {
                std::cerr << "cycle " << cycle << " frame failed: " << graphics.lastError() << '\n';
                graphics.shutdown();
                return 1;
            }
        }
        graphics.shutdown();
        if (graphics.initialized()) {
            std::cerr << "cycle " << cycle << " shutdown failed\n";
            return 1;
        }
    }

    const MemorySnapshot cycleAfter = readMemory();
    if (cycleAfter.privateBytes > cycleBaseline.privateBytes + memoryGrowthLimit) {
        std::cerr << "private memory grew by more than 64 MiB across init/shutdown cycles\n";
        return 1;
    }

    std::cout << "Aporia graphics stress passed: " << frameLimit << " frames, "
              << cycleLimit << " init/shutdown cycles, private bytes "
              << before.privateBytes << " -> " << after.privateBytes << '\n';
    return 0;
}
