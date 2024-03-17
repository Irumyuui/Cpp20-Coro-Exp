#pragma once

#include <chrono>
#include <condition_variable>
#include <coroutine>
#include <deque>
#include <exception>
#include <functional>
#include <mutex>
#include <utility>

#include "Executor.hpp"
#include "Result.hpp"
#include "TaskAwaiter.hpp"

#ifndef KARUS_CORO_TASK_HPP
#define KARUS_CORO_TASK_HPP

namespace karus::coro {

template <typename TResult, IsDerivedOfIExecutor TExecutor>
class TaskPromise;

template <IsDerivedOfIExecutor TExecutor>
class TaskPromise<void, TExecutor>;

template <typename TResult, IsDerivedOfIExecutor TExecutor>
class Task;

template <IsDerivedOfIExecutor TExecutor>
class Task<void, TExecutor>;

// a task which will contain a result.
template <typename TResult, IsDerivedOfIExecutor TExecutor = SharedLooperExecutor>
class Task {
public:
    using promise_type = TaskPromise<TResult, TExecutor>;

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

// a task without value.
template <IsDerivedOfIExecutor TExecutor>
class Task<void, TExecutor> {
public:
    using promise_type = TaskPromise<void, TExecutor>;

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

// Task<TResult> 's promise_type
template <typename TResult, IsDerivedOfIExecutor TExecutor>
class TaskPromise {
public:
    DispatchAwaiter initial_suspend();
    std::suspend_always final_suspend() noexcept;
    Task<TResult, TExecutor> get_return_object();
    void unhandled_exception();
    void return_value(TResult value);
    TResult get_result();
    void on_completed(std::function<void(Result<TResult>)> &&fn);

public:
    template <typename TRet, IsDerivedOfIExecutor TExec>
    TaskAwaiter<TRet, TExec> await_transform(Task<TRet, TExec> &&task);
    
    template <typename Rep, typename Period>
    SleepAwaiter await_transform(std::chrono::duration<Rep, Period> &&duration) {
        return SleepAwaiter(
            &executor_, 
            std::chrono::duration_cast<std::chrono::milliseconds>(duration).count()
        );
    }

    template <typename TValue>
    ReadAwaiter<TValue> await_transform(ReadAwaiter<TValue> reader) {
        reader.set_executor(&executor_);
        return reader;
    }

    template <typename TValue>
    WriteAwaiter<TValue> await_transform(WriteAwaiter<TValue> writer) {
        writer.set_executor(&executor_);
        return writer;
    }

private:
    void notify_callbacks();

private:
    std::deque<std::function<void(Result<TResult>)>> callbacks_;
    std::optional<Result<TResult>> result_;
    std::mutex mutex_;
    std::condition_variable cond_;
    TExecutor executor_;
};

// Task<TResult> 's promise_type
template <IsDerivedOfIExecutor TExecutor>
class TaskPromise<void, TExecutor> {
public:
    DispatchAwaiter initial_suspend();
    std::suspend_always final_suspend() noexcept;
    Task<void, TExecutor> get_return_object();
    void unhandled_exception();
    void return_void();
    void get_result();
    void on_completed(std::function<void(Result<void>)> &&fn);
    
public:
    template <typename TRet, IsDerivedOfIExecutor TExec>
    TaskAwaiter<TRet, TExec> await_transform(Task<TRet, TExec> &&task);

    template <typename Rep, typename Period>
    SleepAwaiter await_transform(std::chrono::duration<Rep, Period> &&duration) {
        return SleepAwaiter(&executor_, 
            std::chrono::duration_cast<std::chrono::milliseconds>(duration).count()
        );
    }

    template <typename TValue>
    ReadAwaiter<TValue> await_transform(ReadAwaiter<TValue> reader) {
        reader.set_executor(&executor_);
        return reader;
    }

