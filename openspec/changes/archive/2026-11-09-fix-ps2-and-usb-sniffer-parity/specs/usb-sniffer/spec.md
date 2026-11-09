# Spec Delta

## MODIFIED Requirements

### Requirement: Mode activation and teardown

The USB Sniffer capability SHALL start only when its required hardware is available, SHALL register all claimed hardware resources under BP_RES_OWNER_JOB, and SHALL release every owned PIO program, state machine, IRQ line, clock, buffer, and pin resource cleanly on exit across repeated start/stop cycles.

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

The USB Sniffer capability SHALL provide documented commands, SHALL support cooperative cancellation via 'q' or Ctrl+C, and SHALL return a result or a clear error for each command.

#### Scenario: usb command
- **WHEN** the user runs `usb`
- **THEN** the sniffer menu opens and the clock changes to 120 MHz

#### Scenario: s command
- **WHEN** the user runs `s`
- **THEN** capture starts and stops with captured data or on cooperative cancellation

#### Scenario: b command
- **WHEN** the user runs `b`
- **THEN** the capture buffer is displayed

#### Scenario: k command
- **WHEN** the user runs `k`
- **THEN** keylogging captures USB keyboard reports

#### Scenario: e command
- **WHEN** the user runs `e`
- **THEN** the capture speed cycles

#### Scenario: g command
- **WHEN** the user runs `g`
- **THEN** the capture trigger cycles

#### Scenario: q command
- **WHEN** the user runs `q`
- **THEN** the menu exits and the clock is restored

### Requirement: Clock restoration

The mode SHALL restore the system clock to its pre-entry value on exit, even if sniff sessions are started repeatedly.

#### Scenario: Clock restoration
- **WHEN** the behavior related to clock restoration is exercised
- **THEN** After exit, clock_get_hz(clk_sys) equals the pre-entry value.
