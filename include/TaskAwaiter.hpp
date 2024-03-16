#pragma once

#include <utility>
#ifndef KARUS_CORO_TASK_AWAITER_HPP
#define KARUS_CORO_TASK_AWAITER_HPP

#include <algorithm>
#include <coroutine>

namespace karus::coro {

template <typename TResult>
class Task;

template <typename TResult>
class TaskAwaiter {
public:
    explicit TaskAwaiter(Task<TResult> &&task) noexcept;
    TaskAwaiter(TaskAwaiter &&awaiter) noexcept;
    TaskAwaiter(const TaskAwaiter &) = delete;
    TaskAwaiter& operator=(const TaskAwaiter &other) = delete;

public:
    constexpr bool await_ready() const noexcept;
    void await_suspend(std::coroutine_handle<> handle) noexcept;
    TResult await_resume() noexcept;

private:
    Task<TResult> task_;
};

template <typename TResult>
TaskAwaiter<TResult>::TaskAwaiter(Task<TResult> &&task) noexcept
    : task_(std::move(task)) {
}


template <typename TResult>
TaskAwaiter<TResult>::TaskAwaiter(TaskAwaiter &&awaiter) noexcept
    : task_(std::exchange(awaiter.task_, {})) {
}

template <typename TResult>
constexpr bool TaskAwaiter<TResult>::await_ready() const noexcept {
    return false;
}

template <typename TResult>
void TaskAwaiter<TResult>::await_suspend(std::coroutine_handle<> handle) noexcept {
    task_.finally([handle] {
        handle.resume();
    });
}

template <typename TResult>
TResult TaskAwaiter<TResult>::await_resume() noexcept {
    return task_.get_result();
}

} // namespace karus::coro

#endif // KARUS_CORO_TASK_AWAITER_HPP
