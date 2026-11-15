#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "fc/core/thread.hpp"
#include "fc/osal/osal.hpp"

namespace {

using namespace std::chrono_literals;
using fc::core::Thread;
using fc::core::ThreadConfig;
using fc::osal::Status;

static_assert(!std::is_copy_constructible_v<Thread>);
static_assert(!std::is_copy_assignable_v<Thread>);
static_assert(std::is_nothrow_move_constructible_v<Thread>);
static_assert(std::is_nothrow_move_assignable_v<Thread>);

// Polls pred until it is true or the timeout passes. Keeps failing tests from hanging.
template <typename Pred>
bool eventually(Pred pred, std::chrono::milliseconds timeout = 1000ms) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (!pred()) {
    if (std::chrono::steady_clock::now() > deadline) {
      return false;
    }
    std::this_thread::sleep_for(1ms);
  }
  return true;
}

// A body that spins until asked to stop, recording start and exit.
struct Looper {
  std::atomic<bool> started{false};
  std::atomic<bool> exited{false};

  Thread::Body body() {
    return [this](const std::stop_token& stop) {
      started = true;
      while (!stop.stop_requested()) {
        std::this_thread::sleep_for(1ms);
      }
      exited = true;
    };
  }
};

TEST(Thread, DefaultConstructedIsEmpty) {
  Thread thread;
  EXPECT_FALSE(thread.joinable());
  EXPECT_TRUE(thread.name().empty());
  thread.request_stop();  // Must be safe on an empty Thread.
}

TEST(Thread, RunsBody) {
  std::atomic<bool> ran{false};
  Thread thread{{.name = "runner"}, [&](const std::stop_token&) { ran = true; }};
  EXPECT_TRUE(eventually([&] { return ran.load(); }));
}

TEST(Thread, AppliesNameFromInsideThread) {
  std::string seen;
  std::atomic<bool> done{false};
  Thread thread{{.name = "fc_named"}, [&](const std::stop_token&) {
                  seen = fc::osal::current_thread_name();
                  done.store(true, std::memory_order_release);
                }};
  ASSERT_TRUE(eventually([&] { return done.load(std::memory_order_acquire); }));
  EXPECT_EQ(seen, "fc_named");
  EXPECT_EQ(thread.name(), "fc_named");
  EXPECT_EQ(thread.setup_status(), Status::ok);
}

TEST(Thread, EmptyNameSkipsNaming) {
  Thread thread{{}, [](const std::stop_token&) {}};
  EXPECT_TRUE(thread.joinable());
  EXPECT_EQ(thread.setup_status(), Status::ok);
}

TEST(Thread, DestructorRequestsStopAndJoins) {
  Looper looper;
  {
    Thread thread{{.name = "looper"}, looper.body()};
    ASSERT_TRUE(eventually([&] { return looper.started.load(); }));
  }
  EXPECT_TRUE(looper.exited.load());
}

TEST(Thread, RequestStopThenJoin) {
  Looper looper;
  Thread thread{{.name = "looper"}, looper.body()};
  ASSERT_TRUE(eventually([&] { return looper.started.load(); }));
  thread.request_stop();
  thread.join();
  EXPECT_TRUE(looper.exited.load());
  EXPECT_FALSE(thread.joinable());
}

// setup_status() is checked right after construction with no waiting. That only
// passes if the constructor waits for the new thread to finish its setup.
TEST(Thread, BadNameIsReportedButBodyStillRuns) {
  std::atomic<bool> ran{false};
  Thread thread{{.name = "this_name_is_far_too_long"}, [&](const std::stop_token&) { ran = true; }};
  EXPECT_EQ(thread.setup_status(), Status::invalid_argument);
  EXPECT_TRUE(eventually([&] { return ran.load(); }));
}

TEST(Thread, OutOfRangePriorityIsReported) {
  const int too_high = fc::osal::realtime_priority_range().max + 1;
  Thread thread{{.name = "prio", .priority = too_high}, [](const std::stop_token&) {}};
  EXPECT_EQ(thread.setup_status(), Status::invalid_argument);
}

TEST(Thread, InvalidCoreIsReported) {
  Thread thread{{.name = "pinned", .cpu_core = -1}, [](const std::stop_token&) {}};
  EXPECT_EQ(thread.setup_status(), Status::invalid_argument);
}

TEST(Thread, MoveConstructTransfersOwnership) {
  Looper looper;
  Thread first{{.name = "mover"}, looper.body()};
  ASSERT_TRUE(eventually([&] { return looper.started.load(); }));

  Thread second{std::move(first)};
  EXPECT_FALSE(first.joinable());  // NOLINT(bugprone-use-after-move): moved-from state is defined.
  EXPECT_TRUE(second.joinable());
  EXPECT_EQ(second.name(), "mover");
  EXPECT_FALSE(looper.exited.load());
}

TEST(Thread, MoveAssignStopsPreviousThread) {
  Looper old_work;
  Looper new_work;
  Thread target{{.name = "old"}, old_work.body()};
  Thread source{{.name = "new"}, new_work.body()};

  target = std::move(source);
  EXPECT_TRUE(old_work.exited.load());
  EXPECT_FALSE(new_work.exited.load());
  EXPECT_EQ(target.name(), "new");
  EXPECT_TRUE(target.joinable());
}

// Stress tests. Their real value is running clean under the tsan preset.

TEST(ThreadStress, RapidCreateAndDestroy) {
  constexpr int kIterations = 200;
  std::atomic<int> exits{0};
  for (int i = 0; i < kIterations; ++i) {
    const Thread thread{{.name = "churn"}, [&](const std::stop_token& stop) {
                          while (!stop.stop_requested()) {
                            std::this_thread::yield();
                          }
                          exits.fetch_add(1);
                        }};
  }
  EXPECT_EQ(exits.load(), kIterations);
}

TEST(ThreadStress, RunningThreadsSurviveVectorGrowth) {
  constexpr int kWorkers = 8;
  std::atomic<std::uint64_t> ticks{0};
  std::atomic<int> exits{0};
  std::vector<Thread> workers;  // No reserve(): growth moves Threads while they run.
  for (int i = 0; i < kWorkers; ++i) {
    workers.emplace_back(ThreadConfig{.name = "worker"}, [&](const std::stop_token& stop) {
      while (!stop.stop_requested()) {
        ticks.fetch_add(1, std::memory_order_relaxed);
        std::this_thread::yield();
      }
      exits.fetch_add(1);
    });
  }
  ASSERT_TRUE(eventually([&] { return ticks.load(std::memory_order_relaxed) > 1000; }));
  for (auto& worker : workers) {
    worker.request_stop();
  }
  workers.clear();
  EXPECT_EQ(exits.load(), kWorkers);
}

}  // namespace
