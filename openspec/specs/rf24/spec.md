# RF24 Specification

## Purpose

Defines the nRF24L01+ 2.4 GHz mode: channel selection, RPD scanning, sweep, waterfall, packet transmit and receive, and carrier-sweep jamming.

## Requirements

### Requirement: Mode activation and teardown

The RF24 capability SHALL start only when its required hardware is available and SHALL release every owned PIO, DMA, IRQ, clock and pin resource on exit.

#### Scenario: Successful activation
- **WHEN** the required hardware is connected and powered
- **THEN** the mode reports ready and the command prompt changes

#### Scenario: Missing hardware
- **WHEN** the required hardware is absent
- **THEN** the mode reports the missing hardware and does not hang

#### Scenario: Clean exit
- **WHEN** the user leaves the mode
- **THEN** PIO, DMA, IRQ, clock, pin and PSU resources are restored

#### Scenario: Peripheral lease arbitration
- **WHEN** the SPI1 peripheral is in use by another mode or driver
- **THEN** the mode setup aborts without claiming pin purpose, modifying pinmux, or leaking leases

### Requirement: Command coverage

The RF24 capability SHALL provide every documented command and SHALL return a result or a clear error for each command.

#### Scenario: setchannel command
- **WHEN** the user runs `setchannel`
- **THEN** the command completes with a meaningful result or a clear error

#### Scenario: scan command
- **WHEN** the user runs `scan`
- **THEN** the command completes with a meaningful result or a clear error

#### Scenario: sweep command
- **WHEN** the user runs `sweep`
- **THEN** the command completes with a meaningful result or a clear error

#### Scenario: waterfall command
- **WHEN** the user runs `waterfall`
- **THEN** the command completes with a meaningful result or a clear error

#### Scenario: send command
- **WHEN** the user runs `send`
- **THEN** the command completes with a meaningful result or a clear error

#### Scenario: receive command
- **WHEN** the user runs `receive`
- **THEN** the command completes with a meaningful result or a clear error

#### Scenario: jam command
- **WHEN** the user runs `jam`
- **THEN** the command completes with a meaningful result or a clear error

### Requirement: Failure handling

The RF24 capability SHALL report missing hardware, invalid input and cancellation without hanging the terminal.

#### Scenario: Invalid input
- **WHEN** the user supplies an invalid argument
- **THEN** the command reports the error and returns to the prompt

#### Scenario: Cancellation
- **WHEN** the user presses a key to cancel a long operation
- **THEN** the operation stops and the prompt returns

#### Scenario: USB disconnect
- **WHEN** the USB host disconnects during the operation
- **THEN** the device remains responsive after reconnect

#### Scenario: Absent module error handling
- **WHEN** an RF24 command is executed while the module is unpowered or disconnected
- **THEN** the command emits a clear diagnostic message with wiring pinout, releases hardware leases, and returns promptly to the prompt

### Requirement: WiFi-band sweep limitation

The WiFi group SHALL be described as an nRF24 carrier sweep over WiFi-overlapping channels and SHALL NOT claim 802.11 frame injection on RP2350.

#### Scenario: WiFi-band sweep limitation
- **WHEN** the behavior related to wifi-band sweep limitation is exercised
- **THEN** The output and help call it a WiFi-band carrier sweep; a second receiver sees carrier energy.
