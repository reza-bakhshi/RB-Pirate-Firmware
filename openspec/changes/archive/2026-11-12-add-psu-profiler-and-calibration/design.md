# Design: PSU Profiler and Calibration

## Core Ownership & Concurrency
- `calibration`: Stored in `bp_runtime_t` on Core 0. Calibration calculation wizard runs interactively on Core 0 terminal.
- `profiler`: Executes as a cancellable Core 0 background operation or Core 1 job. Accumulates $\sum I \cdot \Delta t$ at each monitor tick. Flushes rows to storage through Core 0 storage service.

## Memory & Leases
- Calibration structure: 32 bytes static RAM.
- CSV logging buffer: 512 bytes static streaming buffer.
- Zero heap allocation.
