#include <gtest/gtest.h>

#include <chrono>
#include <thread>
#include <type_traits>

#include "fc/core/scoped_timer.hpp"

namespace {

using namespace std::chrono_literals;
using fc::core::ScopedTimer;

static_assert(!std::is_copy_constructible_v<ScopedTimer>);
static_assert(!std::is_move_constructible_v<ScopedTimer>);
static_assert(std::is_nothrow_constructible_v<ScopedTimer, std::chrono::nanoseconds&>);

TEST(ScopedTimer, WritesElapsedOnlyAtScopeExit) {
  std::chrono::nanoseconds out{-1};
  {
    const ScopedTimer timer{out};
    std::this_thread::sleep_for(5ms);
    EXPECT_EQ(out, -1ns);  // Untouched while the timer is alive.
  }
  EXPECT_GE(out, 5ms);
}

TEST(ScopedTimer, ElapsedGrowsWhileRunning) {
  std::chrono::nanoseconds out{};
  const ScopedTimer timer{out};
  const auto first = timer.elapsed();
  std::this_thread::sleep_for(2ms);
  const auto second = timer.elapsed();
  EXPECT_GE(first, 0ns);
  EXPECT_GE(second - first, 2ms);
}

TEST(ScopedTimer, NestedTimersMeasureTheirOwnScope) {
  std::chrono::nanoseconds outer{};
  std::chrono::nanoseconds inner{};
  {
    const ScopedTimer outer_timer{outer};
    std::this_thread::sleep_for(2ms);
    {
      const ScopedTimer inner_timer{inner};
      std::this_thread::sleep_for(2ms);
    }
  }
  EXPECT_GE(inner, 2ms);
  EXPECT_GE(outer, inner + 2ms);
}

}  // namespace
