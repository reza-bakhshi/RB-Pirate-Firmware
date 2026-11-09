# resource-lifecycle Specification

## Purpose
Defines the ownership contract for PIO programs, state machines, DMA, IRQ handlers, clocks, pins and buffers used by a mode or binary feature.

## Requirements

### Requirement: Release every owned PIO resource

A mode or feature SHALL remove every PIO program it added and SHALL disable and unclaim every state machine it owns before returning to HiZ.

#### Scenario: Normal mode exit

- **GIVEN** a mode that added PIO programs and state machines
- **WHEN** the user leaves the mode
- **THEN** its programs are removed, its state machines are disabled and unclaimed, and another mode can start

#### Scenario: Repeated entry and exit

- **GIVEN** a mode that uses PIO
- **WHEN** the user enters and exits it 100 times
- **THEN** no PIO program memory or state machine is leaked

### Requirement: Do not clear other drivers' PIO state

A mode SHALL NOT clear all PIO instruction memory or unclaim state machines it does not own.

#### Scenario: USB sniffer exits

- **GIVEN** another subsystem owns a PIO program
- **WHEN** the USB sniffer exits
- **THEN** the other subsystem's program and state machine remain intact

### Requirement: Restore the system clock

A feature that changes `clk_sys` SHALL restore the previous frequency before returning control to the shell.

#### Scenario: USB sniffer exits

- **GIVEN** the USB sniffer changed the clock to 120 MHz
- **WHEN** the user exits the sniffer
- **THEN** `clock_get_hz(clk_sys)` returns the pre-entry value

### Requirement: Use the PSU control layer

A feature SHALL enable and disable VOUT through `psucmd_*`, not by calling the low-level PSU driver directly.

#### Scenario: Mode enables target power

- **GIVEN** a mode needs 3.3 V on VOUT
- **WHEN** it enables power
- **THEN** pin labels, monitor state and error handling follow the normal PSU command path

### Requirement: Handle allocation failure

A feature SHALL check every large-buffer allocation and SHALL return a clear error instead of using a NULL pointer.

#### Scenario: Logic analyzer buffer unavailable

- **GIVEN** the big buffer is owned by another feature
- **WHEN** the logic analyzer starts
- **THEN** it reports the allocation failure and does not configure DMA with a NULL address

### Requirement: Respect debug pin ownership

A mode SHALL refuse to start when a pin it needs is marked `BP_PIN_DEBUG`.

#### Scenario: RFID pins overlap the debug UART

- **GIVEN** BIO0/BIO1 are used by the debug UART
- **WHEN** the user starts RFID on that pin pair
- **THEN** the mode refuses with a clear error and leaves the debug UART working
