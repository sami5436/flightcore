# Milestone 0: Project Setup

Milestone 0 sets up the build, test and CI foundation for the flight code. It has no flight logic
yet. The goal is that every line written from Milestone 1 on is compiled with strict warnings,
tested, and checked for memory errors and data races automatically.

On real flight software teams this setup comes first. Later in a project it is expensive to add,
because existing code must first be fixed for every new warning or sanitizer report.

## Directory layout

```
flightcore/
├── CMakeLists.txt          top-level build definition
├── CMakePresets.json       named build configurations (debug, release, asan, tsan)
├── cmake/
│   ├── Warnings.cmake      strict compiler warnings, as errors
│   └── Sanitizers.cmake    ASan/UBSan and TSan switches
├── include/fc/core/        public headers  (what other code #includes)
├── src/core/               implementation  (.cpp files)
├── tests/                  GoogleTest unit tests, mirroring src/
├── docs/                   milestone write-ups like this one
├── .clang-format           code style
├── .clang-tidy             static analysis rules
├── .github/workflows/      CI pipeline
├── README.md               public description
└── CLAUDE.md               working rules for our pairing sessions
```

**Why `include/fc/core/` and not just `include/`?** Includes look like
`#include "fc/core/version.hpp"`. The `fc/` prefix acts like a namespace for headers, so the
header never collides with another library's `version.hpp`. Each module (core, bus, sched, gnc)
gets its own folder, and the C++ namespaces match: `fc::core`, `fc::bus`, and so on.

**Why separate `include/` and `src/`?** Headers in `include/` are the public interface, like the
public section of a class. Files in `src/` are the implementation. Anything that uses the library
sees only the interface.

## CMake: `CMakeLists.txt`

CMake does not compile code itself. It generates build files (here, Ninja files) that call the
compiler. Think of it as a generator for a large, correct Makefile.

```cmake
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)   # fail instead of silently falling back to C++17
set(CMAKE_CXX_EXTENSIONS OFF)         # -std=c++20, not -std=gnu++20 (no compiler extensions)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # writes compile_commands.json for clangd / clang-tidy
```

### Targets, the core idea of modern CMake

```cmake
add_library(fc_core src/core/version.cpp)
add_library(fc::core ALIAS fc_core)
target_include_directories(fc_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
```

A **target** is like a class. It owns its sources, include paths, flags and dependencies.
`PUBLIC` vs `PRIVATE` works the way it does in C++ inheritance:

- `PRIVATE`: used only to build this target (example: warning flags).
- `PUBLIC`: used by this target **and passed on** to anything that links it (example: the include
  directory, so the tests can `#include "fc/core/..."` without setting it again).
- `INTERFACE`: passed on only, not used by the target itself.

The `fc::core` alias exists because a typo in a `::` name is a hard configure error, while a typo
in a plain name can be silently treated as a system library. The `$<BUILD_INTERFACE:...>`
wrapper is a generator expression: the path applies only while building from source, not after an
install.

`FC_VERSION_STRING` is passed from `project(VERSION 0.1.0)` into C++ as a `#define`, so the
version lives in exactly one place.

## Warnings: `cmake/Warnings.cmake`

Every warning is an error (`-Werror`). Flight code does not ship with warnings, and in safety
standards like DO-178C a warning is a finding you have to justify. The important flags:

| Flag | Catches |
|------|---------|
| `-Wall -Wextra -Wpedantic` | the standard set, plus non-portable extensions |
| `-Wshadow` | a local variable hiding a member or outer variable |
| `-Wconversion -Wsign-conversion` | silent narrowing, like `double` to `int` or `int` to `size_t` |
| `-Wold-style-cast` | C-style casts; use `static_cast` and friends |
| `-Wnon-virtual-dtor` | a polymorphic base class without a virtual destructor (leaks, UB) |
| `-Woverloaded-virtual` | a derived function hiding a base virtual instead of overriding it |
| `-Wnull-dereference` | paths where the compiler can prove a null deref |
| `-Wdouble-promotion` | `float` silently promoted to `double` (costly on embedded FPUs) |
| `-Wimplicit-fallthrough` | a `switch` case falling into the next without `[[fallthrough]]` |

`-Wconversion` will be the most annoying one, and the most valuable. Numeric conversion bugs are
a classic aerospace failure mode: Ariane 5 Flight 501 was lost to an unchecked 64-bit float to
16-bit integer conversion.

The function is applied per target (`fc_set_warnings(fc_core)`), so third-party code like
GoogleTest is not held to our rules.

## Sanitizers: `cmake/Sanitizers.cmake`

Sanitizers are compiler instrumentation that check your program **while it runs**.

