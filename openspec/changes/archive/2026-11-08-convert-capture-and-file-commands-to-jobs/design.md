# Design: Convert Capture and File Commands to Jobs

## 1. Architecture Overview

In the Bus Pirate system, capture, dump, flash, and file editor commands handle large data sets (from tens of kilobytes up to 16 megabytes) across storage, memory buffers, and peripheral buses:
- **`txtest`**: Generates high-volume synthetic terminal patterns (up to 32 KB) across multiple output layers to benchmark and test UART/CDC throughput and FIFO stability.
- **Global `dump`**: Reads byte streams directly from active bus modes (I2C, SPI, 1-Wire, etc.) and writes them to FatFS files.
- **SPI Flash (`flash` / `spiflash`)**: Performs multi-megabyte flash chip identification, read, dump, erase, erase verify, write test, and write verify operations.
- **EEPROM Commands (`eeprom` / `i2c`)**: Performs block-by-block and page-by-page read, write, dump, and verify operations over I2C, 1-Wire, and SPI.
- **Logic Analyzer & SUMP (`fala` / `logicanalyzer` / `sump`)**: Arms high-speed DMA captures and dumps sample buffers over packet streams.
- **LCDTAP (`lcdtap_backend`)**: Intercepts display bus traffic, reconstructs framebuffers, and exports frames to BMP files on SD storage.
- **File Editors (`edit`, `hexedit`, `ui_mem_gui`)**: Allocate large RAM arenas (128 KB `BIG_BUFFER`) for in-place text and binary file inspection and editing.

Step C2 ensures that all long-running loops in these commands check `bp_job_check_cancelled()`, provide progress indication, release memory leases unconditionally, and safely close all open files on abort within 100 ms.

```
+-------------------------------------------------------------------------+
| Long-Running Command (dump, flash, eeprom, txtest, lcdtap)              |
|                                                                         |
|  Loop: (page / block / chunk)                                           |
|    1. Check bp_job_check_cancelled()  ---> If true:                     |
|    2. Process slice (read / write)           - ui_term_progress_cleanup |
|    3. Emit / update progress                 - f_close(&file)           |
|    4. Service background tasks               - mem_free(big_buf)        |
|                                              - return cancel status     |
+-------------------------------------------------------------------------+
```

## 2. Component Design

### 2.1 Test Pattern Streaming (`src/commands/global/txtest.c`) (ARCH-302)
- Inspects `bp_job_check_cancelled()` inside `txtest_drain()` and during chunked Layer 3 transmission loops.
- If Ctrl+C or a timeout occurs, `txtest` halts transmission, prints an aborted notice, releases `BP_BIG_BUFFER_TXTEST` via `mem_free()`, and exits immediately without hanging the terminal.

### 2.2 Global Dump and Flash Operations (`src/commands/global/dump.c`, `src/commands/spi/spiflash.c`) (ARCH-303)
- **Global `dump`**: Checks `bp_job_check_cancelled()` at each 256-byte buffer boundary. If cancelled, closes the open FatFS `FIL` handle and reports the number of bytes written before aborting.
- **SPI Flash Operations**:
  - `spiflash_dump()`, `spiflash_force_dump()`, `spiflash_load()`: Check `bp_job_check_cancelled()` every iteration. On abort, clean up progress bar (`ui_term_progress_bar_cleanup`), flush and close open file handles (`f_close(&fil)`), and deselect chip select line (`hwspi_deselect()`).
  - `spiflash_erase_verify()`, `spiflash_write_test()`, `spiflash_write_verify()`, `spiflash_verify()`: Check `bp_job_check_cancelled()` on each address block, stopping cleanly within 100 ms.

### 2.3 EEPROM and I2C Dump/Write Operations (`src/commands/eeprom/eeprom_base.c`, `src/commands/i2c/i2c.c`) (ARCH-303)
- In `eeprom_write()`, `eeprom_read()`, `eeprom_verify()`, `eeprom_erase()`, and `eeprom_test()`, check `bp_job_check_cancelled()` at block boundaries.
- On cancellation, clean up any open file handle (`file_close(&eeprom->file_handle)`), dismiss progress UI, and return cleanly.
- In `src/commands/i2c/i2c.c`, check cancellation during `i2c_dump_file()` and `i2c_dump()`.

### 2.4 Logic Analyzer and LCDTAP Capture Streaming (ARCH-304, ARCH-308)
- In `src/mode/lcdtap_backend.cpp`, `lcdtap_backend_save_bmp()` writes row-by-row image data to SD card. Add `bp_job_check_cancelled()` inside the row loop so multi-hundred-kilobyte saves can be aborted immediately, releasing the write-protection lock (`gTap->setWriteProtected(false)`) and closing the BMP file.
- In `lcdtap_backend_dump()`, check cancellation during controller event printing.
- In `src/binmode/logicanalyzer.c` and `src/binmode/sump.c`, ensure sample streaming handles cancellation without blocking.

### 2.5 Editor and UI Memory GUI Lifecycle (ARCH-310)
- In `src/commands/global/edit.c` and `src/commands/global/hexedit.c`, ensure the big buffer (`BP_BIG_BUFFER_EDITOR`) is unconditionally reclaimed via `mem_free()` on all exit paths, including error returns and setjmp recovery.
- In `src/ui/ui_mem_gui.c` and `src/commands/eeprom/eeprom_i2c_gui.c`, check cooperative cancellation during multi-block operations and ensure proper progress popup cleanup.

### 2.6 Sub-100ms Cancellation and File Handle Integrity (ARCH-314, ARCH-316)
- Bounded slice sizes ensure that no single iteration blocks for more than 5 ms.
- Any open file writing partial contents is flushed and closed cleanly via `f_close()`, preserving FAT integrity.
