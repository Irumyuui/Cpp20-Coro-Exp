#include <algorithm>
#include <exception>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "include/TaskAwaiter.hpp"
#include "include/Task.hpp"
#include "include/Generator.hpp"

using namespace karus::coro;

Generator<int> Fib(int n) {
  if (n == 0 || n == 1) {
    co_return;
  }
  int a = 0, b = 1;
  co_yield a;
  co_yield b;

  for (int i = 2; i <= n; ++i) {
    co_yield a + b;

    b = a + b;
    a = b - a;
  }
  co_return;
}
