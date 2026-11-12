# Tasks

## 1. DMA-Driven AMUX ADC Sweeps
- [x] 1.1 Claim 2 DMA channels under `BP_RES_OWNER_SYSTEM` for AMUX address stepping and ADC transfer
- [x] 1.2 Implement circular DMA buffer in `src/pirate/amux.c` and connect to `system_monitor.c` seqlock publication
- [x] 1.3 Verify zero CPU spinlocks during continuous voltage monitoring

## 2. Multi-Block SPI DMA Storage
- [x] 2.1 Implement multi-block read (`CMD18`) and write (`CMD25`) in `src/pirate/storage.c` using SPI DMA channels
- [x] 2.2 Benchmark sequential read/write speed and verify data integrity with MD5/SHA-256 checks

## 3. Asynchronous LCD Framebuffer Refresh
- [x] 3.1 Implement non-blocking DMA transmit in `src/pirate/lcd.c`
- [x] 3.2 Verify UI toolbar and console prompt responsiveness during full display redraws
