# Dual-core architecture

The current firmware has a hybrid core layout. USB has not migrated to Core 0.
Protocol jobs share Core 1 with USB and monitoring, so a slow slice delays those services.

| Work | Current owner | Source |
| --- | --- | --- |
| CLI, command parsing, syntax execution, mode setup/cleanup | Core 0 | `pirate.c`, `syntax_run.c`, `mode/bp_mode.c` |
| Service storage RPC and config files | Core 0 | `services/bp_storage.c` |
| TinyUSB task, CDC RX/TX, MSC callbacks | Core 1 | `pirate.c:core1_infinite_loop` |
| AMUX/PSU monitoring, toolbar updates | Core 1 | `pirate.c:core1_infinite_loop` |
| Asynchronous protocol job slices | Core 1 | `kernel/bp_kernel.c`, `kernel/bp_job.c` |
| Watchdog supervisor, CLI snapshot service | Core 0 | `pirate.c`, `kernel/bp_kernel.c` |

## Writing protocol jobs

Keep every slice bounded. The scheduler checks cancellation and deadlines between slices;
it cannot preempt a blocking callback. Use `bp_job_dispatch_with_cleanup()` when a job
owns driver state or loaded PIO programs. Cleanup runs exactly once on Core 0 for success,
error, cancellation (including before the first slice), or timeout. Core 1 publishes FINISHING and stops slicing; `bp_job_finalize()` on Core 0 disables
the IRQs on their setup core. Cleanup runs before generic
lease release and before a terminal state becomes visible to Core 0. Cleanup must be
bounded and must not submit a replacement job or wait on the control core.

Use the Core 1 text ring for output. The printf character sink routes Core 1 text through
`bp_term_core1_putc()`; Core 0 drains it into the terminal TX queue. Saturation can drop text.
The binary TX ring has one consumer: the USB service on Core 1, including disconnect cleanup.

## Switching modes

`bp_mode_switch()` currently runs cleanup, pin reset, configuration and hardware setup on
Core 0, with rollback to HiZ on failure. It posts mode-change events after successful setup.
It does not implement an acknowledged Core 1 teardown/init mailbox transaction.
Do not switch modes while a protocol job is still running.

## Remaining isolation work

Moving USB to Core 0 requires changing its initialization/IRQ ownership, FIFO producers and
consumers, MSC storage serialization, and blocking CLI wait paths together. Moving monitoring
also requires ADC/PSU ownership validation. These changes require hardware tests with USB
traffic, capture load, cancellation, PSU faults, and repeated mode transitions. A passing host
suite does not establish real-time isolation or electrical protocol correctness.