    template <typename TValue>
    WriteAwaiter<TValue> await_transform(WriteAwaiter<TValue> writer) {
        writer.set_executor(&executor_);
        return writer;
    }
    
private:
    void notify_callbacks();

private:
    std::deque<std::function<void(Result<void>)>> callbacks_;
    std::optional<Result<void>> result_;
    std::mutex mutex_;
    std::condition_variable cond_;
    TExecutor executor_;
};

/*
 * class Task<TResult>;
 */

template <typename TResult, IsDerivedOfIExecutor TExecutor>
Task<TResult, TExecutor>::Task(std::coroutine_handle<promise_type> handle) noexcept
    : handle_(handle) {
}

template <typename TResult, IsDerivedOfIExecutor TExecutor>
Task<TResult, TExecutor>::Task(Task &&task) noexcept
    : handle_(std::exchange(task.handle_, {})) {
}

template <typename TResult, IsDerivedOfIExecutor TExecutor>
Task<TResult, TExecutor>::~Task() noexcept {
    try {
        if (handle_)
            handle_.destroy();
    } catch (...) {

    }
}

template <typename TResult, IsDerivedOfIExecutor TExecutor>
TResult Task<TResult, TExecutor>::get_result() {
    return handle_.promise().get_result();
}

// accepts a callback function that handles asynchronous return values
template <typename TResult, IsDerivedOfIExecutor TExecutor>
Task<TResult, TExecutor>& Task<TResult, TExecutor>::then(std::function<void(TResult)> &&fn) {
    handle_.promise().on_completed([fn](auto result) {
        try {
            fn(result.get_or_throw());
        } catch (...) {

        }
    });
    return *this;
}

// accepts a callback function that handles exceptions in asynchronous
template <typename TResult, IsDerivedOfIExecutor TExecutor>
Task<TResult, TExecutor>& Task<TResult, TExecutor>::catching(std::function<void(std::exception&)> &&fn) {
    handle_.promise().on_completed([fn](auto result) {
        try {
            result.get_or_throw();
        } catch (std::exception &e) {
            fn(e);
        }
    });
    return *this;
}

// accepts a callback function that just work.
template <typename TResult, IsDerivedOfIExecutor TExecutor>
Task<TResult, TExecutor>& Task<TResult, TExecutor>::finally(std::function<void()> &&fn) {
    handle_.promise().on_completed([fn]([[maybe_unused]] auto result) {
        fn();
    });
    return *this;
}

/*
 * class Task<void>;
 */

template <IsDerivedOfIExecutor TExecutor>
inline Task<void, TExecutor>::Task(std::coroutine_handle<promise_type> handle) noexcept
    : handle_(handle) {
}

template <IsDerivedOfIExecutor TExecutor>
inline Task<void, TExecutor>::Task(Task &&task) noexcept
: handle_(std::exchange(task.handle_, {})) {
}

template <IsDerivedOfIExecutor TExecutor>
inline Task<void, TExecutor>::~Task() noexcept {
    try {
        if (handle_)
            handle_.destroy();
    } catch (...) {

    }
}

template <IsDerivedOfIExecutor TExecutor>
inline void Task<void, TExecutor>::get_result() {
    handle_.promise().get_result();
}

template <IsDerivedOfIExecutor TExecutor>
inline Task<void, TExecutor>& Task<void, TExecutor>::then(std::function<void()> &&fn) {
    handle_.promise().on_completed([fn](auto result) {
        try {
            result.get_or_throw();
            fn();
        } catch (...) {

        }
    });
    return *this;
}

template <IsDerivedOfIExecutor TExecutor>
inline Task<void, TExecutor>& Task<void, TExecutor>::catching(std::function<void(std::exception&)> &&fn) {
    handle_.promise().on_completed([fn](auto result) {
        try {
            result.get_or_throw();
        } catch (std::exception &e) {
            fn(e);
        }
    });
    return *this;
}

template <IsDerivedOfIExecutor TExecutor>
inline Task<void, TExecutor>& Task<void, TExecutor>::finally(std::function<void()> &&fn) {
    handle_.promise().on_completed([fn]([[maybe_unused]] auto result) {
        fn();
    });
    return *this;
}

/*
 * class TaskPromise<TResult>
 */

template <typename TResult, IsDerivedOfIExecutor TExecutor>
DispatchAwaiter TaskPromise<TResult, TExecutor>::initial_suspend() {
    return DispatchAwaiter{ &executor_ };
}

template <typename TResult, IsDerivedOfIExecutor TExecutor>
std::suspend_always TaskPromise<TResult, TExecutor>::final_suspend() noexcept {
    return {};
}

template <typename TResult, IsDerivedOfIExecutor TExecutor>
Task<TResult, TExecutor> TaskPromise<TResult, TExecutor>::get_return_object() {
    return Task {
        std::coroutine_handle<TaskPromise>::from_promise(*this)
    };
}

template <typename TResult, IsDerivedOfIExecutor TExecutor>
void TaskPromise<TResult, TExecutor>::unhandled_exception() {
    std::scoped_lock lock{mutex_};
    result_ = Result<TResult>(std::current_exception());
    cond_.notify_all();
    notify_callbacks();
}

template <typename TResult, IsDerivedOfIExecutor TExecutor>
void TaskPromise<TResult, TExecutor>::return_value(TResult value) {
    std::scoped_lock lock{mutex_};
    result_ = Result<TResult>(std::move(value));
    cond_.notify_all();
    notify_callbacks();
}

template <typename TResult, IsDerivedOfIExecutor TExecutor>
TResult TaskPromise<TResult, TExecutor>::get_result() {
    std::unique_lock lock{mutex_};
    if (!result_.has_value()) 
        cond_.wait(lock);
    return result_->get_or_throw();
}


// co_await args...
template <typename TResult, IsDerivedOfIExecutor TExecutor>
    template <typename TRet, IsDerivedOfIExecutor TExec>
TaskAwaiter<TRet, TExec> TaskPromise<TResult, TExecutor>::await_transform(Task<TRet, TExec> &&task) {
    return TaskAwaiter<TRet, TExec>(std::move(task), &executor_);
}

template <typename TResult, IsDerivedOfIExecutor TExecutor>
void TaskPromise<TResult, TExecutor>::on_completed(std::function<void(Result<TResult>)> &&fn) {
    std::unique_lock lock{mutex_};
    if (result_.has_value()) {
        auto value = result_.value();
        lock.unlock();
        fn(value);
    } else {
        callbacks_.emplace_back(std::move(fn));
    }
}

template <typename TResult, IsDerivedOfIExecutor TExecutor>
void TaskPromise<TResult, TExecutor>::notify_callbacks() {
    for (auto value = result_.value(); auto &&callback : callbacks_)
        callback(value);
    callbacks_.clear();
}

/*
 * class TaskPromise<void>;
 */
template <IsDerivedOfIExecutor TExecutor>
inline DispatchAwaiter TaskPromise<void, TExecutor>::initial_suspend() {
    return DispatchAwaiter{ &executor_ };
}

template <IsDerivedOfIExecutor TExecutor>
inline std::suspend_always TaskPromise<void, TExecutor>::final_suspend() noexcept {
    return {};
}

template <IsDerivedOfIExecutor TExecutor>
inline Task<void, TExecutor> TaskPromise<void, TExecutor>::get_return_object() {
    return Task {
        std::coroutine_handle<TaskPromise>::from_promise(*this)
    };
}

template <IsDerivedOfIExecutor TExecutor>
inline void TaskPromise<void, TExecutor>::unhandled_exception() {
    std::scoped_lock lock{mutex_};
    result_ = Result<void>(std::current_exception());
    cond_.notify_all();
    notify_callbacks();
}

template <IsDerivedOfIExecutor TExecutor>
inline void TaskPromise<void, TExecutor>::return_void() {
    std::scoped_lock lock{mutex_};
    result_ = Result<void>();
    cond_.notify_all();
    notify_callbacks();
}

template <IsDerivedOfIExecutor TExecutor>
inline void TaskPromise<void, TExecutor>::get_result() {
    std::unique_lock lock{mutex_};
    if (!result_.has_value()) 
        cond_.wait(lock);
    return result_->get_or_throw();
}


template <IsDerivedOfIExecutor TExecutor>
    template <typename TRet, IsDerivedOfIExecutor TExec>
TaskAwaiter<TRet, TExec> TaskPromise<void, TExecutor>::await_transform(Task<TRet, TExec> &&task) {
    return TaskAwaiter<TRet, TExec>(std::move(task), &executor_);
}

template <IsDerivedOfIExecutor TExecutor>
inline void TaskPromise<void, TExecutor>::on_completed(std::function<void(Result<void>)> &&fn) {
    std::unique_lock lock{mutex_};
    if (result_.has_value()) {
        auto value = result_.value();
        lock.unlock();
        fn(value);
    } else {
        callbacks_.emplace_back(std::move(fn));
    }
}

template <IsDerivedOfIExecutor TExecutor>
inline void TaskPromise<void, TExecutor>::notify_callbacks() {
    for (auto value = result_.value(); auto &&callback : callbacks_)
        callback(value);
    callbacks_.clear();
}

} // namespace karus::coro

#endif // KARUS_CORO_TASK_HPP
