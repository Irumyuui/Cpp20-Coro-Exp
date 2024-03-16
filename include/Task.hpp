#pragma once

#include <condition_variable>
#include <coroutine>
#include <deque>
#include <exception>
#include <functional>
#include <mutex>
#include <utility>

#include "Result.hpp"
#include "TaskAwaiter.hpp"

#ifndef KARUS_CORO_TASK_HPP
#define KARUS_CORO_TASK_HPP

namespace karus::coro {

template <typename TResult>
class TaskPromise;

template <>
class TaskPromise<void>;

template <typename TResult>
class Task;

template <>
class Task<void>;

/*
 * Task with return value
 */
template <typename TResult>
class Task {
public:
    using promise_type = TaskPromise<TResult>;

public:
    explicit Task(std::coroutine_handle<promise_type> handle) noexcept;
    Task(Task &&task) noexcept;
    Task(const Task&) = delete;
    Task& operator=(Task &) = delete;
    ~Task() noexcept;

public:
    TResult get_result();

public:
    Task& then(std::function<void(TResult)> &&fn);
    Task& catching(std::function<void(std::exception&)> &&fn);
    Task& finally(std::function<void()> &&fn);

private:
    std::coroutine_handle<promise_type> handle_;
};

/*
 * Task without return value
 */
template <>
class Task<void> {
public:
    using promise_type = TaskPromise<void>;

public:
    explicit Task(std::coroutine_handle<promise_type> handle) noexcept;
    Task(Task &&task) noexcept;
    Task(const Task&) = delete;
    Task& operator=(Task &) = delete;
    ~Task() noexcept;

public:
    void get_result();

public:
    Task& then(std::function<void()> &&fn);
    Task& catching(std::function<void(std::exception&)> &&fn);
    Task& finally(std::function<void()> &&fn);

private:
    std::coroutine_handle<promise_type> handle_;
};

/*
 * Task<TResult> 's promise_type
 */
template <typename TResult>
class TaskPromise {
public:
    std::suspend_never initial_suspend();
    std::suspend_always final_suspend() noexcept;
    Task<TResult> get_return_object();
    void unhandled_exception();
    void return_value(TResult value);
    TResult get_result();
    template <typename TRet> TaskAwaiter<TRet> await_transform(Task<TRet> &&task);
    void on_completed(std::function<void(Result<TResult>)> &&fn);
    
private:
    void notify_callbacks();

private:
    std::deque<std::function<void(Result<TResult>)>> callbacks_;
    std::optional<Result<TResult>> result_;
    std::mutex mutex_;
    std::condition_variable cond_;
};

/*
 * Task<void> 's promise_type
 */
template <>
class TaskPromise<void> {
public:
    std::suspend_never initial_suspend();
    std::suspend_always final_suspend() noexcept;
    Task<void> get_return_object();
    void unhandled_exception();
    void return_void();
    void get_result();
    template <typename TRet> TaskAwaiter<TRet> await_transform(Task<TRet> &&task);
    void on_completed(std::function<void(Result<void>)> &&fn);
    
private:
    void notify_callbacks();

private:
    std::deque<std::function<void(Result<void>)>> callbacks_;
    std::optional<Result<void>> result_;
    std::mutex mutex_;
    std::condition_variable cond_;
};

/*
 * class Task<TResult>;
 */

template <typename TResult>
Task<TResult>::Task(std::coroutine_handle<promise_type> handle) noexcept
    : handle_(handle) {
}

template <typename TResult>
Task<TResult>::Task(Task &&task) noexcept
    : handle_(std::exchange(task.handle_, {})) {
}

template <typename TResult>
Task<TResult>::~Task() noexcept {
    try {
        if (handle_)
            handle_.destroy();
    } catch (...) {

    }
}

template <typename TResult>
TResult Task<TResult>::get_result() {
    return handle_.promise().get_result();
}

template <typename TResult>
Task<TResult>& Task<TResult>::then(std::function<void(TResult)> &&fn) {
    handle_.promise().on_completed([fn](auto result) {
        try {
            fn(result.get_or_throw());
        } catch (...) {

        }
    });
    return *this;
}

template <typename TResult>
Task<TResult>& Task<TResult>::catching(std::function<void(std::exception&)> &&fn) {
    handle_.promise().on_completed([fn](auto result) {
        try {
            result.get_or_throw();
        } catch (std::exception &e) {
            fn(e);
        }
    });
    return *this;
}

template <typename TResult>
Task<TResult>& Task<TResult>::finally(std::function<void()> &&fn) {
    handle_.promise().on_completed([fn]([[maybe_unused]] auto result) {
        fn();
    });
    return *this;
}

/*
 * class Task<void>;
 */

inline Task<void>::Task(std::coroutine_handle<promise_type> handle) noexcept
    : handle_(handle) {
}

inline Task<void>::Task(Task &&task) noexcept
: handle_(std::exchange(task.handle_, {})) {
}

inline Task<void>::~Task() noexcept {
    try {
        if (handle_)
            handle_.destroy();
    } catch (...) {

    }
}

inline void Task<void>::get_result() {
    handle_.promise().get_result();
}

inline Task<void>& Task<void>::then(std::function<void()> &&fn) {
    handle_.promise().on_completed([fn](auto result) {
        try {
            result.get_or_throw();
            fn();
        } catch (...) {

        }
    });
    return *this;
}

inline Task<void>& Task<void>::catching(std::function<void(std::exception&)> &&fn) {
    handle_.promise().on_completed([fn](auto result) {
        try {
            result.get_or_throw();
        } catch (std::exception &e) {
            fn(e);
        }
    });
    return *this;
}

inline Task<void>& Task<void>::finally(std::function<void()> &&fn) {
    handle_.promise().on_completed([fn]([[maybe_unused]] auto result) {
        fn();
    });
    return *this;
}

/*
 * class TaskPromise<TResult>
 */

template <typename TResult>
std::suspend_never TaskPromise<TResult>::initial_suspend() {
    return {};
}

template <typename TResult>
std::suspend_always TaskPromise<TResult>::final_suspend() noexcept {
    return {};
}

template <typename TResult>
Task<TResult> TaskPromise<TResult>::get_return_object() {
    return Task {
        std::coroutine_handle<TaskPromise>::from_promise(*this)
    };
}

template <typename TResult>
void TaskPromise<TResult>::unhandled_exception() {
    std::scoped_lock lock{mutex_};
    result_ = Result<TResult>(std::current_exception());
    cond_.notify_all();
    notify_callbacks();
}

template <typename TResult>
void TaskPromise<TResult>::return_value(TResult value) {
    std::scoped_lock lock{mutex_};
    result_ = Result<TResult>(std::move(value));
    cond_.notify_all();
    notify_callbacks();
}

template <typename TResult>
TResult TaskPromise<TResult>::get_result() {
    std::unique_lock lock{mutex_};
    if (!result_.has_value()) 
        cond_.wait(lock);
    return result_->get_or_throw();
}


// co_await args...
template <typename TResult>
    template <typename TRet>
TaskAwaiter<TRet> TaskPromise<TResult>::await_transform(Task<TRet> &&task) {
    return TaskAwaiter<TRet>(std::move(task));
}

template <typename TResult>
void TaskPromise<TResult>::on_completed(std::function<void(Result<TResult>)> &&fn) {
    std::unique_lock lock{mutex_};
    if (result_.has_value()) {
        auto value = result_.value();
        lock.unlock();
        fn(value);
    } else {
        callbacks_.emplace_back(std::move(fn));
    }
}

template <typename TResult>
void TaskPromise<TResult>::notify_callbacks() {
    for (auto value = result_.value(); auto &&callback : callbacks_)
        callback(value);
    callbacks_.clear();
}

/*
 * class TaskPromise<void>;
 */

inline std::suspend_never TaskPromise<void>::initial_suspend() {
    return {};
}

inline std::suspend_always TaskPromise<void>::final_suspend() noexcept {
    return {};
}

inline Task<void> TaskPromise<void>::get_return_object() {
    return Task {
        std::coroutine_handle<TaskPromise>::from_promise(*this)
    };
}

inline void TaskPromise<void>::unhandled_exception() {
    std::scoped_lock lock{mutex_};
    result_ = Result<void>(std::current_exception());
    cond_.notify_all();
    notify_callbacks();
}

inline void TaskPromise<void>::return_void() {
    std::scoped_lock lock{mutex_};
    result_ = Result<void>();
    cond_.notify_all();
    notify_callbacks();
}

inline void TaskPromise<void>::get_result() {
    std::unique_lock lock{mutex_};
    if (!result_.has_value()) 
        cond_.wait(lock);
    return result_->get_or_throw();
}

template <typename TRet>
TaskAwaiter<TRet> TaskPromise<void>::await_transform(Task<TRet> &&task) {
    return TaskAwaiter<TRet>(std::move(task));
}

inline void TaskPromise<void>::on_completed(std::function<void(Result<void>)> &&fn) {
    std::unique_lock lock{mutex_};
    if (result_.has_value()) {
        auto value = result_.value();
        lock.unlock();
        fn(value);
    } else {
        callbacks_.emplace_back(std::move(fn));
    }
}

inline void TaskPromise<void>::notify_callbacks() {
    for (auto value = result_.value(); auto &&callback : callbacks_)
        callback(value);
    callbacks_.clear();
}

} // namespace karus::coro

#endif // KARUS_CORO_TASK_HPP
