# Proposal: Convert Capture and File Commands to Jobs

## Why

In the baseline firmware architecture, data capture, flash memory, dump, and file editor operations (including `txtest`, flash read/write, SPI/I2C/1-Wire EEPROM dumps, logic analyzer dumps, LCDTAP frame captures, and hex/text editor sessions) execute synchronous blocking loops. During multi-megabyte flash transfers, dump sequences, or large editor buffers, Core 0 freezes, cancellation relies on ad-hoc input checks, file handles risk being left open or corrupted on abort, and big buffer memory leases (`BP_BIG_BUFFER_*`) risk being leaked.

Step C2 converts these operations into cancellable streaming jobs with bounded execution slices, progress reporting, sub-100ms cancellation guarantees, safe file handle cleanup, and automatic buffer lease reclamation.

## What Changes

- **Test Pattern Streaming (`src/commands/global/txtest.c`) (ARCH-302)**:
  - Convert `txtest` into a cancellable operation checking `bp_job_check_cancelled()` during drain and blast loops.
  - Guarantee immediate abort and `BP_BIG_BUFFER_TXTEST` release on Ctrl+C or timeout.
- **Flash and EEPROM Dump/Write Job Conversion (ARCH-303)**:
  - Convert global `dump` (`src/commands/global/dump.c`) with periodic cancellation checks and clean file close on abort.
  - Convert SPI flash operations (`src/commands/spi/spiflash.c`, `src/commands/spi/flash.c`): `spiflash_dump()`, `spiflash_load()`, `spiflash_erase_verify()`, `spiflash_write_test()`, `spiflash_write_verify()`, `spiflash_force_dump()`.
  - Convert I2C, 1-Wire, and SPI EEPROM operations (`src/commands/eeprom/eeprom_base.c`, `src/commands/i2c/i2c.c`).
- **Logic Analyzer Dump and Export (ARCH-304)**:
  - Convert logic analyzer dump operations (`src/binmode/logicanalyzer.c`, `src/binmode/sump.c`) ensuring non-blocking packet streaming and abort responsiveness.
- **LCDTAP Display Capture and Dump (ARCH-308)**:
  - Convert `lcdtap_backend_dump` and `lcdtap_backend_save_bmp` in `src/mode/lcdtap_backend.cpp` to check cooperative cancellation.
- **Editor Save/Load and EEPROM GUI Hardening (ARCH-310)**:
  - Harden `src/commands/global/edit.c`, `src/commands/global/hexedit.c`, `src/ui/ui_mem_gui.c`, and `src/commands/eeprom/eeprom_i2c_gui.c` to guarantee safe big buffer cleanup and clean file close on user exit or cancellation.
- **Progress Reporting and Sub-100ms Cancellation (ARCH-314, ARCH-316)**:
  - Provide continuous progress indication and guarantee terminal control return within 100 ms of Ctrl+C across all capture and file jobs.
- **Host Test Suite (`tests/test_capture_file_jobs.sh`)**:
  - Dedicated host test suite verifying capture, dump, flash, and editor cancellation, timeout handling, and file integrity under ASan + UBSan.

## Capabilities

### New Capabilities
None.

### Modified Capabilities
- `protocol-jobs`: Adds requirements for capture, dump, flash, and editor job conversion, safe file teardown on abort, big buffer lease safety, and sub-100ms cancellation.

## Impact

- Affects `src/commands/global/txtest.c`, `src/commands/global/dump.c`, `src/commands/global/edit.c`, `src/commands/global/hexedit.c`.
- Affects `src/commands/spi/spiflash.c`, `src/commands/spi/flash.c`.
- Affects `src/commands/eeprom/eeprom_base.c`, `src/commands/eeprom/eeprom_i2c_gui.c`, `src/commands/i2c/i2c.c`.
- Affects `src/mode/lcdtap_backend.cpp`, `src/binmode/logicanalyzer.c`.
- Guarantees zero resource/file descriptor leaks on cancellation and keeps Core 0 interactive during long data transfers.
- Preserves all static RAM (<= 380 KB) and fixed stack boundaries.
