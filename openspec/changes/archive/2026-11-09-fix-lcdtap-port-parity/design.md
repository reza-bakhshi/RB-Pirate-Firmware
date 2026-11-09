# Design: fix-lcdtap-port-parity

## Context

See `proposal.md` — Why.

LCDTap on RP2350 runs passive display capture using PIO1 for 4-wire SPI (SPI4) or 3-wire 9-bit SPI (SPI3), or hardware I2C0 for SSD1306/ST7032. Reconstructed framebuffers use placement new into the static 128 KB Big Buffer arena (`BP_BIG_BUFFER_LCDTAP`). In `src/lib/lcdtap/pico2/src/spi_slave.cpp`, `spiSlaveInit()` loaded `spi_4line_mode0_program` but failed to assign `s->pioProgram`, preventing `spiSlaveDeinit()` from executing `pio_remove_program()`. This caused PIO instruction slots to leak on every start/stop.

## Goals / Non-Goals

**Goals:**
- Fix `s->pioProgram` pointer assignment in `spiSlaveInit()` and ensure both SPI4 and SPI3 programs are cleanly removed on stop.
- Register all claimed hardware resources (PIO state machine, DMA channel, IRQ line) under `BP_RES_OWNER_LCDTAP`.
- Release all resources cleanly via `bp_res_release_all(BP_RES_OWNER_LCDTAP)` on exit.
- Provide a host unit test (`tests/test_lcdtap.c` / `tests/test_lcdtap.sh`) that validates 100 consecutive start/stop cycles with zero resource leakage.
- Ensure controller listings and help documentation consistently document KS0108 parallel mode as physically unavailable due to 8-pin BIO header limitations.

**Non-Goals:**
- Implementing 8-bit parallel capture for KS0108 (physically requires 13 pins; only 8 BIO pins exist).
- DVI/HDMI output support (unsupported on single-board RB-Pirate).

## Decisions

### Decision 1: PIO Program Pointer Assignment and Lifecycle
In `src/lib/lcdtap/pico2/src/spi_slave.cpp`, `spiSlaveInit()` adds `spi_4line_mode0_program` to the designated PIO block. We assign:
```cpp
s->progOffset = progOffset;
s->pioProgram = &spi_4line_mode0_program;
```
When `spiSlaveDeinit(s)` is called, it checks:
```cpp
if (s->pioProgram) {
    pio_remove_program(s->cfg.pio, s->pioProgram, s->progOffset);
    s->pioProgram = nullptr;
}
```
This restores PIO instruction memory completely. For SPI3, `initSpi3()` already sets `gSpi.pioProgram = &spi_3line_mode0_program;` and `spiSlaveDeinit()` removes it.

### Decision 2: Kernel Resource Lease Tracking
When `lcdtap_backend_start()` claims hardware resources, it registers them with the kernel lease manager:
- PIO1 SM: `bp_res_acquire(BP_RES_CLASS_PIO_SM, (1 * BP_RES_SM_PER_PIO) + gSpiSm, BP_RES_OWNER_LCDTAP, "lcdtap_pio")`
- DMA channel: `bp_res_acquire(BP_RES_CLASS_DMA_CH, gSpi.dmaCh, BP_RES_OWNER_LCDTAP, "lcdtap_dma")`
- IRQ line: `bp_res_acquire(BP_RES_CLASS_IRQ, DMA_IRQ_1, BP_RES_OWNER_LCDTAP, "lcdtap_irq")`
- I2C peripheral (if I2C mode): `bp_res_acquire(BP_RES_CLASS_PERIPH, BP_RES_PERIPH_I2C0, BP_RES_OWNER_LCDTAP, "lcdtap_i2c")`

On `lcdtap_backend_stop()`, calling `bp_res_release_all(BP_RES_OWNER_LCDTAP)` guarantees no resource remains orphaned even if an early return occurs.

### Decision 3: KS0108 Availability Consistency
The KS0108 graphic controller requires 8 data pins (D0-D7) plus 5 control lines (CS1, CS2, RS, E, R/W), totaling 13 pins. Since the RB-Pirate has 8 BIO pins, parallel capture cannot physically function.
- In `controller_names`, mark index 5 as `"KS0108 (unavailable)"`.
- In `lcdtap_help()`, reinforce that KS0108 parallel mode is unavailable.
- In `setup_opts` and `controller_choices`, only offer supported serial/I2C controllers (`st7789`, `ssd1306`, `ssd1331`, `ili9341`, `st7032`).

## Risks / Trade-offs

- **[Risk] DMA IRQ sharing with other subsystems** → Mitigation: `DMA_IRQ_1` is acquired exclusively under `BP_RES_OWNER_LCDTAP` and disabled on deinit.
- **[Risk] BMP file write cancellation corrupting FatFS file handle** → Mitigation: `lcdtap_backend_save_bmp` checks `bp_job_check_cancelled()` per row, clears write protection, and always calls `f_close(&file)`.
