#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>

#ifndef KARUS_CORO_EXECUTOR_HPP
#define KARUS_CORO_EXECUTOR_HPP

namespace karus::coro {

class IExecutor {
public:
    virtual ~IExecutor() noexcept = default;
    virtual void execute(std::function<void()> &&fn) = 0;
};

template <typename T>
concept IsDerivedOfIExecutor = std::is_base_of_v<IExecutor, T> && !std::is_same_v<T, IExecutor>;

class NoopExecutor : public IExecutor {
public:
    void execute(std::function<void()> &&fn) override {
        fn();
    }
};

class NewThreadExecutor : public IExecutor {
public:
    void execute(std::function<void()> &&fn) override {
        std::jthread(fn).detach();
    }
};

class AsyncExecutor : public IExecutor {
public:
    void execute(std::function<void()> &&fn) override {
        [[maybe_unused]] auto result = std::async(fn);
    }
};

class LooperExecutor : public IExecutor {
public:
    LooperExecutor() {
        is_active_.store(true, std::memory_order_relaxed);
        // work_threads_ = std::jthread([this] {
        //     this->run_loop();
        // });
        work_threads_ = std::make_unique<std::jthread[]>(std::jthread::hardware_concurrency());
        for (std::size_t i = 0; i < std::jthread::hardware_concurrency(); i ++) {
            work_threads_[i] = std::jthread([this] {
                this->run_loop();
            });
        }
    }

    ~LooperExecutor() noexcept override {
        try {
            shutdown(false);
            join();
        } catch (...) {

        }
    }

public:
    void execute(std::function<void()> &&fn) override {
        std::unique_lock lock{mutex_};
        if (is_active_.load(std::memory_order_relaxed)) {
            tasks_.emplace_back(std::move(fn));
            lock.unlock();
            cond_.notify_one();
        }
    }

    void shutdown(bool wait_for_complete = true) {
        is_active_.store(false, std::memory_order_relaxed);
        if (!wait_for_complete) {
            std::scoped_lock lock{mutex_};
            tasks_.clear();
        }

        cond_.notify_all();
    }

    void join() {
        for (std::size_t i = 0; i < std::jthread::hardware_concurrency(); i ++) {
            if (work_threads_[i].joinable()) {
                work_threads_[i].join();
            }
        }
    }
    

private:
    void run_loop() {
        while (is_active_.load(std::memory_order_relaxed) || !tasks_.empty()) {
            std::unique_lock lock{mutex_};
            if(tasks_.empty()) {
                cond_.wait(lock);
                if (tasks_.empty())  {
                    continue;
                }
            }

            auto fn = std::move(tasks_.front());
            tasks_.pop_front();
            lock.unlock();
            fn();
        }
    }

private:
    std::mutex mutex_;
    std::condition_variable cond_;
    std::deque<std::function<void()>> tasks_;
    std::atomic<bool> is_active_;
    // std::jthread work_thread_;
    std::unique_ptr<std::jthread[]> work_threads_;
};

class SharedLooperExecutor : public IExecutor {
public:
    void execute(std::function<void()> &&fn) override {
        static LooperExecutor executor{};
        executor.execute(std::move(fn));
    }
};
}

#endif  // KARUS_CORO_EXECUTOR_HPP
