# Tasks: fix-ps2-and-usb-sniffer-parity

## 1. PS/2 Sniffer Parity & Teardown

- [x] 1.1 In `src/mode/ps2.c`, verify `ps2_pio_release()` removes all 4 PIO programs (`&host_to_device_program`, `&idle_signal_program`, `&inhibited_signal_program`, `&device_to_host_program`) and unclaims state machines cleanly.
- [x] 1.2 In `src/mode/ps2.c`, ensure `ps2_cleanup()` cancels active jobs via `bp_job_cancel_active()` and disables interrupts and PSU cleanly.

## 2. USB Sniffer Parity & Teardown

- [x] 2.1 In `src/mode/usb.c`, allocate `s_tar_pio0_program_instructions_current` and `s_tar_pio0_program_current` statically to avoid stack memory expiration on fixed 4 KB stacks.
- [x] 2.2 In `src/mode/usb.c`, update `keylog_pio_start()` to claim PIO state machines via `bp_res_claim_unused_pio_sm(..., BP_RES_OWNER_JOB)` instead of raw `pio_claim_unused_sm()`.
- [x] 2.3 In `src/mode/usb.c`, register `PIO0_IRQ_0` in `keylog_run()` via `bp_res_acquire_irq()` before enabling the interrupt.
- [x] 2.4 In `src/mode/usb.c`, update `usb_pio_release()` to disable IRQs, remove exclusive handlers, and invoke `bp_res_release_all(BP_RES_OWNER_JOB)`.
- [x] 2.5 In `src/mode/usb.c`, ensure `usb_saved_sys_clk_hz` saves the original clock frequency once and restores it on `usb_cleanup()`.
- [x] 2.6 In `src/mode/usb.c`, update `wait_for_trigger()` and `cap()` to support cooperative cancellation (`bp_job_check_cancelled()`, `'q'`, `Ctrl+C`) and non-blocking FIFO polling.
- [x] 2.7 In `src/mode/usb.c`, support `'q'`, `'Q'`, `'x'`, `'X'`, and `Ctrl+C` in `usb_handler()`, and clarify the `p` command status.

## 3. Host Unit Testing & Build Verification

- [x] 3.1 Create `tests/test_sniffers.c` and `tests/test_sniffers.sh` verifying 100 consecutive enter/exit cycles without PIO, SM, or IRQ leaks, clock restoration verification, and cancellation.
- [x] 3.2 Run all unit test suites, `test_memory_model.sh`, and build `bus_pirate6` firmware for RP2350, ensuring static RAM remains within the 380 KB limit.
