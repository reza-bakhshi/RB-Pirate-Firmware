# Proposal

## Why
Periodic analog multiplexer (AMUX) sweeps currently execute on Core 0 by sequentially iterating over 16 channels via software polling and busy waits, loading the control CPU and delaying terminal response during heavy capture sessions. Similarly, the TF card SPI driver performs single-block polling transfers, which limits file transfer throughput for logic analyzer dumps and display captures. Finally, ST7789 display refreshes block CPU execution while clocking pixel rows over SPI. Offloading AMUX sampling, SD card block I/O, and display flushes to dedicated hardware DMA channels eliminates CPU polling overhead and significantly increases transfer bandwidth.

## What Changes
- **Autonomous DMA-Driven AMUX ADC Sweeps**:
  - Offload the 16-channel 74HC4067 multiplexer sequencing and ADC sampling to a chained DMA pipeline.
  - The DMA controller autonomously steps channel select lines, triggers ADC conversions, and fills the circular telemetry buffer without CPU intervention.
- **High-Throughput Multi-Block SPI DMA Transfers**:
  - Implement multi-block read (`CMD18`) and write (`CMD25`) commands using hardware SPI DMA channels in `src/pirate/storage.c` and `diskio.c`.
  - Maximize TF card read/write throughput up to the SPI bus clock ceiling (25–33 MHz).
- **Asynchronous ST7789 LCD Framebuffer Refresh via DMA**:
  - Implement non-blocking DMA framebuffer updates for the onboard ST7789 color display, allowing terminal commands to continue executing while pixels clock out.

## Capabilities

### New Capabilities
*(None)*

### Modified Capabilities
- `power-monitor`: Offload periodic AMUX channel sweeping to an autonomous DMA sequence.
- `storage`: Implement multi-block SPI DMA transfers for high-speed file and capture streaming.

## Impact
- Core 0 (Control plane): `src/pirate/amux.c`, `src/pirate/storage.c`, `src/pirate/lcd.c`, `src/system_monitor.c`.
- Claims 2 DMA channels for AMUX sweeping and 2 DMA channels for SPI storage under `bp_res`.
- Eliminates CPU spinloops in periodic ADC monitoring and display refresh.
