# Cooperative Service Kernel Architecture

> Developer guide to the RB-Pirate cooperative kernel: event dispatching, software timers, work queues, job leases, and resource ownership tracking.

---

## 1. Overview

The RB-Pirate kernel (`src/kernel/`) provides a deterministic, tick-less cooperative multitasking runtime for the control plane (Core 0). It replaces unbounded delays and ad-hoc state loops with bounded execution slices and explicit resource ownership.

```
+-------------------------------------------------------------------------+
|                         COOPERATIVE DISPATCHER                          |
+-------------------------------------------------------------------------+
|  [Event Queue]  --->  [Software Timers]  --->  [Deferred Work Queue]   |
+-------------------------------------------------------------------------+
                                     |
                                     v
+-------------------------------------------------------------------------+
|                         PROTOCOL JOB ENGINE                             |
+-------------------------------------------------------------------------+
|  - Bounded Execution Slices (1ms - 50ms)                                |
|  - Asynchronous Cancellation (Ctrl+C, Timeout, Hardware Trip)          |
|  - Automatic Resource Lease Tracking & Teardown (PIO, DMA, IRQ, Buffers)|
+-------------------------------------------------------------------------+
```

---

## 2. Core Kernel Subsystems

### 2.1 Event Queue (`bp_event.c`)
- Fixed-capacity circular queue for cross-subsystem notifications (button presses, storage hotplug, PSU alerts).
- Dispatched in FIFO order with priority handling for emergency trip events.

### 2.2 Software Timers (`bp_timer.c`)
- Delta-list managed repeating and one-shot software timers driven by microsecond timestamps.
- Zero dynamic allocation; uses a fixed pool of timer descriptors.

### 2.3 Deferred Work Queue (`bp_work.c`)
- Offloads non-critical processing from ISRs and rapid polling loops.
- Workers run to completion within bounded execution budgets.

### 2.4 Protocol Job Engine (`bp_job.c`)
- Encapsulates long-running protocol operations (flash dumps, logic analyzer traces, sniffer sessions, RF transmissions).
- Jobs execute via cooperative slices (`job_slice_fn`).
- Supports clean cancellation via `bp_job_cancel(job_id)` or user `Ctrl+C`.
- Maintains monotonic execution metrics accessible via the `job` CLI command.

### 2.5 Resource Tracker (`bp_resource.c`)
- Tracks ownership of scarce hardware resources: PIO state machines, DMA channels, hardware IRQ slots, and large SRAM buffers.
- Guarantees that when a job completes, errors, or is cancelled, all claimed peripherals are safely freed back to the system pool without leaks.
