# Tasks

## 1. Terminal Service Implementation

- [x] 1.1 Implement `src/services/bp_terminal.h` defining the terminal service API (`bp_term_init`, `bp_term_write`, `bp_term_putc`, `bp_term_getc`, `bp_term_core1_putc`, `bp_term_job_read`, `bp_term_service`, `bp_term_wait_drain`).
- [x] 1.2 Implement `src/services/bp_terminal.c` with static SPSC rings for Core 1 text and job input, non-blocking TX, input demultiplexing, and Ctrl+C cancellation.
- [x] 1.3 Route `_putchar()` in `src/printf-4.0.0/printf.c` through `bp_term_putc()` (Core 0) and `bp_term_core1_putc()` (Core 1).
- [x] 1.4 Hook `bp_term_service()` into Core 0 kernel dispatch (`bp_service(0)`) in `src/kernel/bp_kernel.c`.

## 2. Testing and Build Integration

- [x] 2.1 Implement host unit test suite `tests/test_terminal_service.c` and `tests/test_terminal_service.sh` validating non-blocking output, Core 1 streaming, FIFO ordering, and Ctrl+C cancellation under ASan/UBSan.
- [x] 2.2 Add `src/services/bp_terminal.c` and `src/services/bp_terminal.h` to `src/CMakeLists.txt`.
- [x] 2.3 Verify all test suites pass, static RAM remains <= 380 KB with zero heap, and build target `bus_pirate6.uf2` compiles cleanly.
