#pragma once

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <coroutine>
#include <cstdint>
#include <functional>
#include <mutex>
#include <utility>

#include "Executor.hpp"

#ifndef KARUS_CORO_TASK_AWAITER_HPP
#define KARUS_CORO_TASK_AWAITER_HPP

namespace karus::coro {

template <typename TResult, IsDerivedOfIExecutor TExecutor>
class Task;

template <typename TResult, IsDerivedOfIExecutor TExecutor>
class TaskAwaiter {
public:
    explicit TaskAwaiter(Task<TResult, TExecutor> &&task, IExecutor *executor) noexcept;
    TaskAwaiter(TaskAwaiter &&awaiter) noexcept;
    TaskAwaiter(const TaskAwaiter &) = delete;
    TaskAwaiter& operator=(const TaskAwaiter &other) = delete;

public:
    constexpr bool await_ready() const noexcept;
    void await_suspend(std::coroutine_handle<> handle) noexcept;
    TResult await_resume() noexcept;

private:
    Task<TResult, TExecutor> task_;
    IExecutor *executor_;
};

template <typename TResult, IsDerivedOfIExecutor TExecutor>
TaskAwaiter<TResult, TExecutor>::TaskAwaiter(Task<TResult, TExecutor> &&task, IExecutor *executor) noexcept
    : task_(std::move(task)), executor_(executor) {
}


template <typename TResult, IsDerivedOfIExecutor TExecutor>
TaskAwaiter<TResult, TExecutor>::TaskAwaiter(TaskAwaiter &&awaiter) noexcept
    : task_(std::exchange(awaiter.task_, {})),
      executor_(std::exchange(awaiter.executor_, {})) {
}

template <typename TResult, IsDerivedOfIExecutor TExecutor>
constexpr bool TaskAwaiter<TResult, TExecutor>::await_ready() const noexcept {
    return false;
}

template <typename TResult, IsDerivedOfIExecutor TExecutor>
void TaskAwaiter<TResult, TExecutor>::await_suspend(std::coroutine_handle<> handle) noexcept {
    task_.finally([handle, this] {
        executor_->execute([handle] {
            handle.resume();
        });
    });
}

template <typename TResult, IsDerivedOfIExecutor TExecutor>
TResult TaskAwaiter<TResult, TExecutor>::await_resume() noexcept {
    return task_.get_result();
}

/**
 * dispatch task to different executor 
 * 
 */
class DispatchAwaiter {
public:
    explicit DispatchAwaiter(IExecutor *executor) noexcept 
        : executor_(executor) {
    }

    bool await_ready() const { return false; }

    void await_suspend(std::coroutine_handle<> handle) const {
        executor_->execute([handle] {
            handle.resume();
        });
    }

    void await_resume() {}

private:
    IExecutor *executor_;
};


struct SleepAwaiter {
public:
    explicit SleepAwaiter(IExecutor *executor, std::int64_t duration) noexcept
        : executor_(executor), duration_(duration) {
    }

    bool await_ready() const { return false; }

    void await_suspend(std::coroutine_handle<> handle) const {
        static Scheduler scheduler{};

        scheduler.execute([this, handle] {
            executor_->execute([handle] { handle.resume(); });
        }, duration_);
    }

    void await_resume() {}

private:
    IExecutor *executor_;
    std::int64_t duration_;
};

} // namespace karus::coro

#endif // KARUS_CORO_TASK_AWAITER_HPP
