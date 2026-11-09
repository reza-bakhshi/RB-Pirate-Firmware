# fault-recovery Specification

## Purpose
Provides hardware watchdog supervision, dual-core heartbeat tracking, persistent crash record capture, and safe-mode boot recovery on the RP2350 platform.

## Requirements

### Requirement: Hardware watchdog supervision

The firmware SHALL maintain a hardware watchdog timer on the RP2350 that resets the device if either core fails to advance its heartbeat within the timeout period.

#### Scenario: Core 0 stall
- **GIVEN** the watchdog is enabled with a configured timeout between 2 and 5 seconds
- **WHEN** Core 0 ceases advancing its heartbeat while Core 1 continues
- **THEN** the hardware watchdog resets the microcontroller upon timeout expiration

#### Scenario: Core 1 stall
- **GIVEN** the watchdog is enabled with a configured timeout between 2 and 5 seconds
- **WHEN** Core 1 ceases advancing its heartbeat while Core 0 continues
- **THEN** the hardware watchdog resets the microcontroller upon timeout expiration

#### Scenario: Hardware debugger pause
- **GIVEN** the watchdog is initialized with pause-on-debug enabled
- **WHEN** an attached SWD debugger halts CPU execution at a breakpoint
- **THEN** the watchdog timer pauses and does not trigger a reset while halted

### Requirement: HardFault and assertion crash recording

The firmware SHALL record hardware execution registers and assertion metadata in uninitialized RAM across resets when a fatal fault occurs.

#### Scenario: HardFault exception
- **WHEN** an unhandled HardFault exception occurs on either core
- **THEN** the fault handler captures Cortex-M33 registers CFSR, HFSR, BFAR, MMFAR, PC, LR, and SP into persistent memory
- **AND** triggers a controlled software reset instead of halting indefinitely

#### Scenario: Assertion or panic
- **WHEN** an assertion fails or panic is triggered
- **THEN** the runtime records the source file, line number, and error reason into persistent memory
- **AND** initiates a controlled software reset

### Requirement: Post-reset crash reporting and safe mode

The firmware SHALL inspect persistent RAM on boot, display any recorded crash diagnostics over the terminal, and enforce safe mode if repeated crashes occur.

#### Scenario: Crash report on boot
- **GIVEN** a crash record is present in uninitialized RAM with a valid magic word and checksum
- **WHEN** the firmware completes basic startup and initializes terminal output
- **THEN** the firmware prints a diagnostic report showing the fault cause, registers, or assertion details

#### Scenario: Safe mode after repeated crashes
- **GIVEN** the crash counter exceeds three consecutive crash events without clean runtime
- **WHEN** the device boots
- **THEN** the firmware enters safe mode, suppressing auto-executing scripts and leaving peripheral power rails disabled

### Requirement: Fault diagnostic commands

The firmware SHALL provide CLI commands to inspect and clear persisted crash records and report watchdog operational health.

#### Scenario: View crash diagnostics
- **WHEN** the user executes the fault command
- **THEN** the terminal displays the last recorded crash details, register dump, and boot counter

#### Scenario: Clear crash diagnostics
- **WHEN** the user executes the fault command with the clear argument
- **THEN** the stored crash record in persistent RAM is invalidated
