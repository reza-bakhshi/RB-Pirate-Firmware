# cmsis-dap Specification

## Purpose

Define the CMSIS-DAP v2 USB probe command interface, bounded packet handling and SWD transfer behavior, including response capacity, sequences and retry/error reporting. Physical timing and supported board routing require independent acceptance evidence.

## Requirements

### Requirement: CMSIS-DAP v2 SWD Probe Operation
The system SHALL support the ARM CMSIS-DAP v2 protocol over USB bulk endpoints, providing high-speed Serial Wire Debug (SWD) clock and data communication for host debuggers.

#### Scenario: Host debugger connection
- **GIVEN** RB-Pirate connected over USB to a host running OpenOCD or pyOCD
- **WHEN** the host queries CMSIS-DAP capabilities
- **THEN** RB-Pirate responds with SWD protocol support and maximum clock parameters.

#### Scenario: Target flashing and debugging
- **GIVEN** an ARM Cortex target connected to SWD pins
- **WHEN** OpenOCD issues DAP transfer commands
- **THEN** memory read/write and breakpoint commands execute with zero communication framing errors.

#### Scenario: Clean teardown on host disconnect
- **WHEN** the debugging session terminates or the host disconnects
- **THEN** SWD IO lines revert to safe high-impedance mode without locking the bus.

#### Scenario: Bounded command responses
- **WHEN** an information response cannot fit the supplied response capacity
- **THEN** the command returns zero without writing outside that capacity.
- **WHEN** a read transfer cannot fit its next data word
- **THEN** processing stops before executing that transfer and reports an error with the completed count.

#### Scenario: Full length SWJ sequence
- **WHEN** SWJ Sequence specifies zero in its bit-count byte
- **THEN** the probe requires and clocks all 256 sequence bits.

#### Scenario: Bounded WAIT retry
- **WHEN** a transfer returns WAIT
- **THEN** the probe retries up to its configured retry count, bounded additionally by a 100 ms elapsed deadline.
- **AND** successful reads are counted once; exhausted WAIT and FAULT responses are returned to the host.
