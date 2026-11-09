# Design

## Context

Under the Level 1 dual-core architecture, Core 0 (CTRL) manages the UI, terminal, USB, and filesystem, while Core 1 (PROTO) owns hardware protocols, PIO engines, and capture engines. The newly deployed service kernel (capability `service-kernel`, B1) provides lock-free SPSC event queues, 1 ms soft timers, deferred work queues, and cooperative cancellation tokens.

The protocol job runtime builds directly on top of the service kernel, allowing Core 0 to asynchronously submit protocol tasks to Core 1 and receive streamed results without blocking the interactive UI.

## Goals / Non-Goals

**Goals:**
- Provide a structured, statically budgeted protocol job descriptor (`bp_job_t`).
- Allow Core 0 to submit jobs and continue servicing the terminal prompt, USB CDC, and status bar without blocking.
- Provide a single-active-job execution runtime on Core 1 dispatched cooperatively within `bp_service(1)`.
- Stream progress updates (0–100%) and output text chunks from Core 1 to Core 0 across lock-free rings without invoking `printf` from Core 1.
- Enforce hard deadlines with automatic timeout recovery.
- Guarantee `< 100 ms` cancellation latency on user Ctrl+C, button press, or USB disconnect.
- Provide a `job` CLI command to inspect job state and run simulated long-running protocol jobs.

**Non-Goals:**
- Multi-job preemption or concurrent multi-protocol execution (Core 1 executes one active protocol job at a time).
- Migrating all legacy mode commands in one step (individual mode conversions happen in Phase C).
- Hardware peripheral dynamic lease negotiation (addressed in B3 `add-resource-lease-manager`).

## Decisions

### 1. Single Active Job Runtime with Static Descriptor
- **Decision**: Manage protocol jobs through a statically budgeted `bp_job_t` context pool (1 active job, 1 queued job).
- **Rationale**: Hardware protocol engines (PIO, SPI, I2C, UART) on Core 1 are physically single-instance resources at any given time. A single active job model eliminates the need for dynamic heap allocation or complex preemptive scheduling while satisfying all CTF and hardware probe requirements.
- **Alternative considered**: Multi-priority preemptive job queue. Rejected due to heap allocation overhead and race hazards on hardware pins.

### 2. State Machine: Strictly Monotonic Transitions
- **Decision**: Define job lifecycle states as:
  `BP_JOB_IDLE` -> `BP_JOB_QUEUED` -> `BP_JOB_RUNNING` -> `BP_JOB_DONE` | `BP_JOB_CANCELLED` | `BP_JOB_TIMEOUT` | `BP_JOB_ERROR`.
- **Rationale**: State transitions are atomic and monotonic, allowing Core 0 to poll job status lock-free with acquire/release memory barriers.

### 3. Asynchronous Progress & Output Streaming
- **Decision**: Stream progress and output chunks via a dedicated SPSC event channel:
  ```c
  typedef struct bp_job_progress {
      uint32_t job_id;
      uint8_t  percent;
      char     status_msg[32];
  } bp_job_progress_t;
  ```
- **Rationale**: Keeps Core 1 strictly isolated from console I/O (`printf` / `usb_tx`), avoiding cross-core deadlocks while providing real-time visual progress on the terminal.

### 4. Cancellation & Timeout Handshake
- **Decision**: Core 0 requests cancellation via `bp_job_cancel(job_id)`. The job executor on Core 1 checks `bp_job_check_cancelled()` at every loop slice. If the job exceeds its `deadline_ms`, Core 1 marks it as `BP_JOB_TIMEOUT` and rolls back peripheral state.
- **Rationale**: Enforces deterministic termination within <100 ms while ensuring hardware state (pins, pull-ups, power) is released safely.

## Risks / Trade-offs

- **[Risk] Uncooperative Legacy Protocol Loops**: A legacy protocol driver that blocks in a `while (!flag)` loop could delay cancellation.
  - *Mitigation*: Protocol job handlers must call `bp_job_check_cancelled()` inside inner polling loops, bounded by the hardware watchdog (8000 ms).
- **[Risk] Output FIFO Overflow**: High-rate protocol sniffers could produce output faster than the terminal drains.
  - *Mitigation*: Bounded output ring with drop counter. Core 0 prints dropped chunk counts if backpressure occurs.

## Migration Plan

1. Implement `src/kernel/bp_job.h` and `src/kernel/bp_job.c`.
2. Integrate `bp_job_service()` into Core 1 cooperative loop (`bp_service(1)`).
3. Implement `tests/test_job.c` and `tests/test_job.sh` covering submission, execution, cancellation, and timeout recovery.
4. Add `cmd_job.c` / `cmd_job.h` and register the `job` command.
5. Verify zero dynamic heap usage with `tests/test_memory_model.sh` and compile `bus_pirate6.uf2`.
