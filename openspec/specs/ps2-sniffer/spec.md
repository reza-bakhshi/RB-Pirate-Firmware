# PS2 Sniffer Specification

## Purpose

Defines the passive PS/2 keyboard and mouse capture mode, including its sniff command and clean exit behavior.

## Requirements

### Requirement: Mode activation and teardown

The PS2 Sniffer capability SHALL start only when its required hardware is available, SHALL register all claimed hardware resources under BP_RES_OWNER_JOB, and SHALL release every owned PIO program, state machine, IRQ line, buffer, and pin resource cleanly on exit across repeated start/stop cycles.

#### Scenario: Successful activation
- **WHEN** the required hardware is connected and powered
- **THEN** the mode reports ready and the command prompt changes

#### Scenario: Missing hardware
- **WHEN** the required hardware is absent
- **THEN** the mode reports the missing hardware and does not hang

#### Scenario: Clean exit
- **WHEN** the user leaves the mode
- **THEN** PIO, DMA, IRQ, clock, pin and PSU resources are restored

#### Scenario: Leak-free repeated start/stop cycles
- **WHEN** the mode is started and stopped repeatedly for 100 consecutive cycles
- **THEN** zero PIO instruction words, state machines, or IRQ handlers are leaked

### Requirement: Command coverage

The PS2 Sniffer capability SHALL provide every documented command and SHALL return a result or a clear error for each command.

#### Scenario: ps2 command
- **WHEN** the user runs `ps2`
- **THEN** keyboard and mouse traffic is captured and q exits cleanly

### Requirement: Failure handling

The PS2 Sniffer capability SHALL report missing hardware, invalid input and cancellation without hanging the terminal.

#### Scenario: Invalid input
- **WHEN** the user supplies an invalid argument
- **THEN** the command reports the error and returns to the prompt

#### Scenario: Cancellation
- **WHEN** the user presses a key to cancel a long operation
- **THEN** the operation stops and the prompt returns

#### Scenario: USB disconnect
- **WHEN** the USB host disconnects during the operation
- **THEN** the device remains responsive after reconnect

### Requirement: Sniffer teardown

The sniffer SHALL release its PIO state machines, programs and IRQ handlers on exit.

#### Scenario: Sniffer teardown
- **WHEN** the behavior related to sniffer teardown is exercised
- **THEN** After exit, another mode starts and no PIO resources are leaked.
