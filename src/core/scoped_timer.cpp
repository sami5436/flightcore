#include "fc/core/scoped_timer.hpp"

namespace fc::core {

ScopedTimer::ScopedTimer(std::chrono::nanoseconds& out) noexcept
    : out_(&out), start_(Clock::now()) {}

ScopedTimer::~ScopedTimer() { *out_ = elapsed(); }

std::chrono::nanoseconds ScopedTimer::elapsed() const noexcept {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start_);
}

}  // namespace fc::core
