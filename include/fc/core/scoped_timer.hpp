#pragma once

// Measures how long a scope takes and writes the result when the scope ends.
// Safe for real-time paths: no allocation, no exceptions, no callbacks.
//
//   std::chrono::nanoseconds cycle_time{};
//   {
//     fc::core::ScopedTimer timer{cycle_time};
//     run_rate_group();
//   }  // cycle_time now holds the elapsed time.

#include <chrono>

namespace fc::core {

class ScopedTimer {
 public:
  using Clock = std::chrono::steady_clock;

  // Starts timing. `out` is written once, when this timer is destroyed, and
  // must outlive the timer.
  explicit ScopedTimer(std::chrono::nanoseconds& out) noexcept;

  // Writes the elapsed time into `out`.
  ~ScopedTimer();

  // Pinned to its scope: copying or moving would make the timing meaningless.
  ScopedTimer(const ScopedTimer&) = delete;
  ScopedTimer& operator=(const ScopedTimer&) = delete;
  ScopedTimer(ScopedTimer&&) = delete;
  ScopedTimer& operator=(ScopedTimer&&) = delete;

  // Time since construction, without stopping the timer.
  [[nodiscard]] std::chrono::nanoseconds elapsed() const noexcept;

 private:
  // A pointer, not a reference: reference members silently delete assignment
  // and clang-tidy rejects them. Never null, set from the constructor argument.
  std::chrono::nanoseconds* out_;
  Clock::time_point start_;
};

}  // namespace fc::core
