# flightcore

C++20 mini flight software framework: rate-group scheduler, lock-free software bus,
RAII thread control, GNC sim, fault management. Learning and portfolio project.

## Pairing mode
Sami and Claude pair program, swapping driver and navigator.
- Claude drives: build system, CI, sim plumbing, boilerplate.
- Sami drives: core pieces (RAII wrappers, ring buffer, scheduler loop). Claude writes the
  interface and failing tests, Sami implements, Claude reviews. Do not write these for Sami
  unless Sami asks.
- Each milestone ends with Sami explaining the code back, interview style.
- Sami edits in vim. Tell Sami which files changed so they can be reloaded them.
- Milestone docs live in `docs/milestoneN_<topic>.md`. Write them in plain language: for each
  concept say what it is, give an everyday or defense/aerospace analogy, and say why it matters.
  End with hands-on exercises and short-answer interview questions.

## Build, test
```
cmake --preset debug && cmake --build --preset debug && ctest --preset debug
```
Presets: `debug`, `release`, `asan` (ASan + UBSan), `tsan`. Run `tsan` for any threading change.
Format: `clang-format -i` on touched files. CI runs clang-format and clang-tidy.

## Layout
- `include/fc/<module>/` public headers, `src/<module>/` sources, `tests/<module>/` tests
- `cmake/` warnings and sanitizer modules
- Namespace `fc::<module>`

## Rules
- Warnings are errors. No exceptions or heap allocation on real-time paths after init.
- Every concurrency primitive gets a TSan-clean stress test.
- Small commits, one logical change each.
- No code, data or names from Sami's employer. Personal project only.
