# CELL Specification

## Purpose

Defines the SIMCom-style AT command feature inside UART mode: modem info, network, SIM, phonebook, SMS, calls, USSD, functional mode and raw AT access.

## Requirements

### Requirement: Mode activation and teardown

The CELL capability SHALL start only when its required hardware is available and SHALL release every owned PIO, DMA, IRQ, clock and pin resource on exit.

#### Scenario: Successful activation
- **WHEN** the required hardware is connected and powered
- **THEN** the mode reports ready and the command prompt changes

#### Scenario: Missing hardware
- **WHEN** the required hardware is absent
- **THEN** the mode reports the missing hardware and does not hang

#### Scenario: Clean exit
- **WHEN** the user leaves the mode
- **THEN** PIO, DMA, IRQ, clock, pin, UART peripheral lease, and PSU resources are restored

### Requirement: Command coverage

The CELL capability SHALL provide every documented command and SHALL return a result or a clear error for each command.

#### Scenario: cellmodem command
- **WHEN** the user runs `cellmodem`
- **THEN** the command completes with a parsed response or a clean timeout

#### Scenario: cellnet command
- **WHEN** the user runs `cellnet`
- **THEN** the command completes with a parsed response or a clean timeout

#### Scenario: cellops command
- **WHEN** the user runs `cellops`
- **THEN** the command completes with a parsed response or a clean timeout

#### Scenario: cellsim command
- **WHEN** the user runs `cellsim`
- **THEN** the command completes with a parsed response or a clean timeout

#### Scenario: cellpin command
- **WHEN** the user runs `cellpin`
- **THEN** the command completes with a parsed response or a clean timeout

#### Scenario: cellbook command
- **WHEN** the user runs `cellbook`
- **THEN** the command completes with a parsed response or a clean timeout

#### Scenario: cellsms command
- **WHEN** the user runs `cellsms`
- **THEN** the command completes with a parsed response, waits for network submission confirmation or error, or cleanly times out

#### Scenario: cellcall command
- **WHEN** the user runs `cellcall`
- **THEN** the command completes with a parsed response or a clean timeout

#### Scenario: cellussd command
- **WHEN** the user runs `cellussd`
- **THEN** the command returns the USSD reply or awaits the asynchronous network URC without hanging

#### Scenario: cellmode command
- **WHEN** the user runs `cellmode`
- **THEN** the command completes with a parsed response or a clean timeout

#### Scenario: cellat command
- **WHEN** the user runs `cellat`
- **THEN** the command completes with a parsed response or a clean timeout

### Requirement: Failure handling

The CELL capability SHALL report missing hardware, invalid input and cancellation without hanging the terminal.

#### Scenario: Invalid input
- **WHEN** the user supplies an invalid argument
- **THEN** the command reports the error and returns to the prompt

#### Scenario: Cancellation
- **WHEN** the user presses a key or requests cancellation during a long-running AT command or SMS operation
- **THEN** the operation aborts within 100 milliseconds and the prompt returns

#### Scenario: USB disconnect
- **WHEN** the USB host disconnects during the operation
- **THEN** the device remains responsive after reconnect

### Requirement: Modem dependency

CELL commands SHALL require an active UART mode and a responsive modem, and SHALL time out cleanly when the modem is absent.

#### Scenario: Modem dependency
- **WHEN** the behavior related to modem dependency is exercised
- **THEN** No modem returns a clean error and the prompt remains usable.
