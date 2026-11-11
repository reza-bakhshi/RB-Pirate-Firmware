# Dual-core architecture

Core 0 owns USB and the control service. Core 1 executes protocol jobs, syntax
bytecode and BPIO data transactions. USB and monitoring no longer run in the worker loop.

| Work | Owner |
| --- | --- |
| TinyUSB initialization, task/IRQs, CDC endpoints and MSC callbacks | Core 0 |
| CLI, parsing, mode configuration/lifecycle, FlatCC response building | Core 0 |
| Terminal transport, UART ingress staging, toolbars, periodic monitoring | Core 0 |
| FatFS/storage RPC, watchdog supervisor, job finalization | Core 0 |
| Syntax execution, BPIO data handlers, asynchronous protocol job slices | Core 1 |
| Mode-specific management commands and hardware capture setup | Core 0 |

## Control polling

`bp_control_poll()` checks the executing core and suppresses nested calls. Its startup
hook in `pirate.c` serves USB, UART/RTT ingress, queued output, toolbars and monitoring.
`bp_service(0)` invokes it, as do blocking terminal/binary waits and output backpressure
paths. The pump never parses another command or BPIO packet.

UART IRQs write to a separate staging queue. The control pump is the single producer
of terminal RX; a UART interrupt cannot interrupt a USB enqueue into the same queue.
Core 1 reads routed job input instead of competing for the CLI RX queue. Worker console
text is buffered by `bp_term_core1_putc()` and drained with a bounded control-core budget.

The pump skips USB/MSC callbacks while a block-device transfer holds its lock, preventing
recursive filesystem entry. Background ADC checks use a nonblocking ownership attempt;
they skip a sample if either core already owns the ADC, including a Core 0 command.

Legacy timer, toolbar and RGB requests now execute directly on Core 0. Only the startup
handshake still uses the synchronous core FIFO, so these requests cannot wait behind a
long-running protocol slice.

## Calling the protocol worker

`bp_protocol_call()` submits a job from Core 0, keeps `bp_service(0)` running while it
waits, and returns after Core 1 has finished using the request/result buffers. It never
falls back to executing the transaction on Core 0. Syntax compilation/postprocessing
and FlatBuffer construction stay on Core 0; the transfer callbacks run on Core 1.
Mode periodic hooks are suppressed while a job is active so they cannot consume the
same peripheral data concurrently.

Callbacks must cooperate with cancellation and return. The synchronous caller retains
its context until completion; it cannot safely abandon buffers while the worker still
uses them. A permanently blocked worker is handled by the existing watchdog, not by
freeing its buffers. Syntax repeat/delay loops check cancellation and update heartbeats.

## Driver teardown and mode changes

`bp_job_dispatch_with_cleanup()` publishes FINISHING after the worker stops slicing.
Core 0 runs driver cleanup, releases leases and publishes terminal state. This keeps
IRQ teardown on the core that installed the IRQs. Mode changes are rejected while a
job is queued, running or finishing. Mode setup/cleanup remains on Core 0 with HiZ
rollback; there is no claim that every legacy mode-management function migrated.

## Validation limits

Host tests exercise the real control dispatcher/kernel and job engine with threads,
including a blocked worker, wrong-core requests, reentrant polling and 1,000 result
handoffs. Build-time checks prevent USB/monitoring from returning to worker startup
or its main loop. These checks do not establish electrical timing or real-time latency.

The control core is cooperative. Legacy code that neither returns nor enters an I/O
wait/poll path can still delay its own services. New long-running protocol work must
use the worker interface. USB enumeration, CDC/MSC concurrency, watchdog recovery,
PSU protection and capture operation require qualification on a physical device.
