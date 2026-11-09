# Proposal

## Why

The current Bus Pirate firmware executes Core 0 and Core 1 via ad-hoc, unbounded polling loops interspersed with blocking delays (`busy_wait_ms`), uncoordinated state flags, and direct hardware register polling. This design causes several failure modes:
1. **Interactive Starvation and Latency**: Polling loops starve UI responsiveness during background tasks or USB storage interactions.
2. **Missing Inter-core Signaling**: Cores cannot cleanly coordinate or wake each other without spinning or blocking the hardware SIO FIFO.
3. **No Cancellation Contract**: Long-running operations (frequency measurements, flash dumps, bus scans, protocol sniffs) cannot be cleanly aborted by user Ctrl+C, button presses, or USB disconnections, leading to watchdog timeouts or unresponsive terminals.
4. **Lack of Unified Status and Logging**: Subsystems return inconsistent bools or ints, and Core 1 cannot log diagnostic messages without risking race conditions or deadlocks.

Introducing a deterministic, statically budgeted dual-core cooperative service kernel (`service-kernel`) provides the necessary event dispatch, soft timers, deferred work queues, and cancellation infrastructure required for Level 1 architecture.

## What Changes

- **Static Event Subsystem (`src/kernel/bp_event.h/.c`)**: Fixed-size, statically allocated SPSC event queues allowing zero-allocation event posting and dispatch between ISRs, Core 0, and Core 1.
- **Deterministic 1 ms Soft Timers (`src/kernel/bp_timer.h/.c`)**: Statically budgeted delta-timer list ticked by hardware timer/repeating tick, executing non-blocking callbacks in the cooperative loop.
- **Deferred Work Queue (`src/kernel/bp_work.h/.c`)**: Cooperative task scheduling for deferred background processing without dynamic allocation.
- **Kernel Runner and Service Loop (`src/kernel/bp_kernel.h/.c`)**: Cooperative dispatcher (`bp_kernel_run()`, `bp_service()`) implementing bounded idle sleep (`__wfe()`) and cross-core event signaling (`__sev()`).
- **Unified Status Architecture (`src/kernel/bp_status.h`)**: Standardized error codes and status returns across all subsystems.
- **Asynchronous Safe Log Ring (`src/kernel/bp_log.h/.c`)**: Statically allocated lock-free ring buffer for leveled logging safe for Core 1 and ISR producers with Core 0 console drainage.
- **Health and Metric Tracking (`src/kernel/bp_health.h/.c`)**: Kernel dispatch metrics, event high-water marks, timer overruns, and loop latency tracking.
- **Cancellable Long-Operation Contract (`src/kernel/bp_op.h/.c`)**: Cooperative cancellation tokens checked by long operations, triggered by Ctrl+C, button events, or USB disconnects within <100 ms latency.
- **Main Loop Conversion**: Migrate Core 0 and Core 1 infinite loops in `src/pirate.c` to use `bp_kernel_run()` and kernel service slices.
- **Diagnostic CLI Command**: Add `kernel` (`kernel stats`) command to inspect queue depths, timer allocations, and event dispatch counts.
- **Host Unit Test Suite**: Comprehensive tests in `tests/test_kernel.sh` verifying events, timers, work queues, and cancellation latency.

## Capabilities

### New Capabilities
- `service-kernel`: Cooperative event dispatch, deterministic 1 ms soft timers, deferred work queue, unified status codes, safe log ring, and cancellable operation tokens with zero heap allocations.

### Modified Capabilities
<!-- None -->

## Impact

- **Affected Systems**:
  - `src/pirate.c`: Refactor `core0_infinite_loop()` and `core1_infinite_loop()` to use the service kernel.
  - `src/kernel/`: Add kernel headers and implementation files.
  - `src/commands/global/`: Add `cmd_kernel.c` and `cmd_kernel.h`.
  - `src/commands.c`: Register the `kernel` command.
  - `src/CMakeLists.txt`: Add kernel sources to build targets.
- **Resource & Memory Model**:
  - Zero dynamic heap allocation.
  - Static memory budget strictly within RP2350 <= 380 KB limit.
  - Core stacks remain budgeted at 4096 bytes in dedicated scratch SRAM.
