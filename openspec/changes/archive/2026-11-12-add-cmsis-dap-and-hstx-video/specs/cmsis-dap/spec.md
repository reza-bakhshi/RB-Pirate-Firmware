# Spec Delta

## ADDED Requirements

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
