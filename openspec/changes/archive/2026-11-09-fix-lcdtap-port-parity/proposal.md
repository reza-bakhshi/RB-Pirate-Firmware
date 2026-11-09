# Proposal: fix-lcdtap-port-parity

## Why

LCDTap passive display capture on RP2350 currently leaks PIO instruction memory across start/stop cycles because `spiSlaveInit` omitted assigning `s->pioProgram = &spi_4line_mode0_program`, causing `spiSlaveDeinit` to skip `pio_remove_program`. Furthermore, LCDTap hardware usage (PIO1 state machine, DMA channel, and DMA/I2C IRQs) is not registered with the kernel resource lease manager (`bp_res_*`), and controller listings contain inconsistencies regarding parallel KS0108 (which requires 13 pins and cannot run on the 8-pin BIO header).

This change establishes leak-free start/stop cycles, binds hardware usage to `BP_RES_OWNER_LCDTAP`, unifies controller documentation, and adds host regression tests.

## What Changes

- **Fix PIO program teardown leak in SPI4 and SPI3**: Assign `s->pioProgram = &spi_4line_mode0_program` in `spiSlaveInit()` so `spiSlaveDeinit()` properly removes the program from PIO1 via `pio_remove_program()`. Ensure `spi3Deinit()` cleanly unloads `spi_3line_mode0_program`.
- **Hardware resource lease arbitration**: Acquire and release PIO state machine (`BP_RES_CLASS_PIO_SM`), DMA channel (`BP_RES_CLASS_DMA_CH`), and interrupt line (`BP_RES_CLASS_IRQ`) under `BP_RES_OWNER_LCDTAP` on start/stop.
- **Controller list and help consistency**: Align `controller_names`, `controller_choices`, and `lcdtap_help` to consistently document KS0108 as unavailable due to 8-pin BIO header constraints, while advertising supported controllers (ST7789/ST7735, SSD1306, SSD1331, ILI9341, ST7032).
- **Safe cancellation & Big Buffer bounds**: Confirm cooperative cancellation in `lcdtap_backend_save_bmp` and `lcdtap_backend_dump` remains active and bounds-checked.
- **Host unit test suite**: Add `tests/test_lcdtap.c` and `tests/test_lcdtap.sh` to verify 100 consecutive start/stop cycles without PIO, DMA, or IRQ leaks, verifying BMP generation and cancellation.

## Capabilities

### Modified Capabilities

- `lcdtap`: Strengthen mode activation and teardown to verify zero PIO/DMA/IRQ leaks over repeated cycles, enforce `bp_res_*` lease arbitration, and guarantee KS0108 availability consistency.

## Impact

- `src/lib/lcdtap/pico2/src/spi_slave.cpp`: Assign `s->pioProgram` in `spiSlaveInit()`.
- `src/mode/lcdtap_backend.cpp`: Register PIO, DMA, and IRQ leases with `bp_res_*`, ensure clean teardown.
- `src/mode/lcdtap.c`: Reconcile controller choices and help strings.
- `tests/test_lcdtap.c`, `tests/test_lcdtap.sh`: New automated host test suite with AddressSanitizer and UBSan.
