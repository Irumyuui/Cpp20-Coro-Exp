#include <iostream>

#include "include/TaskAwaiter.hpp"
#include "include/Task.hpp"

using karus::coro::Task;

template <typename T, typename ...Ts>
void debug(const T &val, const Ts &...args) {
    std::cerr << val;
    ((std::cerr << ' ' << args), ...);
    std::cerr << '\n';
}

Task<int, karus::coro::AsyncExecutor> simple_task2() {
  debug("task 2 start ...");
  using namespace std::chrono_literals;
  co_await karus::coro::SleepAwaiter{1s};
  debug("task 2 returns after 1s.");
  co_return 2;
}

Task<int, karus::coro::NewThreadExecutor> simple_task3() {
  debug("in task 3 start ...");
  using namespace std::chrono_literals;
  co_await karus::coro::SleepAwaiter{2s};
  debug("task 3 returns after 2s.");
  co_return 3;
}

Task<int, karus::coro::LooperExecutor> simple_task() {
  debug("task start ...");
  using namespace std::chrono_literals;
  co_await karus::coro::SleepAwaiter{100ms};
  debug("after 100ms ...");
  auto result2 = co_await simple_task2();
  debug("returns from task2: ", result2);

  co_await karus::coro::SleepAwaiter{500ms};
  debug("after 500ms ...");
  auto result3 = co_await simple_task3();
  debug("returns from task3: ", result3);
  co_return 1 + result2 + result3;
}

int main() {
  auto simpleTask = simple_task();
  simpleTask.then([](int i) {
    debug("simple task end: ", i);
  }).catching([](std::exception &e) {
    debug("error occurred", e.what());
  });
  try {
    auto i = simpleTask.get_result();
    debug("simple task end from get: ", i);
  } catch (std::exception &e) {
    debug("error: ", e.what());
  }
}
