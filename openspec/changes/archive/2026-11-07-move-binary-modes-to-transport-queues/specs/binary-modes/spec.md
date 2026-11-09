# Spec Delta: binary-modes

## Purpose

Extends the binary-modes capability to establish dedicated, non-blocking transport queues (`bp_bin_transport`) bridging USB CDC1 with protocol engines, eliminating direct TinyUSB calls from binary mode drivers, and enforcing backpressure drop safety when CDC1 is closed or unresponsive.

## MODIFIED Requirements

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

## ADDED Requirements

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
