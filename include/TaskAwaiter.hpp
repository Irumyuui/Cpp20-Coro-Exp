#pragma once

#include <algorithm>
#include <atomic>
#include <concepts>
#include <condition_variable>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <list>
#include <mutex>
#include <utility>

#include "Executor.hpp"

#ifndef KARUS_CORO_TASK_AWAITER_HPP
#define KARUS_CORO_TASK_AWAITER_HPP

namespace karus::coro {

template <typename T>
concept IsAwaiter = requires(T object, std::coroutine_handle<> handle) {
    {object.await_ready()} -> std::same_as<bool>;
    object.await_suspend(handle);
    object.await_resume();
};

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

// dispatch task to different executor 
class DispatchAwaiter {
public:
    explicit DispatchAwaiter(IExecutor *executor) noexcept 
        : executor_(executor) {
    }

    bool await_ready() const { return false; }

    // the coroutine will be managed by the executor
    void await_suspend(std::coroutine_handle<> handle) const {
        executor_->execute([handle] {
            handle.resume();
        });
    }

    void await_resume() {}

private:
    IExecutor *executor_;
};

// coroutine sleep awaiter.
class SleepAwaiter {
public:
    explicit SleepAwaiter(IExecutor *executor, std::int64_t duration) noexcept
        : executor_(executor), duration_(duration) {
    }

    bool await_ready() const { return false; }

    // has a static scheduler menber.
    // the coroutine will be managed by the scheduler
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

template <typename TValue>
class WriteAwaiter;

template <typename TValue>
class ReadAwaiter;

template <typename TValue>
class Channel {
public:
    class ChannelClosedException : std::exception {
    public:
        const char* what() const noexcept override {
            return "CHannel is closed.";
        }
    };

public:
    explicit Channel(std::size_t capcity = 0)
        : buffer_capcity_(capcity) {
        is_active_.store(true, std::memory_order_relaxed);
    }

    Channel(Channel&&) noexcept = delete;
    Channel(const Channel&) = delete;
    Channel& operator=(Channel&&) noexcept = delete;
    Channel& operator=(const Channel&) = delete;

    ~Channel() noexcept {
        try {
            close();
        } catch (...) {
            
        }
    }

public:
    // check the channel is active.
    [[nodiscard]] bool is_active() const noexcept {
        return is_active_.load(std::memory_order_relaxed);
    }

    // check the cahnnel is closed.
    // if the channel is closed, then will throw ChannelClosedException.
    void check_closed() {
        if (!is_active_.load(std::memory_order_relaxed)) {
            throw ChannelClosedException();
        }
    }
    
    // close the channel.
    void close() {
        if (bool except = true; is_active_.compare_exchange_strong(except, false, std::memory_order_relaxed)) {
            clean_up();
        }
    }

    // create WriteAwaiter<TValue> from value.
    // the awaiter will bind this object.
    WriteAwaiter<TValue> write(TValue value) {
        check_closed();
        return WriteAwaiter<TValue>(this, std::move(value));
    }

    WriteAwaiter<TValue> operator << (TValue value) {
        return write(std::move(value));
    }

    // create ReadAwaiter<TValue>.
    // the awaiter will bind this object.
    ReadAwaiter<TValue> read() {
        check_closed();
        return ReadAwaiter<TValue>(this);
    }

    ReadAwaiter<TValue> operator >> (TValue &value) {
        auto reader = read();
        reader.set_value_ptr(&value);
        return reader;
    }

    // if any reader is waiting, then the writer will services for the reader.
    // or try to push value to buffer if the buffer is not full.
    // if the buffer is full, then put the writer in the waiting queue.
    void try_push_writer(WriteAwaiter<TValue> *writer) {
        // static_assert(writer != nullptr, "the writer cannot be nullptr.");
        
        std::unique_lock lock{channel_mutex_};
        check_closed();

        // if is there any reader in the queue, then call resume anyone reader.
        if (!await_reader_queue_.empty()) {
            auto reader = await_reader_queue_.front();
            await_reader_queue_.pop_front();
            lock.unlock();

            // send the writer's value to this reader.
            reader->resume(writer->get_value());
            writer->resume();

            return;
        }

        // if the buffer is not full, then just push the writer's value to the buffer.
        if (buffer_.size() < buffer_capcity_) {
            buffer_.push_back(writer->get_value());
            lock.unlock();

            // resume the writer.
            writer->resume();

            return;
        }

        // await this writer.
        await_writer_queue_.emplace_back(writer);
    }

    // if the buffer is not full, take an value from the buffer and try to replenish it from an await writer.
    // or if the buffer is empty, then will try to get the value from an await writer.
    // if is there are not await writer in the queue, then put the reader in the waiting queue. 
    void try_push_reader(ReadAwaiter<TValue> *reader) {
        // static_assert(reader != nullptr, "the reader cannot be nullptr.");

        std::unique_lock lock{channel_mutex_};

        // if the buffer is not empty, then take a value from buffer.
        if (!buffer_.empty()) {
            auto value = std::move(buffer_.front());
            buffer_.pop_front();

            // now need to resmue a writer.
            // if there is any writer in await status,
            // then get someone value and resume it.
            if (!await_writer_queue_.empty()) {
                auto writer = await_writer_queue_.front();
                await_writer_queue_.pop_front();
                buffer_.emplace_front(writer->get_value());
                lock.unlock();

                writer->resume();
            } else {
                lock.unlock();
            }

            reader->resume(value);

            return;
        }

        // if any of await writer, then resmue it.
        if (!await_writer_queue_.empty()) {
            auto writer = await_writer_queue_.front();
            await_writer_queue_.pop_front();
            buffer_.emplace_front(writer->get_value());
            lock.unlock();

            // resume
            reader->resume(writer->get_value());
            writer->resume();
            return;
        }

        await_reader_queue_.emplace_back(reader);
    }

