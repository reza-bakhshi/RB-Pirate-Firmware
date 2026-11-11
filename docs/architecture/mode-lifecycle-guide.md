# Mode lifecycle

Mode configuration and hardware setup/cleanup currently run on Core 0 in
`src/mode/bp_mode.c`; see [core ownership](dual_core_guide.md).

`bp_mode_switch()` rejects changes while a job is queued, running or finishing.
Cancel the job and let Core 0 complete its cleanup before switching. The command
reports BUSY instead of freeing state still used by Core 1.

For an idle engine, switching cleans the outgoing mode, releases its leases,
floats BIO pins, prompts/configures the new mode and activates hardware. A failed
setup falls back to HiZ. A successful change posts events to both cores.
This is not an acknowledged Core 1 teardown/init mailbox protocol.

Jobs that own driver state use `bp_job_dispatch_with_cleanup()`. Core 1 stops
slicing and publishes FINISHING; `bp_job_finalize()` on Core 0 runs cleanup,
releases leases and publishes the terminal state. This keeps IRQ teardown on the
core that installed the IRQs. Cleanup must not block on Core 1 or submit a new job.
Resource writes are serialized with an SDK hardware spinlock; initialize the
resource manager at boot before launching Core 1. Host tests use an atomic lock.

Host mode tests cover rollback, active-job rejection and repeated switches.
Electrical pin state, PIO timing and cancellation under USB load require hardware.
