# Multicore Process Model

> Target execution model for Level 1.  
> RP2350 has two cores, no MMU, and timing-sensitive PIO. This model uses lightweight
> **services (actors)**, not Unix-style processes and not one stack per feature.

---

## 1. What “process” means here

A **service** (actor) is:

- a state struct,
- a mailbox (fixed-size message queue),
- a bounded `step()` function,
- an owner name for its resources.

A service never calls another service directly. It sends a message. A service never blocks waiting
for another service; it receives a reply message later or times out.

This is the multicore equivalent of a process, without the MMU, context-switch cost or per-process
stack.

---

## 2. Core assignment

### Core 0 — CTRL

| Service | Responsibility |
|---|---|
| `usb_svc` | TinyUSB CDC0/CDC1/MSC, USB descriptors, endpoint state |
| `term_svc` | terminal TX/RX rings, VT100, toolbars, drop policy |
| `shell_svc` | command parsing, prompts, command dispatch |
| `mode_svc` | mode lifecycle, mode start/stop RPC |
| `job_svc` | submit/cancel/monitor protocol jobs |
| `storage_svc` | FatFS, SD card, MSC policy, hotplug state machine |
| `config_svc` | persisted config, runtime config, UI snapshot |
| `monitor_svc` | ADC/AMUX sampling, PSU safety, pin voltage snapshot |
| `watchdog_svc` | heartbeats, fault record, safe mode, reset decision |

### Core 1 — PROTO

| Service | Responsibility |
|---|---|
| `proto_svc` | protocol engine, mode-specific read/write/start/stop |
| `capture_svc` | FALA, logic analyzer, LCDTAP capture, DMA buffers |
| `binary_svc` | SUMP/BPIO/legacy/IRToy/CH32V003 engines |
| `irq_svc` | PIO/DMA/GPIO IRQ handling and handoff |

---

## 3. Scheduler

Each core runs a small cooperative scheduler:

```c
for (;;) {
    for (service in ready_services) {
        service->step(service, now_ms);
    }
    process_messages();
    feed_watchdog_if_healthy();
}
```

Rules:

- `step()` must return within ~1 ms.
- A long operation is a state machine, not a blocking loop.
- A service may have a deadline; the scheduler reports overruns.
- Control services run before data services on Core 0.
- Protocol jobs run one at a time on Core 1; captures may use DMA in parallel.

No preemption is required for correctness. If a service repeatedly overruns, it is split or moved.

---

## 3.1 Memory model

New services and new mode code use **no heap**:

- static arrays for service state and queues,
- fixed block pools for messages, packets and protocol objects,
- bump arenas for job-scoped temporary data,
- SPSC rings for inter-core byte/word streams,
- placement new into static storage for C++ objects.

`malloc`, `calloc`, `realloc`, `free`, `new` and `delete` are forbidden in new code. Existing heap
sites are migrated to pools/arenas. See [memory-model.md](memory-model.md) for the pool/arena design,
known heap sites and enforcement.

## 4. Communication primitives

| Primitive | Use |
|---|---|
| **Control message** | fixed-size event with a type and small payload; no pointers |
| **Data ring** | SPSC byte/word ring for terminal, binary packets, capture blocks |
| **Buffer pool** | fixed blocks with ownership tokens; zero-copy handoff |
| **Seqlock snapshot** | read-only status (ADC, PSU, UI state) produced by one core |
| **Request/reply** | control RPC with a timeout and a correlation id |

Forbidden:

- Shared mutable structs without a snapshot or lock.
- Cross-core function calls.
- Pointers in messages that outlive the sender's stack frame.
- Blocking waits without a timeout.

---

## 5. Example: a protocol command

```
shell_svc (Core 0)
  → job_svc.submit(CMD_SUBGHZ_SCAN, args)
  → proto_svc (Core 1) receives job
  → proto_svc sends JOB_PROGRESS / JOB_OUTPUT
  → term_svc (Core 0) drains text ring and prints
  → proto_svc sends JOB_DONE
  → shell_svc returns the prompt
```

The shell never blocks. Ctrl+C is a message to `job_svc`; `proto_svc` checks a cancel flag at safe
points. If Core 1 does not acknowledge within a grace period, `watchdog_svc` resets or restarts it.

---

## 6. Why not FreeRTOS initially

FreeRTOS is a valid later option, but it adds:

- per-task stacks (RAM pressure),
- priority inversion and tick latency,
- TinyUSB and PIO integration work,
- harder timing analysis for protocol modes.

The service model gives the same isolation with less risk. If a service needs preemption later, it
can be moved to an RTOS task without changing the message contracts.

---

## 7. Level 1 deliverables

1. `bp_kernel` service scheduler and message queue.
2. `bp_job` runtime and cancellation.
3. `bp_resource` leases for PIO/SM/DMA/IRQ/pins/buffers.
4. Terminal and binary transport ownership moved to Core 0.
5. Storage, config and monitor state moved to their owning services.
6. Core 1 protocol/capture services with heartbeat.
7. Watchdog and fault record.
8. A test job that proves Core 0 stays responsive.

This is the architecture foundation. Feature parity work (Level 4) happens after this model is live.
