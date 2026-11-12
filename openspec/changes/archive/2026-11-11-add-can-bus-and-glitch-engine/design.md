# Design: CAN Bus and Glitch Engine

## Core Ownership & Concurrency
- `can`: Protocol state machine runs on Core 1 under `BP_RES_OWNER_MODE_BASE`. Transmitted frames are queued via SPSC ring from Core 0 parser. Received frames are pushed to a lock-free queue drained by Core 0 terminal service.
- `glitch`: Executed on Core 1 as a precision protocol job. Uses hardware PIO timer counters clocked at system clock frequency (150 MHz = 6.67 ns per cycle) to guarantee sub-cycle jitter.

## Memory & Leases
- CAN frame ring buffer: Static 64-entry buffer (64 * 16 bytes = 1 KB static SRAM).
- Glitch parameters: Static configuration struct in `bp_runtime_t`.
- Zero heap allocation.
