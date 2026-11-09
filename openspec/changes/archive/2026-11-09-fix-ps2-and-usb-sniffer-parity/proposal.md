# Proposal: fix-ps2-and-usb-sniffer-parity

## Why

Upstream sniffer ports for PS/2 (`src/mode/ps2.c`) and USB (`src/mode/usb.c`) introduce resource leaks, clock drift, and unresponsive polling behavior on RP2350:
1. PS/2 loads 4 PIO programs (`host_to_device_program`, `idle_signal_program`, `inhibited_signal_program`, `device_to_host_program`) on each sniff session. While `ps2_pio_release()` was updated to call `pio_remove_program()`, it requires rigorous verification across 100 consecutive enter/exit cycles to guarantee instruction slots, state machines, and IRQ handlers do not leak.
2. In `src/mode/usb.c`, `keylog_pio_start()` directly claims SMs via `pio_claim_unused_sm()` instead of arbitrating through the kernel lease manager (`bp_res_claim_unused_pio_sm()`). Additionally, `keylog_run()` enables `PIO0_IRQ_0` without registering the IRQ lease.
3. USB capture dynamically switches the system clock to 120 MHz (`set_sys_clock_khz(120000, true)`). If `sniff` is executed more than once before mode exit, `usb_saved_sys_clk_hz` would be overwritten with 120 MHz, failing to restore the original system clock on cleanup.
4. USB `cap()` and `wait_for_trigger()` use blocking polling loops (`pio_sm_get_blocking()` and tight `while(1)`) without cooperative cancellation or keypress checks, locking up the terminal if the trigger is missing or no packets arrive.
5. In `usb_handler()`, the command menu advertises `p - Stop capture` as unimplemented, while capture runs synchronously until completion; menu navigation lacks standard `'q'` and `Ctrl+C` exits.

This change closes all sniffer lifecycle defects, enforces strict lease arbitration under `BP_RES_OWNER_JOB`, makes all loops cooperatively cancellable, guarantees exact clock restoration, and creates an automated host regression test suite.

## What Changes

- **Kernel lease tracking for USB**: Convert `keylog_pio_start()` to claim PIO0 and PIO1 state machines through `bp_res_claim_unused_pio_sm(..., BP_RES_OWNER_JOB)`. Register `PIO0_IRQ_0` in `keylog_run()` via `bp_res_acquire_irq()`.
- **Complete IRQ and SM teardown in `usb_pio_release()`**: Disable IRQs `PIO0_IRQ_0` and `PIO1_IRQ_0`, remove exclusive handlers, and release all leases via `bp_res_release_all(BP_RES_OWNER_JOB)`.
- **Stack memory safety**: Allocate `s_tar_pio0_program_instructions_current` and `s_tar_pio0_program_current` statically in `src/mode/usb.c`, eliminating local stack arrays on the 4 KB stack.
- **Clock preservation**: Save the original system clock into `usb_saved_sys_clk_hz` only if not already saved, and cleanly restore it on `usb_cleanup()`.
- **Cooperative cancellation and non-blocking polling**: Add cooperative cancellation (`bp_job_check_cancelled()`) and `'q'` / `Ctrl+C` checking to `wait_for_trigger()`, `cap()`, and `usb_handler()`. Use non-blocking `pio_sm_is_rx_fifo_empty()` checks in `cap()` so the loop yields when idle.
- **Menu and command cleanup**: Support `'q'`, `'Q'`, `'x'`, `'X'`, and `Ctrl+C` for exiting `usb_handler()`. Clarify `p` command status as automatic stop at limit or on cancel.
- **Host sniffer regression test suite**: Add `tests/test_sniffers.c` and `tests/test_sniffers.sh` verifying 100 consecutive enter/exit cycles with zero PIO, SM, or IRQ leaks, clock restoration verification, and cancellation.

## Capabilities

### Modified Capabilities

- `ps2-sniffer`: Strengthen teardown verification to ensure all 4 PIO programs, 4 state machines, and 2 IRQ handlers are fully restored over 100 cycles.
- `usb-sniffer`: Enforce `bp_res_*` lease arbitration on keylogger PIO and IRQ, guarantee system clock restoration, eliminate blocking polling hangs, and support clean `'q'`/`'x'` cancellation.

## Impact

- `src/mode/ps2.c`: Verify and harden teardown in `ps2_cleanup()` and `ps2_pio_release()`.
- `src/mode/usb.c`: Register leases with `bp_res_*`, fix clock preservation, make `cap()` and `wait_for_trigger()` non-blocking, clean up command dispatch and teardown.
- `tests/test_sniffers.c`, `tests/test_sniffers.sh`: New automated unit test suite running under ASan + UBSan.
