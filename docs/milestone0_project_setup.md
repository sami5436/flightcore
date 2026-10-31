# Milestone 0: Project Setup

## The big picture

**What we did:** we wrote zero flight code. Instead we built the workshop the flight code will be
built in.

**Analogy:** before a company builds an aircraft, it sets up the factory: the assembly line, the
inspection stations, the test bench, the quality inspector. Nobody bolts parts together in an
empty parking lot. Milestone 0 is that factory.

**Why it matters:** from now on, every line you write gets checked automatically. Mistakes are
caught in seconds on your laptop, not weeks later in integration. Adding checks later is painful,
because you first have to fix all the old code that fails them. That's why real flight software
teams set this up first.

Here is what happens to your code once it is written:

```
 you write code
      |
      v
 1. BUILD      compiler turns code into a program   (strict: any warning = stop)
      |
      v
 2. TEST       small programs check your code works  (GoogleTest)
      |
      v
 3. SANITIZE   run again with "watchdogs" on         (catch memory bugs and race conditions)
      |
      v
 4. CI         GitHub repeats steps 1 to 3 on clean machines, every push
```

Each section below is one station in that factory.

---

## 1. Folder layout: where things live

```
flightcore/
├── include/fc/core/   headers: WHAT the code offers
├── src/core/          .cpp files: HOW it works inside
├── tests/             tests that check src/ does what include/ promises
├── cmake/             build helper files
├── docs/              these write-ups
└── .github/workflows/ the CI robot's instructions
```

**Analogy:** you already know this from defense work. A header is the **ICD (interface control
document)** for a box: the connectors, the signals, what goes in and what comes out. The `.cpp`
file is the **inside of the box**: the circuit boards. Whoever integrates the box reads the ICD
and never needs to open it up.

**Why it matters:** other code depends only on the interface. You can rewrite the inside (make it
faster, fix a bug) and nobody else has to change. That's what lets a big team work in parallel.

**Why `include/fc/core/` instead of just `include/`?** So the include reads
`#include "fc/core/version.hpp"`. The `fc/` part works like a last name. There could be a hundred
files called `version.hpp` in the world, but only one `fc/core/version.hpp`.

---

## 2. CMake: the build planner

**What it is:** CMake reads `CMakeLists.txt` and works out which files to compile, with which
settings, in which order. Then a tool called **Ninja** carries out that plan by running the
compiler.

**Analogy:** CMake is the **planner who writes the work orders**. Ninja is the **crew that does
the work**. The compiler (clang) is the **power tool** the crew uses.

**Why it matters:** with 3 files you could type the compile command by hand. With 300 files,
plus settings that change for debug vs release vs sanitizers, you can't. CMake also only rebuilds
what changed, the same way a crew only redoes the part of a job that changed.

### The key lines

```cmake
set(CMAKE_CXX_STANDARD 20)             # use C++20
set(CMAKE_CXX_STANDARD_REQUIRED ON)    # if the compiler can't do C++20, stop, don't fake it
set(CMAKE_CXX_EXTENSIONS OFF)          # standard C++ only, no compiler-specific extras
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)  # write a file that tells editors how we build (section 9)
```

### Targets: one part number per thing you build

```cmake
add_library(fc_core src/core/version.cpp)
target_include_directories(fc_core PUBLIC ...include)
```

**Analogy:** a **target** is a **part number** in a bill of materials. `fc_core` is a part (our
library). `fc_tests` is another part (the test program) that uses `fc_core`.

**PUBLIC vs PRIVATE** controls what gets passed on to whoever uses the part:

- **PRIVATE** = the **tools used to build the part**. The welding torch stays in the factory, so
  the customer never sees it. Example: our strict warning flags.
- **PUBLIC** = the **mounting spec and connectors that ship with the part**. Anyone who uses it
  needs them too. Example: the `include/` folder, so the tests can find our headers automatically.

**Why it matters:** each part carries its own requirements. When the tests say "I use `fc_core`",
they get the right include folder for free, and nobody has to copy settings around.

### The `fc::core` alias

`add_library(fc::core ALIAS fc_core)` gives the part a second name with `::` in it.

**Analogy:** like a part number with a **check digit**. If you mistype a plain name, CMake may
quietly assume it's some system library and fail in a confusing way later. If you mistype a `::`
name, CMake stops right away and says "no such part."

### One place for the version

`project(VERSION 0.1.0)` is the only place the version number is written. CMake hands it to the
C++ code. **Why:** if a number is written in two places, one day they'll disagree.

