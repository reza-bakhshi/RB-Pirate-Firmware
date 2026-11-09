# Binary Modes Specification

## Purpose

Defines the binary host transports: SUMP logic analyzer, BPIO FlatBuffers, legacy Bus Pirate protocol, FALAIO, IRToy and CH32V003 programming.

## Requirements

### Requirement: Mode activation and teardown

The Binary Modes capability SHALL start only when its required hardware is available and SHALL release every owned PIO, DMA, IRQ, clock and pin resource on exit.

#### Scenario: Successful activation
- **WHEN** the required hardware is connected and powered
- **THEN** the mode reports ready and the command prompt changes

#### Scenario: Missing hardware
- **WHEN** the required hardware is absent
- **THEN** the mode reports the missing hardware and does not hang

#### Scenario: Clean exit
- **WHEN** the user leaves the mode
- **THEN** PIO, DMA, IRQ, clock, pin and PSU resources are restored

### Requirement: Command coverage

The Binary Modes capability SHALL provide every documented command and SHALL return a result or a clear error for each command.

#### Scenario: sump command
- **WHEN** the user runs `sump`
- **THEN** a host tool connects, exchanges data and disconnects cleanly

#### Scenario: bpio command
- **WHEN** the user runs `bpio`
- **THEN** a host tool connects, exchanges data and disconnects cleanly

#### Scenario: legacy command
- **WHEN** the user runs `legacy`
- **THEN** a host tool connects, exchanges data and disconnects cleanly

#### Scenario: falaio command
- **WHEN** the user runs `falaio`
- **THEN** a host tool connects, exchanges data and disconnects cleanly

#### Scenario: irtoy command
- **WHEN** the user runs `irtoy`
- **THEN** a host tool connects, exchanges data and disconnects cleanly

#### Scenario: ch32v003 command
- **WHEN** the user runs `ch32v003`
- **THEN** a host tool connects, exchanges data and disconnects cleanly

### Requirement: Failure handling

The Binary Modes capability SHALL report missing hardware, invalid input and cancellation without hanging the terminal.

#### Scenario: Invalid input
- **WHEN** the user supplies an invalid argument
- **THEN** the command reports the error and returns to the prompt

#### Scenario: Cancellation
- **WHEN** the user presses a key to cancel a long operation
- **THEN** the operation stops and the prompt returns

#### Scenario: USB disconnect
- **WHEN** the USB host disconnects during the operation
- **THEN** the device remains responsive after reconnect

### Requirement: Transport ownership
Binary transports SHALL route exclusively through the unified binary transport service (`bp_bin_transport`) and SHALL NOT call TinyUSB (`tud_*` or `tud_cdc_n_*`) directly from protocol engines or mode service loops.

#### Scenario: Transport ownership
- **WHEN** the behavior related to transport ownership is exercised
- **THEN** A binary stress test passes while the terminal is active and a USB disconnect does not hang the device.

#### Scenario: Mode engines use transport service without direct TinyUSB calls
- **GIVEN** any active binary mode (SUMP, BPIO, legacy, FALAIO, IRToy, CH32V003)
- **WHEN** binary host communications take place
- **THEN** data frames are exchanged via `bp_bin_transport` APIs and no `tud_cdc_n_*` calls are invoked from `src/binmode/`

#### Scenario: Concurrent terminal and binary operations
- **GIVEN** an active binary mode communicating over CDC1
- **WHEN** interactive terminal output or status bar refreshes occur concurrently on CDC0
- **THEN** both communication channels operate independently without data corruption or core deadlock

### Requirement: Binary Transport Framing and Backpressure Safety
The binary transport subsystem SHALL provide lock-free SPSC packet and byte queues between the USB subsystem and protocol engines, with strict backpressure drop accounting ensuring that producers never block indefinitely when the host stops reading.

#### Scenario: Unopened or stalled host connection
- **GIVEN** CDC1 is not connected or the host reader has stalled
- **WHEN** a binary mode engine transmits packets or bytes
- **THEN** data writes that exceed queue capacity return immediately, dropped bytes are accounted for in telemetry, and neither core hangs

#### Scenario: High-throughput packet streaming
- **GIVEN** an active host connection on CDC1
- **WHEN** SUMP, BPIO, or FALAIO streams capture data up to full USB-FS bandwidth
- **THEN** packets are delivered to the host in FIFO order without buffer corruption or cross-core race conditions

#### Scenario: Bounded timeout reception
- **GIVEN** a binary protocol expecting host commands
- **WHEN** reading from the binary transport with a timeout
- **THEN** the call returns within the specified deadline with `BP_STATUS_TIMEOUT` if no data arrives, while continuously feeding the watchdog supervisor
