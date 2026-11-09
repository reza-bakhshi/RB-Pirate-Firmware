# Tasks: fix-lcdtap-port-parity

## 1. PIO Program & Resource Lease Teardown

- [x] 1.1 In `src/lib/lcdtap/pico2/src/spi_slave.cpp`, assign `s->pioProgram = &spi_4line_mode0_program;` in `spiSlaveInit()` so `spiSlaveDeinit()` cleanly unloads the program via `pio_remove_program()`.
- [x] 1.2 In `src/mode/lcdtap_backend.cpp`, register claimed hardware resources under `BP_RES_OWNER_LCDTAP` (`BP_RES_CLASS_PIO_SM`, `BP_RES_CLASS_DMA_CH`, `BP_RES_CLASS_IRQ`, `BP_RES_CLASS_PERIPH`) and invoke `bp_res_release_all(BP_RES_OWNER_LCDTAP)` in `lcdtap_backend_stop()`.

## 2. Controller & Documentation Alignment

- [x] 2.1 In `src/mode/lcdtap.c`, update `controller_names` and `lcdtap_help()` to explicitly designate KS0108 parallel mode as physically unavailable due to 8-pin BIO header constraints, while advertising supported serial and I2C controllers.

## 3. Host Unit Testing & Build Verification

- [x] 3.1 Create `tests/test_lcdtap.c` and `tests/test_lcdtap.sh` to test 100 consecutive start/stop cycles without PIO/DMA/IRQ leaks, verify BMP export with cooperative cancellation, and confirm ASan/UBSan cleanliness.
- [x] 3.2 Run all 23 unit test suites, `test_memory_model.sh`, and build `bus_pirate6` firmware for RP2350, ensuring static RAM remains within the 380 KB limit.