    void remove_reader(ReadAwaiter<TValue> *reader) {
        std::scoped_lock lock{channel_mutex_};
        await_reader_queue_.remove(reader);
    }

    void remove_writer(WriteAwaiter<TValue> *writer) {
        std::scoped_lock loc{channel_mutex_};
        await_writer_queue_.remove(writer);
    }

private:
    // let all await coroutine resume.
    // then clear all task and value on buffer.
    void clean_up() {
        std::scoped_lock lock{channel_mutex_};
        for (auto &writer : await_writer_queue_) {
            writer->resume();
        }
        await_writer_queue_.clear();
        for (auto &reader : await_reader_queue_) {
            reader->resume();
        }
        await_reader_queue_.clear();
        
        buffer_.clear();
    }

private:
    std::size_t buffer_capcity_;
    std::deque<TValue> buffer_;
    std::list<WriteAwaiter<TValue>*> await_writer_queue_;
    std::list<ReadAwaiter<TValue>*> await_reader_queue_;
    std::atomic<bool> is_active_;
    std::mutex channel_mutex_;
    std::condition_variable channel_cond_;
};

// channel's writer
template <typename TValue>
class WriteAwaiter {
public:
    explicit WriteAwaiter(Channel<TValue> *channel, TValue value)
        : channel_(channel), value_(std::move(value)) {
    }

    WriteAwaiter(WriteAwaiter &&other) noexcept
        : channel_(std::exchange(other.channel_, nullptr)),
          executor_(std::exchange(other.executor_, nullptr)),
          value_(other.value_),
          call_handle_(other.call_handle_) {
    }

    ~WriteAwaiter() noexcept {
        try {
            if (channel_) {
                channel_->remove_writer(this);
            }
        } catch (...) {

        }
    }

public:
    constexpr bool await_ready() const noexcept {
        return false;
    }

    // suspend the coroutine.
    // take current handel, then add to channel's await writer queue.
    void await_suspend(std::coroutine_handle<> handle) {
        call_handle_ = handle;
        channel_->try_push_writer(this);
    }

    // if the writer want to be resumed, the channel must be actived.
    // if the channel is not active, then will throw an exception from channel object.
    void await_resume() {
        channel_->check_closed();
    }

    // channel will call this method.
    // if has executor, then let the executor exec.
    // or on this thread.
    void resume() {
        if (executor_) {
            executor_->execute([this] { call_handle_.resume(); });
        } else {
            call_handle_.resume();
        }
    }

    void set_executor(IExecutor *executor) {
        executor_ = executor;
    }

    TValue get_value() const {
        return value_;
    }

    TValue get_value() {
        return value_;
    }

private:
    Channel<TValue> *channel_;              // current object requires the channel.
    IExecutor *executor_{nullptr};
    TValue value_;
    std::coroutine_handle<> call_handle_;
};

// channel reader
template <typename TValue>
class ReadAwaiter {
public:
    explicit ReadAwaiter(Channel<TValue>* channel)
        : channel_(channel) {
    }

    ReadAwaiter(ReadAwaiter &&other) noexcept
        : channel_(std::exchange(other.channel_, nullptr)),
          executor_(std::exchange(other.executor_, nullptr)),
          value_(other.value_),
          value_ptr_(std::exchange(other.value_ptr_, nullptr)),
          call_handle_(other.call_handle_) {
    }

    ~ReadAwaiter() noexcept {
        try {
            if (channel_) {
                channel_->remove_reader(this);
            }
        } catch (...) {

        }
    }

public:
    constexpr bool await_ready() const noexcept {
        return false;
    }

    void await_suspend(std::coroutine_handle<> handle) {
        call_handle_ = handle;
        channel_->try_push_reader(this);
    }

    // check the channel has been closed.
    // if the channel is closed, then throw ChannelClosedException.
    // otherwith return read value.
    TValue await_resume() {
        channel_->check_closed();
        channel_ = nullptr;
        return value_;
    }
    
    // the method will be called by channel when it needed to read the value.
    void resume(TValue value) {
        value_ = value;
        if (value_ptr_) {
            *value_ptr_ = value;
        }
        resume();
    }

    void resume() {
        if (executor_) {
            executor_->execute([this] { call_handle_.resume(); });
        } else {
            call_handle_.resume();
        }
    }

    void set_value_ptr(TValue *ptr) noexcept {
        value_ptr_ = ptr;
    }

    void set_executor(IExecutor *executor) {
        executor_ = executor;
    }

    TValue get_value() const {
        return value_;
    }

    TValue get_value() {
        return value_;
    }

private:
    Channel<TValue> *channel_;              // current object requires the channel.
    IExecutor *executor_{nullptr};
    TValue value_;
    TValue* value_ptr_{nullptr};
    std::coroutine_handle<> call_handle_;
};

} // namespace karus::coro

#endif // KARUS_CORO_TASK_AWAITER_HPP
