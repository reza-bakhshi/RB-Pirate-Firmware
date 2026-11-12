# Design: DMA AMUX and Storage Optimization

## Core Ownership & Concurrency
- `amux`: Supervised by Core 0. Uses DMA ping-pong buffers so Core 0 reads a coherent array of 16 channel voltages.
- `storage`: Physical SPI DMA channels are claimed under `BP_RES_OWNER_STORAGE` on Core 0. Access remains protected by `blockdev_lock`.
- `lcd`: ST7789 display flushes initiate DMA background transmit; an end-of-frame interrupt sets an idle flag without spinlooping.

## Memory & Leases
- DMA channels claimed: 2 for AMUX, 2 for SPI storage, 1 for LCD SPI TX. All tracked in `bp_resource.c`.
- Buffers: Static DMA ring buffers for AMUX and 4 KB sector bounce buffers for multi-block storage.
