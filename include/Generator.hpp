#pragma once

#include <coroutine>
#include <cstddef>
#include <exception>
#include <functional>
#include <iterator>
#include <memory>
#include <type_traits>
#include <utility>

#ifndef KARSU_CORO_GENERATOR_HPP
#define KARSU_CORO_GENERATOR_HPP

namespace karus::coro {

template <typename Ty>
class Generator {
public:
    using TValue = std::remove_reference_t<Ty>;
    using TRef = std::conditional_t<std::is_reference_v<Ty>, Ty, Ty&>;
    using TPtr = TValue*;
    
public:
    class promise_type {        
    public:
        promise_type() = default;

    public:
        Generator get_return_object() noexcept {
            return Generator { std::coroutine_handle<promise_type>::from_promise(*this) };
        }

        constexpr std::suspend_always initial_suspend() const noexcept {
            return {};
        }

        constexpr std::suspend_always final_suspend() const noexcept {
            return {};
        }

        template <typename T = Ty>
            requires (!std::is_rvalue_reference_v<T>)
        std::suspend_always yield_value(std::remove_reference_t<T>& val) noexcept {
            value_ptr_ = std::addressof(val);
            return {};
        }

        std::suspend_always yield_value(std::remove_reference_t<Ty>&& value) noexcept {
            value_ptr_ = std::addressof(value);
            return {};
        }

        void await_transform() = delete;

        constexpr void return_void() const noexcept {};

        void unhandled_exception() {
            except_ = std::current_exception();
        }

        void try_throw() {
            if (except_) {
                std::rethrow_exception(except_);
            }
        }

        TRef get_value() const noexcept {
            return static_cast<TRef>(*value_ptr_);
        }

    private:
        TPtr value_ptr_{nullptr};
        std::exception_ptr except_;
    };

    // generator's iterator
    class Iterator {
    public:        
        Iterator(Iterator &&other) noexcept 
            : handle_(std::exchange(other.handle_, {})) {}

        Iterator& operator=(Iterator &&other) noexcept {
            handle_ = std::exchange(other.handle_, {});
            return *this;
        }

        TValue& operator*() const noexcept {
            return handle_.promise().get_value();
        }

        Iterator& operator++() {
            handle_.resume();
            if (handle_.done()) {
                handle_.promise().try_throw();
            }
            return *this;
        }
        
        void operator++(int) {
            [[maybe_unused]] auto result = ++ *this;
        }

        friend constexpr bool operator==(const Iterator &it, std::default_sentinel_t) noexcept {
            return !it.handle_ || it.handle_.done();
        }

        friend constexpr bool operator==(std::default_sentinel_t, const Iterator &it) noexcept {
            return it == std::default_sentinel;
        }

        friend constexpr bool operator!=(const Iterator &it, std::default_sentinel_t) noexcept {
            return !(it == std::default_sentinel);
        }

        friend constexpr bool operator!=(std::default_sentinel_t, const Iterator &it) noexcept {
            return !(it == std::default_sentinel);
        }

    private:
        template <typename> friend class Generator;
        
        explicit Iterator(std::coroutine_handle<promise_type> handle_) noexcept
            : handle_(handle_) {}

    private:
        std::coroutine_handle<promise_type> handle_{nullptr};
    };

public:
    explicit Generator(std::coroutine_handle<promise_type> handle) noexcept
        : handle_(handle) {}

    Generator(const Generator&) = delete;

    Generator(Generator &&other) noexcept
        : handle_(std::exchange(other.handle_, {})) {}

    ~Generator() {
        if (handle_) {
            handle_.destroy();
        }
    }

    Generator& operator=(Generator other) noexcept {
        std::swap(handle_, other.handle_);
        return *this;
    }

public:
    Iterator begin() {
        if (handle_) {
            handle_.resume();
            if (handle_.done()) {
                handle_.promise().try_throw();
            }
        }
        return Iterator{ handle_ };
    }

    std::default_sentinel_t end() const noexcept {
        return std::default_sentinel;
    }

    template <typename ...Ts>
    static Generator<TValue> from(Ts &&...args) {
        (co_yield TValue(std::forward<Ts>(args)), ...);
    }

    template <typename TConvertor>
        requires (!std::is_void_v<std::invoke_result_t<TConvertor, TValue>>)
    Generator<std::invoke_result_t<TConvertor, TValue>> select(TConvertor fn) {
        for (auto it = begin(); it != end(); ++ it) {
            co_yield fn(*it);
        }
    }

    template <typename TFiller>
        requires std::is_invocable_r_v<bool, TFiller, TValue>
    Generator<TValue> where(TFiller &&fn) {
        for (auto &&item : *this) {
            if (fn(item)) {
                co_yield item;
            }
        }
    }

private:
    std::coroutine_handle<promise_type> handle_{nullptr};
};
}

#endif
