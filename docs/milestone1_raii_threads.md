# Milestone 1: RAII, Threads and the OS Layer

## The big picture

**What we built:** three small pieces that everything later stands on.

1. **OSAL** (OS abstraction layer): the one place that talks to the operating system about threads.
2. **ScopedTimer**: a stopwatch that writes down how long a block of code took.
3. **Thread**: a safe thread that can never be left running by accident.

**Analogy:** Milestone 0 built the factory. Milestone 1 hires the crew and gives them rules: every
crew member has a name badge, a rank, an assigned workstation, and a time clock they must punch
out on before they leave. Nobody wanders off mid-shift.

**Why it matters:** flight software is many tasks running at once (guidance at 100 Hz, telemetry
at 1 Hz, fault checks in between). Every later milestone (scheduler, software bus, GNC sim) runs
on these threads and measures itself with this timer.

```
   GNC sim, fault mgmt         (later milestones)
          |
   scheduler, software bus     (later milestones)
          |
   fc::core::Thread, ScopedTimer   <- this milestone
          |
   fc::osal                        <- this milestone
          |
   pthreads (macOS / Linux)
```

Files touched:

| File | What it is |
|---|---|
| `include/fc/osal/osal.hpp`, `src/osal/osal.cpp` | OS layer |
| `include/fc/core/scoped_timer.hpp`, `src/core/scoped_timer.cpp` | stopwatch |
| `include/fc/core/thread.hpp`, `src/core/thread.cpp` | RAII thread |
| `tests/osal/test_osal.cpp`, `tests/core/test_scoped_timer.cpp`, `tests/core/test_thread.cpp` | tests |

---

## 1. RAII: cleanup you cannot forget

**What it is:** RAII stands for "Resource Acquisition Is Initialization", a bad name for a simple
idea. An object grabs something in its **constructor** and gives it back in its **destructor**.
C++ guarantees the destructor runs when the object goes out of scope, however that happens: the
block ends, a `return`, or an exception.

**Analogy:** a hotel key card that cuts the room power when you pull it out of the slot. You never
have to remember to turn off the lights. Leaving the room (the scope) does it.

See it for yourself:

```cpp
#include <cstdio>

struct Noisy {
  Noisy()  { std::puts("constructor: hello"); }
  ~Noisy() { std::puts("destructor: goodbye"); }
};

int main() {
  std::puts("1. before the braces");
  {
    Noisy n;                      // constructor runs here
    std::puts("2. inside the braces");
  }                               // destructor runs here, at the closing brace
  std::puts("3. after the braces");
}
```

Output:

```
1. before the braces
constructor: hello
2. inside the braces
destructor: goodbye
3. after the braces
```

**Why it matters:** in flight software "forgot to clean up" means a thread still running after
its owner died, a lock never released, a device left powered. RAII turns those from "be careful"
into "impossible by construction". Every piece in this milestone is RAII.

---

## 2. ScopedTimer: a stopwatch tied to a scope

**What it is:** start a stopwatch when the object is created, write the elapsed time when it is
destroyed. That's `Noisy` from above with real jobs: the constructor notes the time, the
destructor writes the result.

**Analogy:** a race timing chip. It starts when you cross the start mat and records your time when
you cross the finish. You don't press anything.

How it's used:

```cpp
std::chrono::nanoseconds cycle_time{};
{
  fc::core::ScopedTimer timer{cycle_time};
  run_rate_group();
}  // cycle_time now holds how long run_rate_group() took
```

The whole implementation (`src/core/scoped_timer.cpp`):

```cpp
ScopedTimer::ScopedTimer(std::chrono::nanoseconds& out) noexcept
    : out_(&out), start_(Clock::now()) {}        // 1. remember where to write, start the clock

ScopedTimer::~ScopedTimer() { *out_ = elapsed(); }  // 3. on scope exit, write the result

std::chrono::nanoseconds ScopedTimer::elapsed() const noexcept {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start_);  // 2.
}
```

Four details worth knowing:

