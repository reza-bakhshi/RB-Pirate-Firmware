# terminal-io Specification

## Purpose
Defines how the firmware buffers and delivers terminal output so command execution cannot be blocked by a slow, paused or disconnected USB host.

## Requirements

### Requirement: Non-blocking terminal output

The firmware SHALL accept terminal output without blocking the calling command on USB or UART backpressure.

#### Scenario: Host stops reading

- **GIVEN** a connected USB host that has stopped reading CDC0
- **WHEN** a command produces more output than the terminal buffer can hold
- **THEN** the command continues and returns to the prompt
- **AND** the firmware counts the dropped bytes

#### Scenario: Terminal buffer is full

- **GIVEN** the terminal output buffer is full
- **WHEN** the firmware writes another byte
- **THEN** the write returns a dropped or overflow status instead of spinning
- **AND** the overflow counter increments

### Requirement: Bounded drain

The firmware SHALL provide a terminal drain operation that returns after a caller-specified timeout.

#### Scenario: Drain succeeds

- **WHEN** a caller waits for the terminal buffer to drain with a valid timeout
- **THEN** the operation returns success once the buffer is empty

#### Scenario: Drain times out

- **WHEN** the buffer does not drain before the timeout
- **THEN** the operation returns a timeout status
- **AND** the caller can report the failure without hanging

### Requirement: Disconnect-safe delivery

The terminal service SHALL continue to drain or discard buffered output when the USB host disconnects.

#### Scenario: USB disconnect during output

- **GIVEN** a command is producing terminal output
- **WHEN** the USB host disconnects
- **THEN** Core 1 discards buffered terminal bytes instead of leaving them queued forever
- **AND** the device remains responsive after reconnect

### Requirement: Truncation is observable

The firmware SHALL report that output was truncated without flooding the terminal.

#### Scenario: Overflow marker

- **GIVEN** output has been dropped because the terminal buffer was full
- **WHEN** buffer space becomes available
- **THEN** the firmware emits one truncation marker with the dropped-byte count
- **AND** it does not emit a marker for every dropped byte

### Requirement: Health counters

The firmware SHALL expose terminal health counters through the `health` command.

#### Scenario: Health output

- **WHEN** the user runs `health`
- **THEN** the output includes terminal dropped bytes, overflow events and the output buffer high-water mark

### Requirement: `txtest` remains responsive

The `txtest` command SHALL use bounded waits and SHALL return an error instead of hanging when the host does not read.

#### Scenario: `txtest` with paused host

- **GIVEN** a host that is not reading CDC0
- **WHEN** the user runs `txtest -n 1048576`
- **THEN** the command completes or times out within a bounded interval
- **AND** the prompt becomes available again

### Requirement: Core 0 Terminal Ownership and Service API
The terminal subsystem SHALL be owned and driven by Core 0 through a centralized non-blocking service API (`bp_terminal`), ensuring that USB CDC0 terminal transmission, reception, and flow control are mediated exclusively by Core 0.

#### Scenario: Core 0 services terminal I/O
- **GIVEN** Core 0 is executing its cooperative kernel loop
- **WHEN** `bp_service(0)` runs
- **THEN** it drains pending output rings and processes inbound characters through `bp_term_service`

#### Scenario: Non-blocking terminal writes with dropped counter
- **GIVEN** the terminal output buffer is full
- **WHEN** a command attempts to write additional text
- **THEN** the write returns immediately without blocking Core 0 and increments the dropped bytes counter

### Requirement: Core 1 Isolated Text Streaming
Core 1 text and diagnostic output SHALL NOT write directly to the hardware terminal or console, but SHALL stream across a dedicated lock-free SPSC ring drained by Core 0 into the terminal output stream in strict FIFO order.

#### Scenario: Core 1 emits diagnostic output during job execution
- **GIVEN** a protocol job is executing on Core 1
- **WHEN** Core 1 produces log messages or formatted text
- **THEN** the characters are enqueued to the Core 1 text ring without calling blocking USB transmission APIs

#### Scenario: Core 1 output arrives in FIFO order without corrupted interleaving
- **GIVEN** multiple text chunks produced by Core 1
- **WHEN** Core 0 drains the Core 1 text ring during terminal servicing
- **THEN** the characters are transferred to the console in strict FIFO sequence without mid-byte corruption

### Requirement: Job Input Demultiplexing and Ctrl+C Routing
The system SHALL demultiplex user input on Core 0 so that Ctrl+C immediately triggers cooperative job cancellation without being forwarded to the active job, while user data characters are routed to the Core 1 job input ring only when an interactive job is active.

#### Scenario: Ctrl+C intercepts and aborts active protocol job
- **GIVEN** an active protocol job running on Core 1
- **WHEN** the user transmits Ctrl+C (0x03)
- **THEN** Core 0 immediately flags the job for cancellation and suppresses 0x03 from being enqueued into the job input ring

#### Scenario: Keyboard data routed to active protocol job
- **GIVEN** an active protocol job on Core 1 that expects user interaction
- **WHEN** the user types characters into the terminal
- **THEN** Core 0 places the characters into the Core 1 job input ring

#### Scenario: Terminal prompt consumes input when idle
- **GIVEN** no protocol job is currently active
- **WHEN** user input arrives
- **THEN** Core 0 delivers the characters directly to the shell command-line editor
