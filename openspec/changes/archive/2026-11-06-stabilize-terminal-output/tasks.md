# Tasks

## 1. Instrumentation

- [x] 1.1 Add `bp_terminal_health_t` with `dropped_bytes`, `overflow_events`, `high_water` and `drain_timeouts` to `src/usb_tx.h/.c`, and verify the firmware builds.
- [x] 1.2 Update the terminal producer to maintain `high_water`, and verify the counter changes during a normal `txtest -n 4096` run.
- [x] 1.3 Add a `health` command definition in `src/commands/global/` and register it in the System category of `src/commands.c`; verify `health` prints all four counters over the terminal.

## 2. Non-blocking producer

- [x] 2.1 Change `tx_fifo_put()` to return `bp_tx_status_t` and use `spsc_queue_try_add()`; verify `spsc_queue_add_blocking` is no longer referenced from `src/usb_tx.c`.
- [x] 2.2 Change `tx_fifo_write()` to attempt all bytes without spinning and return `BP_TX_DROPPED` when the ring fills; verify with a host test that fills the ring.
- [x] 2.3 Change `_putchar()` in `src/printf-4.0.0/printf.c` to use the non-blocking path; verify normal command output still appears over USB.
- [x] 2.4 Keep the existing `tx_fifo_try_put()` symbol, make it return `bp_tx_status_t`, and verify there is no duplicate symbol and existing callers compile.
- [x] 2.5 Add a compile-time check or grep-based CI guard that `src/usb_tx.c` does not call `spsc_queue_add_blocking()`.

## 3. Drain and Core 1 service

- [x] 3.1 Add `tx_fifo_wait_drain_timeout(uint32_t timeout_ms)` using `absolute_time_t`; keep `tx_fifo_wait_drain()` as a bounded compatibility wrapper (default 1000 ms) so the ~80 existing call sites do not need a signature change; verify the timeout returns `false` when the ring is full and no consumer runs.
- [x] 3.2 Reorder `tx_fifo_service()` so a disconnected CDC0 flushes the ring before the write-available check; verify a USB yank during `txtest` leaves the device responsive.
- [x] 3.3 Update `txtest` to use the bounded drain and report a timeout error; verify `txtest -n 1048576` with a paused host returns to the prompt within 5 seconds.
- [ ] 3.4 Verify the normal `txtest -n 4096` CRC still matches the expected value with an active host.

## 4. Overflow reporting

- [x] 4.1 Implement `tx_overflow_pending` and the low-water marker `[output truncated: N bytes]`; verify the marker appears once after a forced overflow.
- [x] 4.2 Ensure the marker itself never increments the drop counter or recurses; verify the counter stays stable after the marker is emitted.
- [x] 4.3 Verify `health` shows `overflow_events = 1` after one forced overflow episode, not one per dropped byte.

## 5. Tests

- [x] 5.1 Add `tests/test_terminal_tx.c` with a fake SPSC queue covering empty, full, wrap-around and drop cases; verify `tests/test_terminal_tx.sh` passes.
- [x] 5.2 Add a paused-host hardware script under `tools/serial` or `tools/hil` that opens CDC0, stops reading, runs `txtest -n 1048576`, then resumes; verify the prompt returns and output is marked truncated.
- [ ] 5.3 Add a USB disconnect/reconnect hardware check during `txtest`; verify the device reconnects and accepts a command.
- [x] 5.4 Run `cmake --build build_rp2350 --parallel --target bus_pirate6` and record flash/RAM before and after in the change notes.
- [x] 5.5 Fix `tests/test_spsc_queue.sh` to resolve the repository from `dirname "$0"` and verify it passes from both the repo root and `tests/`.

## 6. Documentation

- [x] 6.1 Update `docs/architecture/usb_communication_guide.md` with the non-blocking producer contract and the drop policy; verify the documented API names match the code.
- [x] 6.2 Update `openspec/ROADMAP.md` status for this change; verify the next change dependency is correct.
- [x] 6.3 Add a short note to `docs/architecture/rearchitecture-plan.md` pointing to this OpenSpec change as the first implemented slice.

## Workflow follow-up

- Run the full host test suite and the paused-host hardware test after review.
- Archive this change with `/opsx:archive` once all tasks and verification pass.
- Start `fix-cross-core-io-violations` only after this change is archived.
