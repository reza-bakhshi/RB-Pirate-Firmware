# Design: CMSIS-DAP and HSTX Video

## Core Ownership & Concurrency
- `cmsis-dap`: USB bulk OUT/IN endpoints handled by Core 0 USB service; packet parsing and SWD bit-banging/PIO sequences dispatched to Core 1 protocol worker.
- `hstx-video`: Generates TMDS streams using RP2350 hardware HSTX FIFO and DMA channel on Core 1, consuming static line buffers.

## Memory & Leases
- CMSIS-DAP packets: 2 x 1024-byte static bulk packet buffers.
- HSTX video line buffers: 2 x 640-byte ping-pong scanline buffers (1.28 KB static RAM).
- Zero heap allocation.
