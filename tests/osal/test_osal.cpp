#include <gtest/gtest.h>

#include <string>
#include <thread>

#include "fc/osal/osal.hpp"

namespace {

using fc::osal::Status;

// Runs fn on a fresh thread so tests never rename or reprioritize gtest's thread.
template <typename Fn>
void on_new_thread(Fn fn) {
  std::thread worker{fn};
  worker.join();
}

TEST(Osal, SetsAndReadsThreadName) {
  Status status = Status::error;
  std::string seen;
  on_new_thread([&] {
    status = fc::osal::set_current_thread_name("fc_osal_test");
    seen = fc::osal::current_thread_name();
  });
  EXPECT_EQ(status, Status::ok);
  EXPECT_EQ(seen, "fc_osal_test");
}

TEST(Osal, AcceptsNameAtMaxLength) {
  Status status = Status::error;
  on_new_thread([&] { status = fc::osal::set_current_thread_name("exactly_15_char"); });
  EXPECT_EQ(status, Status::ok);
}

TEST(Osal, RejectsEmptyAndTooLongNames) {
  Status empty = Status::ok;
  Status too_long = Status::ok;
  on_new_thread([&] {
    empty = fc::osal::set_current_thread_name("");
    too_long = fc::osal::set_current_thread_name("sixteen_chars_xx");
  });
  EXPECT_EQ(empty, Status::invalid_argument);
  EXPECT_EQ(too_long, Status::invalid_argument);
}

TEST(Osal, PriorityRangeIsSane) {
  const auto range = fc::osal::realtime_priority_range();
  EXPECT_LT(range.min, range.max);
}

TEST(Osal, RejectsOutOfRangePriority) {
  const auto range = fc::osal::realtime_priority_range();
  EXPECT_EQ(fc::osal::set_current_thread_priority(range.max + 1), Status::invalid_argument);
  EXPECT_EQ(fc::osal::set_current_thread_priority(range.min - 1), Status::invalid_argument);
}

TEST(Osal, InRangePrioritySucceedsOrIsDenied) {
  Status status = Status::error;
  on_new_thread([&] {
    status = fc::osal::set_current_thread_priority(fc::osal::realtime_priority_range().min);
  });
  EXPECT_TRUE(status == Status::ok || status == Status::permission_denied)
      << fc::osal::to_string(status);
}

TEST(Osal, RejectsInvalidCore) {
  EXPECT_EQ(fc::osal::pin_current_thread_to_core(-1), Status::invalid_argument);
  const auto cores = static_cast<int>(std::thread::hardware_concurrency());
  if (cores > 0) {
    EXPECT_EQ(fc::osal::pin_current_thread_to_core(cores), Status::invalid_argument);
  }
}

TEST(Osal, PinsToCoreZeroWhereSupported) {
  Status status = Status::error;
  on_new_thread([&] { status = fc::osal::pin_current_thread_to_core(0); });
#if defined(__linux__)
  EXPECT_EQ(status, Status::ok);
#else
  EXPECT_EQ(status, Status::not_supported);
#endif
}

TEST(Osal, EveryStatusHasAName) {
  for (const Status status : {Status::ok, Status::invalid_argument, Status::permission_denied,
                              Status::not_supported, Status::error}) {
    EXPECT_NE(fc::osal::to_string(status), "unknown");
  }
}

}  // namespace
