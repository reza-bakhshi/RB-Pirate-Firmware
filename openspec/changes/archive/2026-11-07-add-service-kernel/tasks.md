# Tasks

## 1. Kernel Primitives and Data Structures

- [x] 1.1 Add `src/kernel/bp_status.h` defining unified subsystem status codes (ARCH-104) and verify header syntax.
- [x] 1.2 Implement `src/kernel/bp_event.h` and `src/kernel/bp_event.c` (ARCH-100) with fixed-size SPSC ring queues and zero heap allocation.
- [x] 1.3 Implement `src/kernel/bp_timer.h` and `src/kernel/bp_timer.c` (ARCH-101) with deterministic 1 ms soft timers and non-blocking callback dispatch.
- [x] 1.4 Implement `src/kernel/bp_work.h` and `src/kernel/bp_work.c` (ARCH-102) with statically budgeted deferred work queues.
- [x] 1.5 Implement `src/kernel/bp_op.h` and `src/kernel/bp_op.c` (ARCH-107) with cooperative cancellation tokens and <100 ms latency abort checks.
- [x] 1.6 Implement `src/kernel/bp_log.h` and `src/kernel/bp_log.c` (ARCH-105) with safe lock-free leveled log ring for Core 1 / ISR producers.
- [x] 1.7 Implement `src/kernel/bp_health.h` and `src/kernel/bp_health.c` (ARCH-106) tracking event throughput, timer overruns, and high-water utilization.

## 2. Kernel Engine, Unit Tests, and Build Integration

- [x] 2.1 Implement `src/kernel/bp_kernel.h` and `src/kernel/bp_kernel.c` (ARCH-103) providing `bp_kernel_init()`, `bp_service()`, and `bp_kernel_run()` with idle sleep and inter-core signaling.
- [x] 2.2 Add host unit test suite `tests/test_kernel.c` and `tests/test_kernel.sh` (ARCH-112) covering event queues, timers, deferred work, and cancellation latency <100ms.
- [x] 2.3 Integrate `src/kernel/*.c` into `src/CMakeLists.txt` and verify target builds and passes `tests/test_memory_model.sh`.

## 3. Main Loop Migration, Cancellation Plumbing, and CLI

- [x] 3.1 Convert Core 0 main loop in `src/pirate.c` to use `bp_kernel_run()` and kernel service slices (ARCH-108).
- [x] 3.2 Convert Core 1 loop in `src/pirate.c` to dispatch kernel services with idle sleep and watchdog heartbeats (ARCH-109).
- [x] 3.3 Connect Ctrl+C, user button press, and USB CDC disconnect to the kernel cancellation token (ARCH-110).
- [x] 3.4 Add diagnostic CLI command `kernel` (`kernel stats` and `kernel cancel-test`) in `src/commands/global/cmd_kernel.c` (ARCH-111, ARCH-113) and register in `src/commands.c`.
- [x] 3.5 Run all host test suites, compile `bus_pirate6.uf2`, and verify static RAM usage is <= 380 KB with zero runtime heap usage.
