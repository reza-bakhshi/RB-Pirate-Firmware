# RFID Specification

## MODIFIED Requirements

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
