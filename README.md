# flightcore

A mini real-time flight software framework in C++20.

Planned: rate-group scheduler, lock-free software bus with zero heap allocation after init,
RAII thread control, simulated vehicle with Kalman-filter attitude estimation and PID control,
and watchdog-driven fault management.

Status: Milestone 0 (build, test, CI).

## Build

```
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

Presets: `debug`, `release`, `asan`, `tsan`. Requires CMake 3.25+, Ninja, and a C++20 compiler.
