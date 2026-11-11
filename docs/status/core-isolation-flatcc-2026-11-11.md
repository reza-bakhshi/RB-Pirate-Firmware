# Core isolation and FlatCC follow-up — 2026-11-11

This supersedes the core/FlatCC limitations in the earlier reliability review.
It records implementation and host verification, not physical-device qualification.

## Implemented ownership

- Core 0 initializes TinyUSB and owns its IRQs, task, CDC transport, MSC callbacks,
  UART/RTT ingress, toolbars, background ADC/PSU supervision and storage.
- Core 1 executes asynchronous jobs, syntax bytecode and BPIO data handlers. Request
  and result buffers remain valid until the worker acknowledges completion.
- Control polling continues during worker calls and bounded I/O waits. It rejects
  wrong-core entry, suppresses recursion and never parses a second command/frame.
- UART IRQs use a staging queue. Worker input is routed separately, and each new job
  clears the previous job's unconsumed input before its queued state is published.
- Periodic peripheral hooks and conflicting binary-mode changes are suppressed while
  the worker owns a transaction. BPIO may answer status but rejects concurrent mutations.
- ADC polling skips an already-owned ADC; a busy storage transfer prevents nested
  USB/MSC entry. Legacy toolbar/timer/RGB requests no longer round-trip through Core 1.
- Cross-core job state/progress and output-ring publication use explicit atomics.

Mode-specific management commands and DMA capture setup remain on the control core.
The control loop is cooperative: legacy code that does not yield can delay its own
services. These changes isolate the worker loop; they do not introduce an RTOS or
hardware memory protection between cores.

## FlatCC memory

The builder, emitter, refmap, JSON backends and aligned finalization use fixed pools:
8 x 256 bytes, 4 x 1024 bytes and 2 x 4096 bytes, with stable addresses. There is no
libc allocation fallback. Wrong-core allocation is rejected. Overflow, exhausted pools
and unsupported sizes fail without replacing an existing realloc allocation.

BPIO clears the old builder before setup/reinitialization and validates callback
buffer results. Related request validation now uses the decoded packet length, rejects
invalid handler indexes and status shifts, bounds data vectors and requires an address
byte for I2C START/read. FlatCC builders remain on Core 0; workers receive only transfer
parameters and result buffers.

## Validation results

| Check | Result |
| --- | --- |
| Regression runner | 32 suites: 30 passed, 2 physical HIL suites skipped, 0 failed |
| Debug build | Passed; main RAM 382,404 bytes |
| Release build | Passed; main RAM 382,428 bytes |
| Static main RAM budget | 389,120 bytes; Release margin 6,692 bytes |
| FlatCC object-symbol guard | All five runtime backend objects have no libc heap references, including aligned allocation |
| Worker/control tests | Blocked worker independence, wrong-core rejection, reentrancy suppression, real kernel hook ownership and 1,000 transaction handoffs passed |
| FlatCC tests | 1,000 maximum-data responses, status vectors, aligned final buffers, repeated reset/clear, overflow, exhaustion and ownership checks passed |
| Sanitizers | ASan/UBSan passed in applicable suites, including the new control/FlatCC tests |
| Patch checks | git diff whitespace check passed |

The last I2C address-vector guard was additionally verified by both firmware builds.

Reproduce after configuring Debug and Release builds:

```sh
cmake --build build_rp2350 --target bus_pirate6 --parallel
bash tests/run_all.sh
bash tests/test_control_service.sh
bash tests/test_flatcc_memory.sh
bash tests/test_memory_model.sh
```

For another configured build, set `BP_TEST_BUILD_DIR` when invoking the memory guard.

## Qualification still required

No physical port was selected and no firmware was flashed. USB enumeration, CDC/MSC
concurrency, UART input under load, PSU faults, electrical timing, watchdog recovery and
long-duration hardware stability still require a real fixture. Host tests do not prove
those behaviors. Static memory has 6,692 bytes of remaining budget; future additions
must preserve that limit. Global live-heap usage outside FlatCC is not instrumented.
