#include "fc/core/thread.hpp"

#include <future>
#include <utility>

namespace fc::core {

namespace {

// Applies the requested settings to the calling thread in a fixed order and
// returns the first failure. Later settings are still attempted after a failure.
osal::Status apply_setup(std::string_view name, std::optional<int> priority,
                         std::optional<int> cpu_core) noexcept {
  osal::Status first_failure = osal::Status::ok;
  const auto record = [&first_failure](osal::Status status) {
    if (first_failure == osal::Status::ok) {
      first_failure = status;
    }
  };
  if (!name.empty()) {
    record(osal::set_current_thread_name(name));
  }
  if (priority) {
    record(osal::set_current_thread_priority(*priority));
  }
  if (cpu_core) {
    record(osal::pin_current_thread_to_core(*cpu_core));
  }
  return first_failure;
}

}  // namespace

Thread::Thread(ThreadConfig config, Body body) : name_(config.name) {
  // The new thread signals through this promise once setup is done. The promise
  // moves into the thread, so it stays alive however long set_value() takes.
  std::promise<void> setup_done;
  std::future<void> setup_finished = setup_done.get_future();

  thread_ = std::jthread{[this, priority = config.priority, cpu_core = config.cpu_core,
                          setup_done = std::move(setup_done),
                          body = std::move(body)](std::stop_token stop) mutable {
    // Safe to use `this` here: the constructor is blocked below, so nobody can
    // move or destroy this Thread yet.
    setup_status_ = apply_setup(name_, priority, cpu_core);
    setup_done.set_value();
    // From here on, never touch `this`. The Thread may be moved at any moment.
    body(std::move(stop));
  }};

  setup_finished.wait();
}

// Members are destroyed in reverse declaration order, so thread_ goes first:
// std::jthread's destructor requests stop and joins. Only then are name_ and
// setup_status_ destroyed.
Thread::~Thread() = default;

Thread::Thread(Thread&& other) noexcept
    : name_(std::exchange(other.name_, {})),
      setup_status_(std::exchange(other.setup_status_, osal::Status::ok)),
      thread_(std::move(other.thread_)) {}

Thread& Thread::operator=(Thread&& other) noexcept {
  if (this != &other) {
    // std::jthread's move assignment requests stop on, and joins, the thread we
    // owned before taking the new one.
    thread_ = std::move(other.thread_);
    name_ = std::exchange(other.name_, {});
    setup_status_ = std::exchange(other.setup_status_, osal::Status::ok);
  }
  return *this;
}

void Thread::request_stop() noexcept { static_cast<void>(thread_.request_stop()); }

void Thread::join() { thread_.join(); }

bool Thread::joinable() const noexcept { return thread_.joinable(); }

std::string_view Thread::name() const noexcept { return name_; }

osal::Status Thread::setup_status() const noexcept { return setup_status_; }

}  // namespace fc::core
