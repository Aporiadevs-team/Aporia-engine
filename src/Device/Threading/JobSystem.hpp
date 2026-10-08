#pragma once

#if defined(APORIA_EXPERIMENTAL)

#include <cstdint>
#include <functional>
#include <memory>

namespace Aporia::graphics_internal {

// EXPERIMENTAL worker pool owned by the main thread. Step 1 of the
// multithreaded renderer: the main thread can dispatch parallel work before
// per-pass command buffers land.
class JobSystem {
public:
    // threadCount == 0 -> hardware_concurrency() - 1 (min 1) workers.
    explicit JobSystem(std::uint32_t threadCount = 0);
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    [[nodiscard]] std::uint32_t threadCount() const noexcept;
    [[nodiscard]] bool valid() const noexcept;

    // Runs fn(index) for every index in [0, count) on the pool. The calling
    // thread participates and blocks until every index completed.
    void parallelFor(std::uint32_t count, const std::function<void(std::uint32_t)>& fn);

    // Blocks until the queue is drained (no-op for an empty pool).
    void waitIdle();

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace Aporia::graphics_internal

#endif // APORIA_EXPERIMENTAL
