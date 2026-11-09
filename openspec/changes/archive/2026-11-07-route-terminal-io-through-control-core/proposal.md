# Proposal: Route Terminal I/O Through Control Core

## Why

Currently, console I/O (`printf`, `tx_fifo_put`, `rx_fifo_try_get`) is accessed directly across both cores without strict ownership boundaries. Core 1 protocol drivers or background tasks that call `printf()` can collide with Core 0 console output, corrupt terminal escape sequences, cause lock contention, or trigger assertions. Furthermore, during long-running protocol operations on Core 1, user keystrokes (such as Ctrl+C) are not cleanly demultiplexed between the command prompt, job cancellation, and interactive job input.

Step B4 establishes strict terminal ownership on Core 0 through `bp_terminal`. Core 1 output is routed through a dedicated lock-free SPSC ring, and Core 0 demultiplexes terminal input, ensuring that Ctrl+C instantly aborts background jobs while forwarding data characters to active protocol jobs.

## What Changes

- **Terminal Service Subsystem (`src/services/bp_terminal.h`, `src/services/bp_terminal.c`)**:
  - Implement a unified non-blocking terminal service on Core 0 (`bp_term_init`, `bp_term_write`, `bp_term_putc`, `bp_term_read`, `bp_term_service`, `bp_term_wait_drain`).
  - Encapsulate `usb_tx.c` and `usb_rx.c` behind the terminal service interface.
  - Track terminal metrics: TX level, high-water mark, dropped byte counts, overflow events.
- **Core 1 Text Output Ring**:
  - Statically allocated lock-free SPSC queue (`s_core1_text_ring`, 2048 bytes) for Core 1 output.
  - Redirect `_putchar()` on Core 1 to `bp_term_core1_putc()`.
  - Core 0 drains `s_core1_text_ring` during `bp_term_service()` and emits text in strict FIFO order without mangling prompts or ANSI sequences.
- **Input Demultiplexing & Job Routing**:
  - Check inbound characters on Core 0:
    - If `0x03` (Ctrl+C) is received and a protocol job is running, immediately invoke `bp_job_cancel(active_id)` without pushing `0x03` into the job input buffer.
    - If an active job expects interactive input, forward user characters to `s_core1_job_input_ring`.
    - When no job is active, forward characters to the terminal command prompt (`linenoise` / shell).
- **Core 0 Service Kernel Hook**:
  - Hook `bp_term_service()` into `bp_service(0)` inside `src/kernel/bp_kernel.c`.
- **Automated Verification**:
  - Host unit tests (`tests/test_terminal_service.c`, `tests/test_terminal_service.sh`) testing non-blocking writes, Core 1 streaming, FIFO ordering, Ctrl+C cancellation interception, and job input routing under AddressSanitizer and UBSan.

## Capabilities

### New Capabilities
<!-- None -->

### Modified Capabilities
- `terminal-io`: Core 0 terminal ownership, Core 1 lock-free text streaming ring, input demultiplexing, and prompt-safe Ctrl+C job cancellation.

## Impact

- **Affected Code**:
  - `src/services/bp_terminal.h`, `src/services/bp_terminal.c`: New terminal service.
  - `src/printf-4.0.0/printf.c`: Route `_putchar` per core.
  - `src/kernel/bp_kernel.c`: Hook `bp_term_service()` into `bp_service(0)`.
  - `src/CMakeLists.txt`: Build target inclusion.
- **Memory & Resource Budget**:
  - Zero dynamic heap allocation (`malloc`, `free`, `new`, `delete`).
  - Static SPSC rings allocated in `.bss` well within the RP2350 <= 380 KB static RAM budget.
