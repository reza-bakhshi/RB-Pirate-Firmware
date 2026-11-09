# Tasks

## 1. INFRARED PIO lifecycle

- [x] 1.1 Add `tx_initialized` / `rx_initialized` guards to `src/pirate/irio_pio.c` and verify repeated init/deinit does not call remove on an uninitialized program.
- [x] 1.2 Remove `pio_config_rx_mod_freq` in `irio_pio_rx_deinit()` and verify the added/removed program count matches.
- [x] 1.3 Disable state machines and clear FIFOs before removing programs in both TX and RX deinit; verify no PIO program memory leak with the static checker.

## 2. LCDTAP SPI3 teardown

- [x] 2.1 Add `spi3Deinit()` in `src/mode/lcdtap_backend.cpp` and call it from `lcdtap_backend_stop()` for the SPI3 path.
- [x] 2.2 Verify `spi_3line_mode0_program` is removed and the SM is disabled/unclaimed on stop.
- [x] 2.3 Verify `lcdtap` help/README do not claim KS0108 unless it is actually selectable.

## 3. USB clock, PSU and RF24 jam

- [x] 3.1 Save and restore `clk_sys` around the USB sniffer; verify `clock_get_hz(clk_sys)` after exit.
- [x] 3.2 Replace direct `psu_enable`/`psu_disable` in `usb.c` with `psucmd_enable`/`psucmd_disable`.
- [x] 3.3 Toggle CE around `nrf24_jam_channel()` and verify the carrier can retune.
- [x] 3.4 Change `jam -g wifi` output/help to “WiFi-band carrier sweep” and document that RP2350 cannot inject 802.11 frames.

## 4. PIO ownership

- [x] 4.1 Remove `pio_destroy()` from `src/mode/usb.c` and release only the sniffer's own SMs/programs.
- [x] 4.2 Remove `pio_destroy()` from `src/mode/ps2.c` and release only the PS/2 SMs/programs.
- [x] 4.3 Add a static check or unit test that no mode calls `pio_clear_instruction_memory()`.

## 5. Allocation and ADC

- [x] 5.1 Check `mem_alloc()` in `logicanalyzer_setup()` and refuse to configure DMA when `la_buf == NULL`.
- [x] 5.2 Make `adc_busy_wait(false)` release the flag inside the spinlock and mark `busy` volatile.
- [x] 5.3 Add a debug-pin helper and make RFID refuse a pin pair used by the debug UART.

## 6. Verification

- [x] 6.1 Run `cmake --build build_rp2350 --parallel --target bus_pirate6` and record flash/RAM.
- [x] 6.2 Run `tests/test_ir_protocols.sh`, `tests/test_subghz.sh` and `tests/test_spsc_queue.sh`; record results.
- [x] 6.3 Run a static grep for `pio_destroy`, `pio_clear_instruction_memory`, `set_sys_clock_khz` and `psu_enable` in mode code; verify each remaining call is intentional.
- [x] 6.4 Update `openspec/FEATURES.md` checklist statuses to `STATIC PASS` for the fixed lifecycle rows, keeping hardware evidence as a separate future gate.
- [x] 6.5 Run `openspec validate --all --strict`.

## Workflow follow-up

- Archive after build and host tests pass.
- Continue with F3 (INFRARED parity) and F6 (RF24 parity).