- **ASan (AddressSanitizer)**: out-of-bounds reads/writes, use-after-free, double free, leaks.
- **UBSan (UndefinedBehaviorSanitizer)**: signed overflow, misaligned access, bad shifts, invalid
  enum values. Bundled with ASan here.
- **TSan (ThreadSanitizer)**: data races, meaning two threads touching the same memory, at least
  one writing, with no synchronization. This is the one that matters most for this project.

ASan and TSan cannot run in the same binary (both remap memory), so each gets its own build.
`-fno-sanitize-recover=all` makes UBSan stop at the first error instead of printing and
continuing, so a test cannot pass while UB happened. `-fno-omit-frame-pointer` keeps stack traces
readable.

Selecting one: `-DFC_SANITIZER=address` or `-DFC_SANITIZER=thread`. You normally go through a
preset instead.

## Presets: `CMakePresets.json`

Presets name the configurations so nobody has to remember the flags:

| Preset | Build type | Use it for |
|--------|------------|------------|
| `debug` | Debug | everyday work, debugger |
| `release` | RelWithDebInfo | timing and benchmarks (optimized, but keeps symbols) |
| `asan` | Debug + ASan/UBSan | memory and UB checks |
| `tsan` | Debug + TSan | any threading change |

Each builds into its own folder (`build/debug`, `build/tsan`, ...), so switching costs nothing.

```
cmake --preset tsan          # configure
cmake --build --preset tsan  # compile
ctest --preset tsan          # run tests
```

## Tests: `tests/`

GoogleTest is downloaded at configure time with `FetchContent`, pinned to tag `v1.15.2`. Pinning
matters: an unpinned dependency means the same commit can build differently next month.

```cmake
add_executable(fc_tests core/test_version.cpp)
target_link_libraries(fc_tests PRIVATE fc::core GTest::gtest_main)
gtest_discover_tests(fc_tests)
```

`gtest_main` provides `main()`. `gtest_discover_tests` asks the test binary which `TEST()`s it
contains and registers each one with CTest separately, so failures are reported per test.

The one test today (`Version.MatchesProjectVersion`) is only a smoke test. It proves the library
builds, links, and runs under every preset.

## Code style and static analysis

- **`.clang-format`**: Google style, 100-column lines. Formatting is never discussed in review;
  the tool decides. Run `clang-format -i <file>` before committing.
- **`.clang-tidy`**: static analysis, meaning it finds bugs by reading code instead of running it.
  Enabled groups: `bugprone`, `concurrency`, `cppcoreguidelines`, `modernize`, `performance`,
  `readability`. A few noisy checks are turned off (magic numbers, short names, trailing return
  types). It runs in CI.

## CI: `.github/workflows/ci.yml`

On every push to `main` and every pull request, GitHub Actions runs:

1. **build-test**: a matrix of 2 OSes (Ubuntu 24.04, macOS 15) x 4 presets = 8 jobs. Each
   configures, builds, and runs all tests. `fail-fast: false` lets every job finish, so one
   failure does not hide others.
2. **lint**: clang-format in check mode (fails on unformatted code) and clang-tidy.

Linux is the main target because real-time Linux (PREEMPT_RT) is where we will measure jitter
later. macOS keeps the code portable and matches the dev machine.

## Local setup details

- **`compile_commands.json`** at the repo root is a symlink to `build/debug/compile_commands.json`
  (gitignored, local only). clangd in vim reads it to know the exact flags, so completions and
  errors match the real build.
- **Git identity**: commits use the GitHub noreply email so no personal email appears in history.
- **`.gitignore`**: build output, editor folders, and `.env*` files (in case secrets ever appear).

## Commits so far

1. CMake build skeleton with `fc_core` library and GoogleTest
2. CMake presets
3. clang-format and clang-tidy configs
4. CI workflow
5. README
6. CLAUDE.md

Each is one logical change, so history reads like a changelog and any commit can be reverted
cleanly.

## Try it yourself

These take a few minutes each and show why the setup is worth it:

1. **Warnings as errors**: in `src/core/version.cpp`, add `int x = 3.7;` inside the function.
   Build `debug` and read the error. Remove it.
2. **ASan**: in a test, write `int a[3]; a[3] = 1;`. Run `ctest --preset debug` (likely passes),
   then `ctest --preset asan` (fails with a stack trace). Remove it.
3. **TSan**: in a test, start two `std::thread`s that each do `++counter` 100000 times on a plain
   `int`. Run under `tsan` and read the race report. Change `int` to `std::atomic<int>` and run
   it again.

Exercise 3 is the preview of Milestone 1.

## Be ready to explain

- Why are warnings errors, and why are they applied per target?
- Why can ASan and TSan not share a build?
- What is the difference between `PUBLIC` and `PRIVATE` in `target_link_libraries`?
- Why pin dependency versions?
- Why does CI run on Linux and macOS?
