# RFID Specification

## Purpose

Defines the PN532 high-speed UART RFID/NFC mode: tag inventory, reading, dumping, writing, restoring, cloning, NDEF, password authentication and diagnostics.

## Requirements

### Requirement: Mode activation and teardown

The RFID capability SHALL start only when its required hardware is available and SHALL release every owned PIO, DMA, IRQ, clock and pin resource on exit.

#### Scenario: Successful activation
- **WHEN** the required hardware is connected and powered
- **THEN** the mode reports ready and the command prompt changes

#### Scenario: Missing hardware
- **WHEN** the required hardware is absent
- **THEN** the mode reports the missing hardware and does not hang

#### Scenario: Clean exit
- **WHEN** the user leaves the mode
- **THEN** PIO, DMA, IRQ, clock, pin and PSU resources are restored

#### Scenario: Peripheral arbitration
- **WHEN** the selected UART peripheral is in use by debug/terminal UART or another subsystem
- **THEN** the mode aborts setup without altering pinmux or claiming pin labels

### Requirement: Command coverage

The RFID capability SHALL provide every documented command and SHALL return a result or a clear error for each command.

#### Scenario: scan command
- **WHEN** the user runs `scan`
- **THEN** the command completes or reports an explicit unsupported result for the tag type

#### Scenario: transceive command
- **WHEN** the user runs `transceive`
- **THEN** the command completes or reports an explicit unsupported result for the tag type

#### Scenario: jewel command
- **WHEN** the user runs `jewel`
- **THEN** the command completes or reports an explicit unsupported result for the tag type

#### Scenario: forum3 command
- **WHEN** the user runs `forum3`
- **THEN** the command completes or reports an explicit unsupported result for the tag type

#### Scenario: forum4 command
- **WHEN** the user runs `forum4`
- **THEN** the command completes or reports an explicit unsupported result for the tag type

#### Scenario: read command
- **WHEN** the user runs `read`
- **THEN** the command completes or reports an explicit unsupported result for the tag type

#### Scenario: dump command
- **WHEN** the user runs `dump`
- **THEN** the command completes or reports an explicit unsupported result for the tag type

#### Scenario: restore command
- **WHEN** the user runs `restore`
- **THEN** the command completes or reports an explicit unsupported result for the tag type

#### Scenario: write command
- **WHEN** the user runs `write`
- **THEN** the command completes or reports an explicit unsupported result for the tag type

#### Scenario: clone command
- **WHEN** the user runs `clone`
- **THEN** the command completes or reports an explicit unsupported result for the tag type

#### Scenario: erase command
- **WHEN** the user runs `erase`
- **THEN** the command completes or reports an explicit unsupported result for the tag type

#### Scenario: ndef command
- **WHEN** the user runs `ndef`
- **THEN** the command completes or reports an explicit unsupported result for the tag type

#### Scenario: pwauth command
- **WHEN** the user runs `pwauth`
- **THEN** the command completes or reports an explicit unsupported result for the tag type

#### Scenario: scanner command
- **WHEN** the user runs `scanner`
- **THEN** the command completes or reports an explicit unsupported result for the tag type

#### Scenario: nonce command
- **WHEN** the user runs `nonce`
- **THEN** the command completes or reports an explicit unsupported result for the tag type

#### Scenario: info command
- **WHEN** the user runs `info`
- **THEN** the command completes or reports an explicit unsupported result for the tag type

#### Scenario: tools command
- **WHEN** the user runs `tools`
- **THEN** the command completes or reports an explicit unsupported result for the tag type

### Requirement: Failure handling

The RFID capability SHALL report missing hardware, invalid input and cancellation without hanging the terminal.

#### Scenario: Invalid input
- **WHEN** the user supplies an invalid argument
- **THEN** the command reports the error and returns to the prompt

#### Scenario: Cancellation
- **WHEN** the user presses a key to cancel a long operation
- **THEN** the operation stops and the prompt returns

#### Scenario: USB disconnect
- **WHEN** the USB host disconnects during the operation
- **THEN** the device remains responsive after reconnect

### Requirement: Unsupported tag operations

Operations that are not valid for a detected tag type SHALL return an explicit unsupported message and SHALL close any open file.

#### Scenario: Unsupported tag operations
- **WHEN** the behavior related to unsupported tag operations is exercised
- **THEN** An unsupported operation leaves no file open and the next command works.

#### Scenario: Unsupported dump file cleanup
- **WHEN** a dump operation targets an unsupported tag type with an open output file
- **THEN** the file is closed and unlinked from the filesystem, and no success message is emitted.
