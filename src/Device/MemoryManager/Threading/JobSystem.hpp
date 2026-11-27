#pragma once

#if defined(APORIA_EXPERIMENTAL)

#include <cstdint>
#include <functional>
#include <memory>

namespace Aporia::graphics_internal {

// EXPERIMENTAL worker pool owned by the main thread. The main thread can
// dispatch work that runs concurrently with its own recording, which is the
// basis of the multithreaded renderer.
class JobSystem {
public:
    // threadCount == 0 -> hardware_concurrency() - 1 (min 1) workers.
    explicit JobSystem(std::uint32_t threadCount = 0);
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    [[nodiscard]] std::uint32_t threadCount() const noexcept;
    [[nodiscard]] bool valid() const noexcept;

    // Queues a task and returns immediately.
    void submit(std::function<void()> task);

    // Blocks until every submitted task has finished.
    void waitAll();

    // Runs fn(index) for every index in [0, count) on the pool and blocks.
    void parallelFor(std::uint32_t count, const std::function<void(std::uint32_t)>& fn);

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace Aporia::graphics_internal

#endif // APORIA_EXPERIMENTAL
