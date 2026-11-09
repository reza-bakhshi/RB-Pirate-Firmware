# Design: fix-ps2-and-usb-sniffer-parity

## Context

See `proposal.md` — Why.

The Bus Pirate passive sniffer modes for PS/2 and USB monitor fast serial signals using the RP2350's programmable I/O (PIO) blocks and state machines. In previous revisions, teardown paths failed to properly account for PIO instruction memory, resource manager lease registrations, system clock manipulation, and cooperative task cancellation.

## Goals / Non-Goals

**Goals:**
- Guarantee zero PIO instruction leaks, state machine leaks, or IRQ handler leaks across 100 consecutive enter/exit cycles in both PS/2 and USB sniffer modes.
- Route all PIO state machine allocations in `usb.c` (`keylog_pio_start()`, `cap()`) and IRQ registrations through the kernel lease manager (`bp_res_*`) under `BP_RES_OWNER_JOB`.
- Ensure `usb_saved_sys_clk_hz` preserves the true baseline system clock and restores it upon `usb_cleanup()`.
- Eliminate blocking `while(1)` polling loops in `wait_for_trigger()` and `cap()` by adding `bp_job_check_cancelled()` and non-blocking RX FIFO checks.
- Support standard `'q'`, `'Q'`, `'x'`, `'X'`, and `Ctrl+C` exit signals in `usb_handler()`.
- Add host test suite `tests/test_sniffers.c` and `tests/test_sniffers.sh` running under ASan + UBSan.

**Non-Goals:**
- Implementing USB high-speed (480 Mbps) sniffing (hardware limitation: RP2350 USB PHY and GPIO sampling limit is Full-Speed / 12 Mbps).
- Emulating PS/2 host or device transmission beyond passive monitoring.

## Decisions

### Decision 1: USB PIO State Machine and IRQ Lease Tracking
In `src/mode/usb.c`, `keylog_pio_start()` previously invoked raw `pio_claim_unused_sm()`. We replace this with:
```c
uint32_t sm_u = 0;
if (bp_res_claim_unused_pio_sm(pio_idx, BP_RES_OWNER_JOB, name, &sm_u) != BP_STATUS_OK) {
    // handle error
}
```
In `keylog_run()`, before enabling `PIO0_IRQ_0`, we register the interrupt with `bp_res_acquire_irq(PIO0_IRQ_0, BP_RES_OWNER_JOB, "usb_pio0", usb_pio0_irq)`.

### Decision 2: Complete Teardown in `usb_pio_release()`
`usb_pio_release()` disables both `PIO0_IRQ_0` and `PIO1_IRQ_0`, removes their handlers if matching, removes all tracked PIO programs via `pio_remove_program()`, unclaims state machines, and calls `bp_res_release_all(BP_RES_OWNER_JOB)`.

### Decision 3: Clock Preservation and Restoration
To prevent multiple `sniff` invocations from overwriting `usb_saved_sys_clk_hz` with 120 MHz:
```c
if (usb_saved_sys_clk_hz == 0) {
    usb_saved_sys_clk_hz = clock_get_hz(clk_sys);
}
```
In `usb_cleanup()`:
```c
if (usb_saved_sys_clk_hz) {
    set_sys_clock_khz(usb_saved_sys_clk_hz / 1000u, true);
    stdio_init_all();
    usb_saved_sys_clk_hz = 0;
}
```

### Decision 4: Non-Blocking Polling and Cooperative Cancellation
In `wait_for_trigger()`:
```c
while (1) {
    if (bp_job_check_cancelled()) return false;
    char ch;
    if (rx_fifo_try_get(&ch) && (ch == 'q' || ch == 'Q' || ch == 0x03)) return false;
    if (gpio_get(TRIGGER_INDEX) == 0) return true;
    busy_wait_us(10);
}
```
In `cap()`:
Check `wait_for_trigger()`; if false, cleanly exit. In the packet reading loop, check `pio_sm_is_rx_fifo_empty()` and cooperative cancellation instead of blocking indefinitely on `pio_sm_get_blocking()`.

## Risks / Trade-offs

- **[Risk] PIO FIFO empty polling loop overhead in full-speed capture** → Mitigation: Checking `pio_sm_is_rx_fifo_empty()` before `pio_sm_get()` introduces single-digit microsecond latency which is well within 12 Mbps packet inter-frame spacing, while preventing unrecoverable deadlocks when packets are absent.
