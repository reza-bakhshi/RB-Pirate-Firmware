# RF24 Specification

## MODIFIED Requirements

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
