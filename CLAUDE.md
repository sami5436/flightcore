# flightcore

C++20 mini flight software framework: rate-group scheduler, lock-free software bus,
RAII thread control, GNC sim, fault management. Learning and portfolio project.

## Working mode
Claude builds, Sami learns from the docs.
- Claude writes all the code, core pieces included, with tests. Do not hand Sami
  implementation tasks or stop to quiz.
- Each milestone ships as a branch and PR, with the milestone doc in the same PR. Claude merges
  its own PRs once CI is green (merge commit, no squash or force-push).
- Milestone docs live in `docs/milestoneN_<topic>.md` and walk through the real code with
  snippets. Write them in plain language: for each concept say what it is, give an everyday or
  defense/aerospace analogy, and say why it matters. End with hands-on exercises and
  short-answer interview questions so Sami can explain the code back, interview style.
- Sami reads code in vim. Tell Sami which files changed so they can reload them.

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
