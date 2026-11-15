#pragma once

// OS abstraction layer: the only place that talks to pthreads directly.
// Everything above this layer stays portable between Linux and macOS.
//
// All "current_thread" calls act on the calling thread. macOS can only name
// the calling thread, so callers must apply settings from inside the thread.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace fc::osal {

enum class Status : std::uint8_t {
  ok,
  invalid_argument,   // Rejected before calling the OS (bad name, priority, core).
  permission_denied,  // OS refused, usually real-time priority without privileges.
  not_supported,      // Platform has no such feature (core pinning on macOS).
  error,              // Any other OS failure.
};

[[nodiscard]] std::string_view to_string(Status status) noexcept;

// Linux limits thread names to 15 characters plus the terminator.
inline constexpr std::size_t kMaxThreadNameLength = 15;

// Empty names and names longer than kMaxThreadNameLength are invalid_argument.
[[nodiscard]] Status set_current_thread_name(std::string_view name) noexcept;

// Diagnostic helper. Allocates, so keep it off real-time paths.
[[nodiscard]] std::string current_thread_name();

struct PriorityRange {
  int min;
  int max;
};

// Valid SCHED_FIFO priorities on this platform.
[[nodiscard]] PriorityRange realtime_priority_range() noexcept;

// Switches the calling thread to SCHED_FIFO at the given priority.
// Expect permission_denied on Linux without CAP_SYS_NICE.
[[nodiscard]] Status set_current_thread_priority(int priority) noexcept;

// Pins the calling thread to one CPU core. not_supported on macOS.
[[nodiscard]] Status pin_current_thread_to_core(int core) noexcept;

}  // namespace fc::osal
