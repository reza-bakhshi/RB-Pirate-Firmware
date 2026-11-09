# Design

## Context

The RP2350B microcontroller runs two ARM Cortex-M33 cores at 150 MHz. Under the Level 1 architecture plan (see `openspec/ROADMAP.md` Phase B):
- **Core 0 (CTRL)** owns UI/terminal, USB device stack (interim host during B1–B3), filesystem, and system supervision.
- **Core 1 (PROTO)** owns hardware protocols, PIO engines, and capture DMA.

All runtime memory must obey the static memory model (capability `memory-model`, B0): no dynamic heap allocations (`malloc`, `free`, `new`, `delete`), bounded static budgets (<= 380 KB static RAM), and dedicated 4096-byte stacks per core in `SCRATCH_X` / `SCRATCH_Y`.

## Goals / Non-Goals

**Goals:**
- Provide a deterministic, cooperative kernel runtime on both cores.
- Implement zero-allocation SPSC event queues (`bp_event_t`) for inter-core and ISR signaling.
- Implement 1 ms soft timers (`bp_timer_t`) with delta chaining.
- Implement cooperative deferred work queues (`bp_work_t`).
- Implement an atomic cooperative cancellation token system (`bp_op_t`) guaranteeing abort latency < 100 ms.
- Implement a lock-free leveled log ring (`bp_log_t`) safe for Core 1 / ISR producers.
- Convert `core0_infinite_loop()` and `core1_infinite_loop()` to dispatch through `bp_kernel_run()`.
- Provide runtime inspection via `kernel stats`.

**Non-Goals:**
- Preemptive RTOS context switching or task stacks.
- Protocol job engine and parser separation (deferred to B2 `add-protocol-job-runtime`).
- Dynamic resource leasing and arbitration (deferred to B3 `add-resource-leases`).
- Moving `tud_task()` off Core 1 (scheduled for B4/B7).

## Decisions

### 1. SPSC Lock-Free Event Ring vs Mutex-Guarded Queues
- **Decision**: Use lock-free Single-Producer Single-Consumer (SPSC) rings with memory barriers for core-to-core and ISR-to-core queues, sized statically to powers of two (e.g. 32 entries).
- **Rationale**: Guarantees bounded O(1) enqueue/dequeue time without spinlock contention or priority inversion between cores.
- **Alternative considered**: Mutex-guarded circular queues. Rejected due to cross-core spinlock lockup risks during interrupts.

### 2. Event Representation: Fixed 16-Byte Envelope
- **Decision**: Standardize `bp_event_t` as a 16-byte struct:
  ```c
  typedef struct bp_event {
      uint16_t id;
      uint16_t source;
      uint32_t timestamp_ms;
      union {
          uint32_t u32[2];
          void* ptr;
          int32_t i32[2];
      } data;
  } bp_event_t;
  ```
- **Rationale**: Compact enough for efficient ring buffering while carrying sufficient metadata for UI events, buttons, USB state changes, and timer triggers without dynamic heap allocation.

### 3. Delta-List Soft Timers vs Hardware Alarms
- **Decision**: Implement soft timers as a static linked delta-list ticked by a single 1 ms system tick timer.
- **Rationale**: RP2350 hardware alarm pools have limited hardware channels. Soft timers allow dozens of concurrent timers (timeouts, debounces, UI blinks) multiplexed over a single tick.

### 4. Cooperative Cancellation Architecture
- **Decision**: Implement `bp_op_t` with an atomic cancellation flag. Long operations periodically call `bp_op_check_cancelled()`.
- **Rationale**: Preemption cannot safely unwind C hardware peripheral drivers. Cooperative checks at slice boundaries ensure clean hardware release and state rollback within 100 ms.

### 5. Idle Sleep & Inter-Core Wakeup
- **Decision**: When no events, timers, or work are ready, cores enter low-power wait using `__wfe()`. When a producer on Core 1 enqueues an event for Core 0 (or vice versa), it issues `__sev()` to wake the peer.
- **Rationale**: Eliminates busy polling while keeping event delivery latency under 1 microsecond.

## Risks / Trade-offs

- **[Risk] Cooperative Handler Blocking**: A long-running command handler could starve event dispatch.
  - *Mitigation*: The watchdog supervisor (8000 ms timeout) monitors heartbeats. Cancellable operations must poll `bp_op_check_cancelled()` at least every 10 ms.
- **[Risk] Log Ring Buffer Overrun**: High burst logging from Core 1 could drop entries.
  - *Mitigation*: Bounded ring buffer with drop counter. When full, oldest entries are overwritten and an overflow counter is exposed in `kernel stats`.

## Migration Plan

1. Create `src/kernel/` headers and implementations (`bp_status.h`, `bp_event`, `bp_timer`, `bp_work`, `bp_op`, `bp_log`, `bp_health`, `bp_kernel`).
2. Add comprehensive host unit tests in `tests/test_kernel.sh` verifying all data structures and cancellation latency.
3. Integrate kernel sources into `src/CMakeLists.txt` and verify zero-heap static memory compliance via `tests/test_memory_model.sh`.
4. Refactor `core0_infinite_loop()` and `core1_infinite_loop()` to use `bp_kernel_run()`.
5. Register and test `kernel stats` command.
