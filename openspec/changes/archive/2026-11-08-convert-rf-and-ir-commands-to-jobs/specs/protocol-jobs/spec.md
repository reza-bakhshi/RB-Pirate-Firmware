# protocol-jobs Specification Delta

## ADDED Requirements

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