**a) `steady_clock`, not `system_clock`.** `system_clock` is wall-clock time, and it can jump:
NTP corrects it, someone changes the date, a GPS time update lands. If it jumps backwards mid
measurement you get a negative duration. `steady_clock` only ever moves forward at a steady rate.
*Analogy:* timing a lap by looking at the town clock tower, which a worker might reset while
you're running, versus using a stopwatch in your hand. **Rule:** measure intervals with
`steady_clock`; only use `system_clock` to stamp "what time of day was it".

**b) `duration_cast`.** Subtracting two time points gives a duration in the clock's own unit,
which might not be nanoseconds. `duration_cast` converts it explicitly, so a unit mistake can't
slip in silently.

**c) Copy and move are deleted.**

```cpp
ScopedTimer(const ScopedTimer&) = delete;
ScopedTimer(ScopedTimer&&) = delete;
```

A copy would mean two timers writing to the same output at different times. A moved timer would
measure a scope it doesn't belong to. Deleting them makes those mistakes compile errors.

**d) It stores a pointer to `out`, so `out` must outlive the timer.** If the `nanoseconds`
variable dies first, the destructor writes into freed memory. The class can't detect that (C++
has no way to ask "is this address still alive?"), so it's written in the header as the caller's
job. ASan would catch it in testing. It's a pointer rather than a reference because reference
members break assignment and our clang-tidy rules reject them.

**Why it matters:** the scheduler (Milestone 2) runs each task in a fixed time slot. It wraps
each task in a `ScopedTimer` to catch overruns, which is how real flight software detects a task
that's about to make the vehicle miss a deadline. `noexcept` and no heap allocation mean it's
safe to use inside that loop.

**Why the nested-timers test passes for free:** each timer has its own `start_` and its own
`out`. Two timers share nothing, so nesting just works.

---

## 3. OSAL: one door to the operating system

**What it is:** a thin layer of functions (`fc::osal`) that are the *only* code allowed to call
pthreads. Everything above it calls `fc::osal::set_current_thread_name(...)` instead of
`pthread_setname_np(...)`.

