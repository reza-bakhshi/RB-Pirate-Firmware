## ADDED Requirements

### Requirement: Capture, Dump, and Flash Job Conversion
The system SHALL convert long-running capture, dump, and flash read/write operations into cancellable jobs or cooperative loops that check `bp_job_check_cancelled()` at slice and page boundaries. This includes `txtest`, global `dump`, SPI flash (`spiflash_dump`, `spiflash_load`, `spiflash_erase_verify`, `spiflash_write_test`, `spiflash_write_verify`, `spiflash_force_dump`), and I2C/1-Wire/SPI EEPROM dump/write commands.

#### Scenario: User cancels active flash read or write operation
- **GIVEN** a multi-megabyte flash or EEPROM dump or write operation is executing
- **WHEN** the user presses Ctrl+C (`0x03`) or cancellation is signalled
- **THEN** the loop terminates immediately, cleans up UI progress indicators, and returns control within 100 milliseconds.

#### Scenario: Progress indication during long-running dump
- **GIVEN** a flash, EEPROM, or global dump command is initiated
- **WHEN** pages or blocks are transferred
- **THEN** progress is reported continuously to the terminal UI without blocking Core 0 event handling.

### Requirement: Safe Partial Output and File Teardown on Abort
When a capture, dump, flash, or file operation writing to storage is aborted or cancelled before completion, the system SHALL safely flush and close open FatFS file handles (`FIL`) within 100 milliseconds, preventing filesystem corruption, hung file handles, or leaked memory blocks.

#### Scenario: File handle closed on abort during dump
- **GIVEN** a dump command has opened a target file on the filesystem and is actively writing blocks
- **WHEN** cancellation or an I/O error occurs
- **THEN** the open file handle is closed cleanly via `f_close()` before returning to the command prompt.

### Requirement: Big Buffer Lease Safety Across Editor and Test Jobs
The system SHALL guarantee that all memory buffers allocated from the big buffer system (including `BP_BIG_BUFFER_TXTEST` and `BP_BIG_BUFFER_EDITOR`) are unconditionally released upon command exit, cancellation, or error.

#### Scenario: Big buffer release on txtest abort or completion
- **GIVEN** `txtest` has claimed `BP_BIG_BUFFER_TXTEST` for test pattern generation
- **WHEN** the test completes, times out, or is cancelled by the user
- **THEN** the buffer is released back to the free pool, allowing subsequent commands to claim it without error.

#### Scenario: Big buffer release on editor exit or abort
- **GIVEN** the text editor or hex editor has claimed `BP_BIG_BUFFER_EDITOR`
- **WHEN** the user quits or an allocation failure occurs
- **THEN** the editor arena is torn down and the buffer is released immediately.

### Requirement: Logic Analyzer and LCDTAP Capture Streaming
The system SHALL support cooperative cancellation during Fast Logic Analyzer sample dumping and LCDTAP display frame saving, ensuring that large capture data transfers can be aborted cleanly without deadlocking the data plane.

#### Scenario: User cancels LCDTAP frame save
- **GIVEN** LCDTAP is saving a captured framebuffer to a BMP file
- **WHEN** cancellation is requested
- **THEN** row writing stops immediately, the BMP file is closed, and the display tap write-protection is released.
