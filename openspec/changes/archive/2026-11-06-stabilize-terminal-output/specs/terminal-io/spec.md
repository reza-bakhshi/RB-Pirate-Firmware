# Spec Delta

## Purpose

Defines how the firmware buffers and delivers terminal output so command execution cannot be blocked by a slow, paused or disconnected USB host.

## ADDED Requirements

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
