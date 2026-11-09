# Spec Delta

## MODIFIED Requirements

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

### Requirement: Sniffer teardown

The sniffer SHALL release its PIO state machines, programs and IRQ handlers on exit.

#### Scenario: Sniffer teardown
- **WHEN** the behavior related to sniffer teardown is exercised
- **THEN** After exit, another mode starts and no PIO resources are leaked.
