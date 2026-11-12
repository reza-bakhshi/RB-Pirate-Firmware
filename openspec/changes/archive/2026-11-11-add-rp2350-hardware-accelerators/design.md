# Design: RP2350 Hardware Accelerators

## Core Ownership & Concurrency
- `sha256`: Runs on Core 0 as a command job. Reads file chunks via `f_read` using a static 512-byte scratch buffer on Core 0, feeding the hardware SHA-256 block via `pico/sha256.h` hardware calls.
- `trng`: Exposes `bp_trng_get_u32()` which reads from the RP2350 hardware TRNG/ROSC registers. Core-safe and reentrant.
- `big_buffer`: The static big buffer is scaled conditionally via `#if defined(PICO_RP2350)` in `src/pirate/mem.h` from 128 KB to 256 KB.

## Memory & Leases
- Zero dynamic allocation. SHA-256 state structure and streaming blocks reside in fixed static memory.
- SRAM budget: 256 KB big buffer leaves ~264 KB SRAM for kernel queues, USB descriptors, FAT buffers, and stacks.