**Analogy:** a power adapter for travel. Your laptop doesn't care which country's wall socket it's
plugged into; the adapter deals with that. cFS (NASA's flight software framework) has a real OSAL
for exactly this, so the same flight code runs on Linux in the lab and on VxWorks or RTEMS on
the spacecraft.

**Why it matters:** macOS and Linux differ in annoying ways, and those `#if` blocks live in one
file instead of spreading across the codebase:

```cpp
#if defined(__APPLE__)
  return from_error_code(pthread_setname_np(buffer.data()));                  // macOS: calling thread only
#else
  return from_error_code(pthread_setname_np(pthread_self(), buffer.data()));  // Linux: any thread
#endif
```

### 3a. Status codes instead of exceptions

```cpp
enum class Status : std::uint8_t {
  ok, invalid_argument, permission_denied, not_supported, error,
};
```

Every OSAL call returns a `Status`, and `[[nodiscard]]` makes the compiler complain if you ignore
it. pthreads returns raw error numbers (`EPERM`, `EINVAL`), and `from_error_code` translates
those into this enum.

*Why not exceptions?* Many flight coding standards (JSF AV C++, NASA's rules) ban exceptions on
flight paths: they hide control flow and their timing is hard to predict. A return value is
visible right where you call the function.

### 3b. Thread names, without the heap

Linux caps thread names at 15 characters plus the null terminator, so the OSAL rejects anything
longer up front, the same on both platforms. pthreads needs a null-terminated C string, so the
name is copied into a fixed-size buffer **on the stack**:

```cpp
std::array<char, kMaxThreadNameLength + 1> buffer{};  // 16 bytes on the stack, zero-filled
std::ranges::copy(name, buffer.begin());
```

No `std::string`, so no heap allocation. Named threads show up in debuggers, `top -H`, and crash
dumps, which is a big help when ten threads are running.

### 3c. Real-time priority (`SCHED_FIFO`)

Normal threads share the CPU "fairly". `SCHED_FIFO` threads don't: a higher-priority one always
runs first, and keeps running until it blocks or finishes. *Analogy:* an ambulance at an
intersection; it doesn't wait its turn. Guidance and control threads get high priority so a log
writer can never delay them. On Linux this needs privileges, so `permission_denied` is a normal
answer on a dev laptop and the tests accept it.

### 3d. Core pinning

Pinning keeps a thread on one CPU core. That keeps its cache warm and makes timing repeatable.
Linux supports it; macOS has no such API, so we return `not_supported` honestly instead of
pretending it worked.

---

## 4. Threads: `std::thread` vs `std::jthread`

**What a thread is:** a second line of execution inside the same program, running at the same
time and sharing the same memory.

**The `std::thread` trap:** if a `std::thread` is destroyed while still running (nobody called
`join()`), the program calls `std::terminate` and dies. One forgotten `join()`, or an early
`return`, and the vehicle software crashes.

**`std::jthread` (C++20)** fixes that with RAII: its destructor asks the thread to stop, then
waits for it (joins). "j" is for joining.

**Stop tokens:** C++ can't safely kill a thread from the outside, because it might be holding a
lock or be halfway through a write. Instead it **asks**: `request_stop()` sets a flag, and the
thread body checks `stop.stop_requested()` and exits on its own.

```cpp
[](std::stop_token stop) {
  while (!stop.stop_requested()) {
    do_one_cycle();
  }
  // clean exit
}
```

*Analogy:* mission control doesn't cut power to a crew member's station; it radios "wrap up and
return to base", and the crew finishes the current step safely first. That's cooperative
cancellation.

**Why it matters:** this is why the header says "the body must return promptly once the stop
token is triggered". A body that never checks the token makes the destructor wait forever.

---

## 5. `fc::core::Thread`: a flight-ready thread

`std::jthread` handles stopping. `Thread` adds what a flight thread also needs: a **name**, a
**priority**, a **core**, and a report of whether those settings worked.

```cpp
fc::core::Thread gnc{{.name = "gnc", .priority = 80, .cpu_core = 2},
                     [](std::stop_token stop) { /* control loop */ }};
if (gnc.setup_status() != fc::osal::Status::ok) { /* log it, keep flying */ }
```

### 5a. Setup happens inside the new thread

macOS can only name the *calling* thread. So instead of naming the thread from outside, the new
thread names itself as its first job, then sets its priority and core, then runs your body.

```cpp
thread_ = std::jthread{[this, priority = config.priority, cpu_core = config.cpu_core,
                        setup_done = std::move(setup_done),
                        body = std::move(body)](std::stop_token stop) mutable {
  setup_status_ = apply_setup(name_, priority, cpu_core);  // name, priority, core
  setup_done.set_value();                                  // "setup finished"
  // From here on, never touch `this`. The Thread may be moved at any moment.
  body(std::move(stop));
}};

setup_finished.wait();  // constructor blocks until the line above says "setup finished"
```

**Best effort:** if naming fails, we still try priority and core, and the body still runs.
`setup_status()` reports the *first* failure. *Analogy:* a pre-flight check that finds a cabin
light out. You log it and still fly, rather than grounding the aircraft. Whether a failure is
fatal is the caller's decision, not the thread wrapper's.

### 5b. The handshake: why a promise and future

The constructor must not return until setup is done; otherwise `setup_status()` could be read
while the new thread is still writing it, which is a data race. So the two threads hand off:

```
 constructor thread                 new thread
 ------------------                 ----------
 create promise/future
 start thread  -------------------> apply name, priority, core
 future.wait()   (blocked)          write setup_status_
                 <----------------- promise.set_value()
 return; setup_status_ is ready     run body(stop)
```

`std::promise` is the sending end and `std::future` the receiving end of a one-shot message.

*Why not a simple `std::atomic<bool>` on the constructor's stack?* Because of this race: the new
thread sets the flag, the constructor wakes and **returns**, destroying the flag, and *then* the
new thread calls `notify` on memory that no longer exists. With promise/future the shared state
lives on the heap and is reference counted, and the promise is **moved into** the thread's lambda,
so nothing the new thread touches can disappear underneath it. That one heap allocation happens
at startup, never in the real-time loop, so it's allowed.

*Analogy:* a relay race baton. The next runner doesn't start until the baton is physically in
their hand. The promise/future pair is the baton.

### 5c. Declaration order is a design decision

```cpp
 private:
  std::string name_;                                // built 1st, destroyed 3rd
  osal::Status setup_status_{osal::Status::ok};     // built 2nd, destroyed 2nd
  std::jthread thread_;                             // built 3rd, destroyed 1st
```

C++ builds members **top to bottom** and destroys them **bottom to top**, always, regardless of
how you write the initializer list. So:

- `name_` exists before the thread starts, so the new thread can safely read it.
- On destruction, `thread_` is destroyed first: it requests stop and joins. Only after the thread
  is gone are `name_` and `setup_status_` destroyed.

That's why the destructor is just:

```cpp
Thread::~Thread() = default;
```

If `thread_` were declared first, it would be destroyed *last*, after `name_`, so a thread could
still be running while those members are torn down. Our body happens never to touch them after
setup, so today that would be harmless, but one future edit (say, logging `name_` on exit) would
turn it into a use-after-free. Correct order makes the safe thing automatic. *Analogy:* the last person out
turns off the lights. You don't switch off the lights while someone's still inside.

### 5d. Moves: handing a running thread to a new owner

A `Thread` can't be copied (two owners of one thread makes no sense), but it can be **moved**:
ownership of the running thread passes to a new object.

```cpp
Thread::Thread(Thread&& other) noexcept
    : name_(std::exchange(other.name_, {})),
      setup_status_(std::exchange(other.setup_status_, osal::Status::ok)),
      thread_(std::move(other.thread_)) {}
```

`std::exchange(x, new_value)` returns the old value and leaves `new_value` behind, so the
moved-from `Thread` is explicitly empty, just like a default-constructed one.

**Move assignment** has one extra job: if the target already owns a thread, that thread must be
stopped and joined first, or it would be orphaned. `std::jthread`'s own move assignment does
exactly that, so we lean on it:

```cpp
thread_ = std::move(other.thread_);  // stops and joins our old thread, then takes the new one
```

**Why the "never touch `this` after setup" rule matters:** the moment the constructor returns,
the `Thread` object might move to a new address (for example when a `std::vector<Thread>` grows).
If the body still used `this`, it would read the old, abandoned address. The body only uses its
own captured copies and the stop token, so moving is safe.

**Why `noexcept` on moves matters:** `std::vector` only moves elements during growth if the move
is `noexcept`; otherwise it copies, and `Thread` can't be copied, so it wouldn't compile. The test
file checks it at compile time:

```cpp
static_assert(std::is_nothrow_move_constructible_v<Thread>);
```

---

## 6. Proving it: tests and TSan

Each behavior has a test. Some worth reading in `tests/core/test_thread.cpp`:

| Test | What it proves |
|---|---|
| `DestructorRequestsStopAndJoins` | leaving scope stops and waits for the thread |
| `BadNameIsReportedButBodyStillRuns` | best-effort setup, and the constructor waits for it |
| `MoveAssignStopsPreviousThread` | the old thread isn't orphaned |
| `RapidCreateAndDestroy` | 200 threads created and destroyed back to back |
| `RunningThreadsSurviveVectorGrowth` | moving running threads around is safe |

**The `eventually()` helper** polls a condition with a timeout. Without it, a broken
implementation would hang the test run forever; with it, the test fails after a second.

**TSan** (ThreadSanitizer, from Milestone 0) watches every memory access at runtime and reports
when two threads touch the same memory without proper synchronization. The stress tests matter
most under TSan: lots of threads, lots of chances for a race to show up. Before pushing, the
thread tests ran 30 times in a row under TSan, all clean:

```
ctest --preset tsan -R Thread --repeat until-fail:30
```

**Why it matters:** a race might fail once in ten thousand runs on the ground and then on the day
of launch. Ordinary testing can't find bugs that rare; TSan can.

---

## Try it yourself (5 minutes each)

1. **Watch RAII:** compile and run the `Noisy` example from section 1:
   `clang++ -std=c++20 /tmp/raii_demo.cpp -o /tmp/raii_demo && /tmp/raii_demo`. Then add a
   `return` inside the braces and confirm the destructor still prints.
2. **Break the clock rule:** in `scoped_timer.hpp` change `steady_clock` to `system_clock`. Does
   it compile? Do the tests pass? Explain why passing tests don't make it correct.
3. **Break declaration order:** in `thread.hpp` move `std::jthread thread_;` to the top of the
   private section and build. Read the `-Wreorder` error: the compiler noticed the move
   constructor's initializer list no longer matches declaration order. Why does C++ care? (Hint:
   section 5c, "regardless of how you write the initializer list".) Put it back afterwards.
4. **Remove the handshake:** comment out `setup_finished.wait();` in `thread.cpp`, then run
   `ctest --preset tsan -R Thread`. Which tests fail or report races, and why? Restore it.
5. **Forget the stop token:** write a test whose body is `while (true) {}` with no stop check.
   Predict what the destructor does before running it (then press Ctrl-C).
6. **See the names:** in a test, sleep for 30 seconds inside a named thread and run it. In another
   terminal, attach with `lldb -p <pid>` and type `thread list` (macOS), or use
   `top -H -p <pid>` (Linux), to find your thread name.

---

## Be ready to explain (with short answers)

1. **What is RAII, in one sentence?**
   The constructor acquires a resource and the destructor releases it, so cleanup happens however
   the scope exits.
2. **Why `steady_clock` for measuring time?**
   It never jumps backwards. `system_clock` can be adjusted by NTP or a user, which corrupts
   intervals.
3. **Why are ScopedTimer's copy and move deleted?**
   A timer belongs to exactly one scope. Copies or moves would measure the wrong thing or write
   twice.
4. **What happens if a `std::thread` is destroyed while joinable? And a `std::jthread`?**
   `std::thread` calls `std::terminate`. `std::jthread` requests stop and joins.
5. **How do you stop a thread in modern C++?**
   Cooperatively: `request_stop()` sets a flag, and the body checks `stop_requested()` and exits
   itself.
6. **What is an OSAL and why have one?**
   A thin layer that isolates OS calls, so platform differences live in one file and the flight
   code ports between operating systems.
7. **Why return status codes instead of throwing?**
   Exceptions hide control flow and have unpredictable timing; many flight standards ban them.
8. **Why does `Thread` apply its settings from inside the new thread?**
   macOS can only name the calling thread, and doing all setup the same way keeps both
   platforms identical.
9. **Why does the constructor wait for setup, and why promise/future instead of a stack atomic?**
   So `setup_status()` is race-free on return. A stack atomic could be destroyed while the other
   thread is still notifying on it; the promise's shared state is heap-allocated and reference
   counted.
10. **Why does member declaration order matter in `Thread`?**
    Members are destroyed in reverse order, so `thread_` (declared last) is joined before the
    data the thread might use is destroyed.
11. **What does a moved-from `Thread` look like?**
    Empty: not joinable, empty name, status ok, the same as default-constructed.
12. **Why must the move constructor be `noexcept`?**
    So `std::vector` can move elements when it grows; otherwise it tries to copy, and `Thread`
    can't be copied.
13. **What does `SCHED_FIFO` do, and why does it fail on your laptop?**
    Strict priority scheduling: higher priority always runs first. It needs real-time privileges
    (CAP_SYS_NICE on Linux), so it returns `permission_denied` for normal users.
14. **What does TSan find that normal tests don't?**
    Data races: unsynchronized access to shared memory that might misbehave only rarely.
