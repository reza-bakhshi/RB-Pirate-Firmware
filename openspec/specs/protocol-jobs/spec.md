# protocol-jobs Specification

## Purpose
The protocol job runtime enables Core 0 to asynchronously dispatch hardware protocol operations to Core 1, stream output and progress events across lock-free queues, enforce hard execution deadlines, and guarantee bounded cancellation latency under 100 milliseconds.

## Requirements

### Requirement: Asynchronous Job Submission and Dispatch
The system SHALL allow Core 0 to submit protocol jobs to Core 1 asynchronously without blocking the user terminal interface or prompt loop.

#### Scenario: Non-blocking submission
- **WHEN** Core 0 submits a protocol job to the job queue
- **THEN** Core 0 immediately continues executing the prompt loop while Core 1 transitions the job to RUNNING state

#### Scenario: Queue busy rejection
- **WHEN** Core 0 attempts to submit a job while an active job is already running
- **THEN** the submission returns a busy status error and does not overwrite the active job context

### Requirement: Progress and Output Event Streaming
The system SHALL stream job progress percentages and output records from Core 1 to Core 0 across lock-free rings without using printf from Core 1.

#### Scenario: Periodic progress streaming
- **WHEN** Core 1 updates the progress percentage of an executing protocol job
- **THEN** a progress event is delivered to Core 0 and displayed on the active terminal without blocking Core 1

#### Scenario: Output record emission
- **WHEN** a protocol job produces output bytes or diagnostic text
- **THEN** output records are transferred to Core 0 in strict FIFO order for console presentation

### Requirement: Deterministic Deadline and Timeout Enforcement
The system SHALL enforce hard deadlines on running protocol jobs and terminate overdue jobs cleanly without hanging the system.

#### Scenario: Job deadline expiration
- **WHEN** a running protocol job exceeds its configured timeout duration
- **THEN** Core 1 aborts the job execution, sets status to TIMEOUT, and reports completion to Core 0

### Requirement: Bounded Cancellation Latency
The system SHALL guarantee that an active protocol job terminates within 100 milliseconds upon cancellation request via Ctrl+C, user button, or USB disconnect.

#### Scenario: Ctrl+C aborts active job
- **WHEN** a user transmits Ctrl+C while a protocol job is executing
- **THEN** the job aborts within 100 milliseconds, transitions to CANCELLED state, and returns the terminal prompt to IDLE

#### Scenario: USB disconnect aborts active job
- **WHEN** the USB connection is lost during job execution
- **THEN** the active job is immediately cancelled and hardware resources are returned to safe idle states

### Requirement: Long-Running RF, IR, and Sniffer Job Conversion
The system SHALL convert long-running blocking RF, IR, and sniffer capture loops into asynchronous cancellable jobs dispatched to the Core 1 data plane via `bp_job_dispatch()`. This covers Sub-GHz (CC1101 scan, sweep, receive, record, waterfall, jam, bruteforce), RFID (PN532 inventory, read, write), RF24 (nRF24L01 scan, sweep, waterfall, receive, jam), Infrared (record, send, replay, jam), and PS/2 & USB sniffer capture loops.

#### Scenario: Dispatching RF or IR operations to Core 1
- **GIVEN** Core 0 command processor receives an RF, IR, or sniffer command
- **WHEN** the command invokes `bp_job_dispatch()` with its slice handler and execution context
- **THEN** the job is queued for Core 1 execution and Core 0 continues servicing terminal I/O without blocking.

#### Scenario: Core 0 terminal remains non-blocking during job execution
- **GIVEN** an asynchronous RF, IR, or sniffer job is actively executing on Core 1
- **WHEN** the user interacts with the terminal or issues status queries
- **THEN** Core 0 processes terminal input, updates toolbars, and remains fully responsive.

### Requirement: Automatic Resource Lease Cleanup on Job Teardown
The protocol job runtime SHALL ensure that all hardware resources leased by a running job (including PIO state machines, DMA channels, hardware IRQ handlers, pins, and big buffers under owner `BP_RES_OWNER_JOB`) are transactionally released via `bp_res_release_all()` upon job completion, timeout, error, or cancellation.

#### Scenario: Clean resource release on job cancellation
- **GIVEN** an active protocol job has acquired PIO state machines, DMA channels, or IRQ handlers under `BP_RES_OWNER_JOB`
- **WHEN** the job is cancelled via `bp_job_cancel()`
- **THEN** all leased hardware resources are completely released to the free pool within 100 milliseconds with zero leaks.

#### Scenario: Clean resource release on job completion or error
- **GIVEN** a running protocol job finishes execution or encounters a hardware fault
- **WHEN** the job transitions to `DONE`, `TIMEOUT`, or `ERROR` state
- **THEN** `bp_res_release_all(BP_RES_OWNER_JOB)` is invoked automatically, restoring all claimed resources to free state.

### Requirement: Sub-100ms Cancellation and Ctrl+C Routing
The system SHALL intercept user Ctrl+C (`0x03`) input on Core 0 and route immediate cooperative cancellation to the active job via `bp_job_cancel(bp_job_get_active_id())`, guaranteeing that job execution halts and hardware resources are released within 100 milliseconds.

#### Scenario: User Ctrl+C aborts executing job
- **GIVEN** an RF, IR, or sniffer job is executing on Core 1
- **WHEN** the user enters Ctrl+C (`0x03`) in the terminal
- **THEN** the terminal service consumes the character without polluting command buffers, signals cancellation, and the job terminates in under 100 milliseconds.

### Requirement: FALA and Sniffer Capture Integration
The Fast Logic Analyzer (FALA) and sniffer capture loops SHALL integrate with the resource lease manager and job cancellation mechanisms, ensuring DMA channels and PIO resources are tracked and tear down safely on abort.

#### Scenario: FALA resource acquisition and teardown
- **GIVEN** FALA capture is initiated
- **WHEN** DMA channels are configured
- **THEN** channels are leased under tracked owner identifiers and cleanly unreserved when capture stops or is aborted.

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
