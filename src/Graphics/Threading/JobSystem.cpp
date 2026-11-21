#include "JobSystem.hpp"

#if defined(APORIA_EXPERIMENTAL)

#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

namespace Aporia::graphics_internal {

struct JobSystem::State {
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex mutex;
    std::condition_variable work;
    std::condition_variable idle;
    std::uint32_t pending = 0;
    bool stopping = false;
};

JobSystem::JobSystem(std::uint32_t threadCount) : state_(std::make_unique<State>()) {
    std::uint32_t count = threadCount;
    if (count == 0) {
        const unsigned int hardware = std::thread::hardware_concurrency();
        count = hardware > 1 ? hardware - 1 : 1;
    }
    state_->workers.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        state_->workers.emplace_back([state = state_.get()]() {
            for (;;) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(state->mutex);
                    state->work.wait(lock, [state]() {
                        return state->stopping || !state->tasks.empty();
                    });
                    if (state->stopping && state->tasks.empty()) {
                        return;
                    }
                    task = std::move(state->tasks.front());
                    state->tasks.pop();
                }
                task();
            }
        });
    }
}

JobSystem::~JobSystem() {
    if (!state_) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->stopping = true;
    }
    state_->work.notify_all();
    for (std::thread& worker : state_->workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

std::uint32_t JobSystem::threadCount() const noexcept {
    return state_ ? static_cast<std::uint32_t>(state_->workers.size()) : 0;
}

bool JobSystem::valid() const noexcept {
    return state_ && !state_->workers.empty();
}

void JobSystem::submit(std::function<void()> task) {
    if (!state_) {
        task();
        return;
    }
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        ++state_->pending;
        state_->tasks.push([state = state_.get(), task = std::move(task)]() mutable {
            task();
            std::lock_guard<std::mutex> done(state->mutex);
            if (--state->pending == 0) {
                state->idle.notify_all();
            }
        });
    }
    state_->work.notify_one();
}

void JobSystem::waitAll() {
    if (!state_) {
        return;
    }
    std::unique_lock<std::mutex> lock(state_->mutex);
    state_->idle.wait(lock, [this]() { return state_->pending == 0 && state_->tasks.empty(); });
}

void JobSystem::parallelFor(std::uint32_t count, const std::function<void(std::uint32_t)>& fn) {
    if (count == 0) {
        return;
    }
    if (!state_ || state_->workers.empty()) {
        for (std::uint32_t index = 0; index < count; ++index) {
            fn(index);
        }
        return;
    }
    for (std::uint32_t index = 0; index < count; ++index) {
        submit([&fn, index]() { fn(index); });
    }
    waitAll();
}

} // namespace Aporia::graphics_internal

#endif // APORIA_EXPERIMENTAL