---

## 3. Warnings as errors: the pre-flight checklist

**What it is:** the compiler can warn about code that is legal but suspicious. We turned on many
warnings and made every one a hard error (`-Werror`). If there's a warning, the build fails.

**Analogy:** a **pre-flight checklist where any amber light means no-go**. A warning light on a
dashboard is easy to ignore, until you have 40 of them and miss the one that matters. With zero
allowed, every new warning is impossible to miss.

**Why it matters:** in certified flight software (DO-178C, the avionics software standard) every
warning is something you'd have to explain in writing. Teams just fix them.

The ones you'll run into most:

| Warning | Plain meaning | Example it catches |
|---------|---------------|--------------------|
| `-Wconversion` | "you're squeezing a big number into a small box" | `int x = 3.7;` silently becomes 3 |
| `-Wsign-conversion` | "you're mixing negative-capable and positive-only numbers" | `-1` turning into 4 billion |
| `-Wshadow` | "two variables have the same name, and one hides the other" | a local `speed` hiding the member `speed` |
| `-Wold-style-cast` | "use a C++ cast so the intent is clear" | `(int)x` should be `static_cast<int>(x)` |
| `-Wnon-virtual-dtor` | "this base class will leak if deleted through a base pointer" | missing `virtual ~Base()` |
| `-Wdouble-promotion` | "you made a slow double out of a float without meaning to" | costly on small embedded chips |

**The famous story:** in 1996 the Ariane 5 rocket blew up 37 seconds after launch. A 64-bit
floating-point number was converted to a 16-bit integer, the value was too big to fit, and the
guidance software crashed. That's exactly the kind of bug `-Wconversion` exists to flag.

