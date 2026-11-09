# Proposal

## Why

Currently, long-running protocol operations (such as frequency measurements, I2C bus scans, flash dumps, SubGHz/IR packet captures, and RF sweeps) block Core 0 execution. This freezes the terminal prompt, pauses status bar updates, prevents concurrent command execution, and risks watchdog resets if an external slave device hangs the bus. Core 0 must be able to dispatch protocol work to Core 1 asynchronously while remaining interactive, streaming progress, and enforcing deterministic timeouts.

## What Changes

- **Protocol Job Descriptor (`src/kernel/bp_job.h/.c`)**: Define `bp_job_t` with statically budgeted parameter envelopes, unique job IDs, execution states (`IDLE`, `QUEUED`, `RUNNING`, `DONE`, `CANCELLED`, `TIMEOUT`, `ERROR`), and completion statuses.
- **Core 1 Job Dispatcher (`src/kernel/bp_job.c`)**: Integrate job dispatching into Core 1's `bp_service(1)` cooperative slice, ensuring one active protocol job runs without blocking Core 0.
- **Progress & Output Event Streaming**: Stream job progress percentages and output records from Core 1 to Core 0 across lock-free SPSC rings, eliminating cross-core `printf` calls.
- **Deadline & Timeout Enforcement**: Built-in timeout timers that automatically abort overdue jobs and recover Core 1 protocol state cleanly.
- **Bounded Cancellation Latency**: Check cooperative cancellation tokens at slice boundaries, guaranteeing abortion in `< 100 ms` upon Ctrl+C, button press, or USB disconnect.
- **Diagnostic CLI Command**: Add `job` command (`src/commands/global/cmd_job.c`) supporting `job run [ms]`, `job status`, and `job cancel` to inspect job runtime state on hardware.
- **Host Unit Test Suite**: Comprehensive tests in `tests/test_job.sh` validating submission, asynchronous execution, progress updates, timeout recovery, and cancellation latency.

## Capabilities

### New Capabilities
- `protocol-jobs`: Asynchronous submission, execution, cancellation, and progress streaming for protocol operations across the Core 0 / Core 1 boundary with zero dynamic memory allocation.

### Modified Capabilities
<!-- None -->

## Impact

- **Affected Systems**:
  - `src/kernel/`: Add `bp_job.h` and `bp_job.c`.
  - `src/kernel/bp_kernel.c`: Hook job servicing into `bp_service(1)`.
  - `src/commands/global/`: Add `cmd_job.c` and `cmd_job.h`.
  - `src/commands.c`: Register the `job` command.
  - `src/CMakeLists.txt`: Add job sources to build targets.
- **Memory & Resource Budget**:
  - Zero dynamic heap allocation (`malloc`, `free`, `new`, `delete`).
  - Statically allocated job context pool within RP2350 <= 380 KB static RAM budget.
