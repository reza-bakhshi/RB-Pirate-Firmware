# Proposal

## Why
Hardware developers and reverse-engineers currently need separate dedicated debug probes (e.g. Raspberry Pi Debug Probe, J-Link, ST-Link) to perform ARM Cortex Serial Wire Debug (SWD) flashing and source-level debugging with OpenOCD or pyOCD. Furthermore, standalone bench exploration without a PC is constrained by the small onboard 1.3" display. The RP2350B provides an HSTX (High-Speed Serial Transmit) peripheral capable of generating high-resolution digital video bitstreams (DVI/HDMI) directly, while its dual-core architecture allows running standard CMSIS-DAP v2 USB bulk endpoints simultaneously with protocol exploration.

## What Changes
- **CMSIS-DAP v2 Hardware Debug Probe (`src/binmode/cmsis_dap.c`)**:
  - Implement the ARM CMSIS-DAP v2 protocol over USB bulk endpoints (WinUSB / driverless on modern OSes).
  - Support high-speed SWD transactions (SWDIO/SWCLK) up to 10+ MHz for flashing and debugging RP2040, RP2350, STM32, and Nordic nRF targets via OpenOCD, pyOCD, and Keil.
- **HSTX DVI Digital Video Output (`src/pirate/hstx_dvi.c`)**:
  - Implement a digital video driver utilizing the RP2350 HSTX peripheral generating 640x480 60Hz DVI bitstreams.
  - Provide direct video mirroring of the VT100 interactive console, logic analyzer waveforms, and LCDTAP external display captures on HDMI/DVI monitors.

## Capabilities

### New Capabilities
- `cmsis-dap`: Provides CMSIS-DAP v2 SWD hardware debugging over USB bulk endpoints for OpenOCD and pyOCD.
- `hstx-video`: Provides direct DVI digital video output via the RP2350 HSTX serializer for external monitor display.

### Modified Capabilities
*(None)*

## Impact
- Core 0 (USB endpoints) and Core 1 (SWD protocol engine & HSTX serializer): `src/binmode/cmsis_dap.c`, `src/pirate/hstx_dvi.c`, `src/pirate/bio.c`.
- Exposes standard USB WinUSB bulk endpoints on CDC/USB descriptor table.
- HSTX operates via dedicated pin routing without starving CPU cores or blocking main console operations.
