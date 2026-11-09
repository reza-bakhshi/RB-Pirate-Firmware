# Proposal

## Why

Terminal output can hard-freeze the device. `printf()` calls `tx_fifo_put()`, which calls
`spsc_queue_add_blocking()` on a 1 KB ring. If Core 1 stops draining — because the USB host stopped
reading, the cable was yanked, or CDC0 is backpressured — Core 0 spins forever. `txtest` and other
large-output commands make this easy to reproduce.

This is the first change in the stability program: it removes a confirmed hard freeze without
depending on the larger core split.

## What Changes

- Replace the blocking terminal producer with a non-blocking write that returns an overflow status.
- Add a bounded `tx_fifo_wait_drain(timeout)` and migrate callers.
- Fix Core 1 drain ordering so a disconnected or backpressured host cannot leave the ring full forever.
- Add terminal health counters: dropped bytes, overflow events and ring high-water.
- Emit one truncation marker when output is dropped, instead of flooding or silently losing data.
- Make `txtest` use bounded waits and report truncation instead of hanging.
- Add host and hardware tests for the queue-full and host-paused cases.

## Capabilities

### New Capabilities

- `terminal-io`: Terminal output buffering, backpressure, disconnect behavior, truncation reporting and health counters.

### Modified Capabilities

None. This is the first spec for terminal behavior.

## Impact

- `src/usb_tx.c`, `src/usb_tx.h` — producer, drain and service state machine.
- `src/printf-4.0.0/printf.c` — `_putchar()` path.
- `src/commands/global/txtest.c` — bounded waits and truncation reporting.
- `src/pirate.c` — health command registration and counters.
- `tests/` — host test for ring-full behavior.
- `tools/serial/` or `tools/hil/` — hardware test for paused-host behavior.
- No command syntax, translation keys, FlatBuffers schema or PIO timing changes.
