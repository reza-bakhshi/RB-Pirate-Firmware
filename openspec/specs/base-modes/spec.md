# Base Protocol Modes Specification

## Purpose

Defines the core Bus Pirate protocol modes: HiZ, 1-Wire, UART, half-duplex UART, I2C, SPI, 2-Wire, 3-Wire, DIO, LED and JTAG.

## Requirements

### Requirement: Mode activation and teardown
The Base Protocol Modes capability SHALL start only when its required hardware is available and SHALL release every owned PIO, DMA, IRQ, clock and pin resource on exit. All mode transitions SHALL execute the active mode cleanup before configuring the target mode.

#### Scenario: Successful activation
- **WHEN** the required hardware is connected and powered
- **THEN** the mode reports ready and the command prompt changes

#### Scenario: Missing hardware
- **WHEN** the required hardware is absent
- **THEN** the mode reports the missing hardware and does not hang

#### Scenario: Clean exit
- **WHEN** the user leaves the mode
- **THEN** PIO, DMA, IRQ, clock, pin and PSU resources are restored

#### Scenario: Transactional mode switch with cleanup before setup
- **GIVEN** an active base mode
- **WHEN** a mode switch to another base mode is initiated
- **THEN** the active mode is cleaned up and its resource leases released prior to the target mode's setup execution

### Requirement: Command coverage

The Base Protocol Modes capability SHALL provide every documented command and SHALL return a result or a clear error for each command.

#### Scenario: hiz command
- **WHEN** the user runs `hiz`
- **THEN** the mode starts, setup works, read/write loopback works and cleanup returns to HiZ

#### Scenario: 1wire command
- **WHEN** the user runs `1wire`
- **THEN** the mode starts, setup works, read/write loopback works and cleanup returns to HiZ

#### Scenario: uart command
- **WHEN** the user runs `uart`
- **THEN** the mode starts, setup works, read/write loopback works and cleanup returns to HiZ

#### Scenario: hduart command
- **WHEN** the user runs `hduart`
- **THEN** the mode starts, setup works, read/write loopback works and cleanup returns to HiZ

#### Scenario: i2c command
- **WHEN** the user runs `i2c`
- **THEN** the mode starts, setup works, read/write loopback works and cleanup returns to HiZ

#### Scenario: spi command
- **WHEN** the user runs `spi`
- **THEN** the mode starts, setup works, read/write loopback works and cleanup returns to HiZ

#### Scenario: 2wire command
- **WHEN** the user runs `2wire`
- **THEN** the mode starts, setup works, read/write loopback works and cleanup returns to HiZ

#### Scenario: 3wire command
- **WHEN** the user runs `3wire`
- **THEN** the mode starts, setup works, read/write loopback works and cleanup returns to HiZ

#### Scenario: dio command
- **WHEN** the user runs `dio`
- **THEN** the mode starts, setup works, read/write loopback works and cleanup returns to HiZ

#### Scenario: led command
- **WHEN** the user runs `led`
- **THEN** the mode starts, setup works, read/write loopback works and cleanup returns to HiZ

#### Scenario: jtag command
- **WHEN** the user runs `jtag`
- **THEN** the mode starts, setup works, read/write loopback works and cleanup returns to HiZ

### Requirement: Failure handling

The Base Protocol Modes capability SHALL report missing hardware, invalid input and cancellation without hanging the terminal.

#### Scenario: Invalid input
- **WHEN** the user supplies an invalid argument
- **THEN** the command reports the error and returns to the prompt

#### Scenario: Cancellation
- **WHEN** the user presses a key to cancel a long operation
- **THEN** the operation stops and the prompt returns

#### Scenario: USB disconnect
- **WHEN** the USB host disconnects during the operation
- **THEN** the device remains responsive after reconnect

### Requirement: Mode switching

Switching between base modes SHALL stop and release the old mode before starting the new one.

#### Scenario: Mode switching
- **WHEN** the behavior related to mode switching is exercised
- **THEN** 1000 mode switches leave no leaked PIO, IRQ, DMA or pin resource.

### Requirement: Transactional Rollback to HiZ
The mode manager SHALL guarantee transactional safety during mode switches by automatically restoring the HiZ state and reclaiming partial resource leases if the target mode fails to initialize.

#### Scenario: Failed mode activation recovery
- **GIVEN** an active protocol mode
- **WHEN** switching to a mode that fails during hardware setup
- **THEN** the partial mode's resources are released, the system cleanly reverts to HiZ mode, and pins remain floating and safe