**Why only our code?** The warnings apply per target, so GoogleTest (someone else's code) isn't
held to our rules. We only hold our own parts to our standard.

---

## 4. Sanitizers: watchdogs while the program runs

The compiler checks your code **before** it runs. Sanitizers check it **while** it runs. The
compiler adds extra checking code around every memory access and every thread operation.

| Sanitizer | Analogy | Catches |
|-----------|---------|---------|
| **ASan** (Address) | a **security guard on every fence**. Step outside your memory, an alarm goes off | reading past the end of an array, using memory after `delete`, deleting twice, leaks |
| **UBSan** (Undefined Behavior) | a **referee** for the C++ rulebook | integer overflow, bad shifts, misaligned pointers |
| **TSan** (Thread) | a **traffic camera at an intersection with no traffic light** | two threads touching the same variable at the same time, one of them writing (a "data race") |

**Why TSan is the big one for us:** this whole project is about threads. Data races are the worst
kind of bug: the code works 999 times, then fails once in the field, and you can't reproduce it.
TSan catches the race **even on runs where nothing visibly went wrong**.

**Why ASan and TSan need separate builds:** each one reorganizes how the program's memory is laid
out so it can watch it. Two guards can't both rearrange the same warehouse their own way, so each
gets its own build.

**Two extra flags:**
- `-fno-sanitize-recover=all`: stop at the first problem. Otherwise a test could print a warning
  and still pass.
- `-fno-omit-frame-pointer`: keeps the "how did we get here" trail (the stack trace) readable.

---

## 5. Presets: saved driver settings

**What they are:** `CMakePresets.json` gives a name to each set of build settings.

**Analogy:** the **memory seat buttons** in a car. Press 1 and the seat, mirrors and steering
wheel all move to your settings. You don't adjust each one by hand.

| Preset | Analogy | Use it when |
|--------|---------|-------------|
| `debug` | car in the garage, hood open, diagnostic cable plugged in | everyday coding, stepping through in a debugger |
| `release` | car on the race track, fully tuned | measuring speed and timing |
| `asan` | car on a test track with a safety inspector riding along | checking for memory bugs |
| `tsan` | the same, but the inspector watches traffic | any time you touch threads |

Each preset builds into its own folder (`build/debug`, `build/tsan`, ...). **Analogy:** separate
workbenches. Switching between them doesn't mean clearing off and setting up again.

```
cmake --preset tsan          # plan the build
cmake --build --preset tsan  # do the build
ctest --preset tsan          # run the tests
```

---

## 6. Tests: the test bench

**What it is:** GoogleTest is a library for writing small programs that check your code. Right
now there's one test. It checks that `version()` returns `"0.1.0"`.

**Analogy:** an **acceptance test procedure (ATP)** on a test bench. Before a box ships, you run
the procedure: apply known inputs, check for the expected outputs, record pass or fail.

**Why such a silly first test?** It's a **smoke test**: power the box on and make sure no smoke
comes out. It proves the library builds, links and runs under every preset. The real tests start
in Milestone 1.

**Two details:**
- **Pinned version (`v1.15.2`).** GoogleTest is downloaded automatically, always the exact same
  version. **Analogy:** test equipment with a known calibration. If the tool can change under you,
  a pass today and a fail tomorrow tell you nothing. In defense terms, this is configuration
  management.
- **`gtest_discover_tests`** lists every test separately in the report. **Analogy:** a test report
  with one line per requirement, instead of a single "box passed" stamp.

---

## 7. Formatting and static analysis: the style robot and the blueprint inspector

- **clang-format** automatically formats the code (spacing, line breaks, include order).
  **Analogy:** an auto-formatter for documents. **Why:** code reviews never argue about spacing,
  because the robot decides. Run `clang-format -i file.cpp` before you commit.

- **clang-tidy** reads the code and looks for known bug patterns. **Analogy:** an inspector
  **reviewing the blueprints** before anything is built. Sanitizers are the opposite: they watch
  the **test flight**. The two catch different things, so we use both.

---

## 8. CI: the independent inspector

**What it is:** every time you push to GitHub, GitHub Actions starts fresh machines, downloads
your code, and runs everything: build, tests, sanitizers, formatting check, clang-tidy.

**Analogy:** an **independent QA inspector** who wasn't in the room when you built it, rebuilds
it from scratch, and tests it. That kills the classic excuse "it works on my machine."

**The matrix:** 2 operating systems (Linux, macOS) x 4 presets = 8 jobs, plus 1 lint job.
**Analogy:** testing on two aircraft types in four configurations each.

**Why Linux?** Real-time Linux is where we'll measure timing precision later. **Why macOS?** It's
what you develop on, and passing on both proves the code isn't tied to one system.

That green check mark on GitHub is visible proof to anyone who looks at the repo, recruiters
included, that the code builds and passes.

---

## 9. Small local details

- **`compile_commands.json`:** an instruction sheet listing exactly how each file is compiled.
  Vim's code helper (clangd) reads it. **Analogy:** handing your assistant the build manual.
  Without it, clangd guesses, which is why you saw fake red errors earlier. This file only exists
  on your machine; it isn't in the repo.
- **noreply commit email:** commits show a GitHub address instead of your personal email.
  **Analogy:** a PO box instead of your home address on public mail.
- **`.gitignore`:** a list of files git should never save (build output, editor files, secret
  `.env` files). **Analogy:** a "do not ship" bin.
- **Small commits:** each commit is one change. **Analogy:** logbook entries. Each one says what
  changed and can be undone on its own.

---

## Try it yourself (5 minutes each)

1. **See a warning stop the build.**
   In `src/core/version.cpp`, add `int x = 3.7;` inside the function. Run
   `cmake --build --preset debug`.
   *You should see:* an error about implicit conversion. Delete the line.

2. **See ASan catch a memory bug.**
   In `tests/core/test_version.cpp`, add a test with `int a[3]; a[3] = 1;` (index 3 is past the end).
   Run `ctest --preset debug`. *It probably passes.* That's the scary part.
   Run `ctest --preset asan`. *It fails* and points to the exact line. Delete the test.

3. **See TSan catch a race condition.**
   Write a test that starts two `std::thread`s. Each adds 1 to the same plain `int` 100000 times.
   Run under `tsan`. *You should see:* "WARNING: ThreadSanitizer: data race".
   Change `int` to `std::atomic<int>`. Run again. *It's clean.*
   This is the warm-up for Milestone 1.

---

## Be ready to explain (with short answers)

**Why are warnings errors?**
So no warning is ever ignored. Zero allowed means every new one gets noticed and fixed.

**Why can't ASan and TSan be in the same build?**
Both reorganize the program's memory to watch it, in different ways that conflict.

**What's the difference between PUBLIC and PRIVATE in CMake?**
PRIVATE settings are only used to build this part. PUBLIC settings also get passed on to anything
that uses the part.

**Why pin dependency versions?**
So the same code always builds the same way. A test result means nothing if the tools changed.

**Why run CI on two operating systems?**
To prove the code is portable and doesn't depend on one machine.

**What's the difference between static analysis and a sanitizer?**
Static analysis reads the code without running it (inspecting blueprints). A sanitizer watches the
code while it runs (riding along on the test flight).
