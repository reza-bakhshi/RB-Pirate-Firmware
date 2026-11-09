# Spec Delta: terminal-io

## Purpose

Extends the terminal-io capability to establish exclusive ownership of terminal input, output, and prompt flow control on Core 0, while providing lock-free SPSC text streaming from Core 1 and deterministic Ctrl+C job cancellation routing.

## ADDED Requirements

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
