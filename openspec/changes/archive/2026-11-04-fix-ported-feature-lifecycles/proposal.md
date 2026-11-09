# Proposal

## Why

The static code audit found lifecycle defects in the ported features. They compile, but they leak
PIO programs, change the system clock without restoring it, clear PIO state owned by other drivers,
or bypass the PSU control layer.

These are the shared S1 defects that make otherwise-correct feature code unreliable. They must be
fixed before per-feature parity work and before the architecture refactor.

## What Changes

- Fix INFRARED PIO lifecycle: remove every added program, disable state machines before removal,
  and guard deinit against never-initialized paths.
- Fix LCDTAP SPI3 teardown: remove the `spi_3line_mode0` program and disable its state machine.
- Fix USB mode: save/restore the system clock, use `psucmd_*` for PSU, and stop clearing all PIO
  instruction memory.
- Fix PS2 mode: release only its own PIO state machines/programs.
- Fix RF24 jam: toggle CE when changing the continuous-carrier channel and label the WiFi group as
  a carrier sweep, not 802.11 jamming.
- Fix logic analyzer allocation failure: do not use a NULL framebuffer.
- Fix the ADC spinlock release race.
- Add a debug-pin conflict guard so modes refuse to steal the terminal/debug UART pins.

## Capabilities

### New Capabilities

- `resource-lifecycle`: PIO, DMA, IRQ, clock, pin and buffer ownership across mode entry and exit.

### Modified Capabilities

None. The feature specs are updated by later parity changes after hardware evidence.

## Impact

- `src/pirate/irio_pio.c`
- `src/mode/lcdtap_backend.cpp`
- `src/mode/usb.c`
- `src/mode/ps2.c`
- `src/mode/rf24.c`, `src/pirate/nrf24l01.c`
- `src/binmode/logicanalyzer.c`
- `src/pirate/amux.c`
- `src/pirate/bio.c` or a new pin-ownership helper
- `openspec/FEATURES.md`, `openspec/checklists/*`
