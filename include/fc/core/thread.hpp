#pragma once

// RAII thread: owns a std::jthread plus the setup a flight thread needs
// (name, real-time priority, core pinning). Destroying a Thread requests stop
// and joins, so a thread can never outlive the object that owns it.

#include <functional>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>

#include "fc/osal/osal.hpp"

namespace fc::core {

struct ThreadConfig {
  std::string_view name;        // Copied. Empty means "leave the OS default name".
  std::optional<int> priority;  // SCHED_FIFO priority. Unset means "leave as is".
  std::optional<int> cpu_core;  // Core to pin to. Unset means "any core".
};

class Thread {
 public:
  // The body must return promptly once the stop token is triggered.
  using Body = std::function<void(std::stop_token)>;

  // An empty Thread that owns nothing. joinable() is false.
  Thread() noexcept = default;

  // Starts the thread. The new thread applies name, priority and core from
  // inside itself (macOS can only name the calling thread), then runs body.
  //
  // Setup is best effort: body runs even if a setting fails. The constructor
  // does not return until setup has finished, so setup_status() is valid as
  // soon as the constructor returns.
  Thread(ThreadConfig config, Body body);

  // Requests stop and joins if still joinable.
  ~Thread();

  Thread(const Thread&) = delete;
  Thread& operator=(const Thread&) = delete;

  // Moving transfers ownership of the running thread. The moved-from Thread
  // is empty. Move assignment stops and joins whatever this Thread owned first.
  Thread(Thread&& other) noexcept;
  Thread& operator=(Thread&& other) noexcept;

  // Asks the body to finish. Does not wait. Safe on an empty Thread.
  void request_stop() noexcept;

  // Blocks until the body returns. Precondition: joinable().
  void join();

  [[nodiscard]] bool joinable() const noexcept;
  [[nodiscard]] std::string_view name() const noexcept;

  // First non-ok result from applying name, priority, then core. ok if all
  // succeeded or nothing was requested.
  [[nodiscard]] osal::Status setup_status() const noexcept;

 private:
  // Declaration order matters. Members are built top to bottom and destroyed
  // bottom to top, so thread_ starts after name_ exists and is joined before
  // name_ and setup_status_ go away.
  std::string name_;
  osal::Status setup_status_{osal::Status::ok};
  std::jthread thread_;
};

}  // namespace fc::core
