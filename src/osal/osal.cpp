#include "fc/osal/osal.hpp"

#include <pthread.h>
#include <sched.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <thread>

namespace fc::osal {

namespace {

Status from_error_code(int rc) noexcept {
  switch (rc) {
    case 0:
      return Status::ok;
    case EPERM:
      return Status::permission_denied;
    case EINVAL:
      return Status::invalid_argument;
    case ENOTSUP:
      return Status::not_supported;
    default:
      return Status::error;
  }
}

}  // namespace

std::string_view to_string(Status status) noexcept {
  switch (status) {
    case Status::ok:
      return "ok";
    case Status::invalid_argument:
      return "invalid_argument";
    case Status::permission_denied:
      return "permission_denied";
    case Status::not_supported:
      return "not_supported";
    case Status::error:
      return "error";
  }
  return "unknown";
}

Status set_current_thread_name(std::string_view name) noexcept {
  if (name.empty() || name.size() > kMaxThreadNameLength) {
    return Status::invalid_argument;
  }
  // pthread wants a null-terminated string. Copy onto the stack, no heap.
  std::array<char, kMaxThreadNameLength + 1> buffer{};
  std::ranges::copy(name, buffer.begin());
#if defined(__APPLE__)
  return from_error_code(pthread_setname_np(buffer.data()));
#else
  return from_error_code(pthread_setname_np(pthread_self(), buffer.data()));
#endif
}

std::string current_thread_name() {
  std::array<char, 64> buffer{};
  if (pthread_getname_np(pthread_self(), buffer.data(), buffer.size()) != 0) {
    return {};
  }
  return std::string{buffer.data()};
}

PriorityRange realtime_priority_range() noexcept {
  return {.min = sched_get_priority_min(SCHED_FIFO), .max = sched_get_priority_max(SCHED_FIFO)};
}

Status set_current_thread_priority(int priority) noexcept {
  const PriorityRange range = realtime_priority_range();
  if (priority < range.min || priority > range.max) {
    return Status::invalid_argument;
  }
  sched_param param{};
  param.sched_priority = priority;
  return from_error_code(pthread_setschedparam(pthread_self(), SCHED_FIFO, &param));
}

Status pin_current_thread_to_core(int core) noexcept {
  const unsigned int cores = std::thread::hardware_concurrency();
  if (core < 0 || (cores != 0 && static_cast<unsigned int>(core) >= cores)) {
    return Status::invalid_argument;
  }
#if defined(__linux__)
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(static_cast<std::size_t>(core), &set);
  return from_error_code(pthread_setaffinity_np(pthread_self(), sizeof(set), &set));
#else
  return Status::not_supported;
#endif
}

}  // namespace fc::osal
