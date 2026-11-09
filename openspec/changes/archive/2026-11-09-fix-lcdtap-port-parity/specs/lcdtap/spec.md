# Spec Delta

## MODIFIED Requirements

### Requirement: Mode activation and teardown

The LCDTAP capability SHALL start only when its required hardware is available, SHALL register all claimed hardware resources under BP_RES_OWNER_LCDTAP, and SHALL release every owned PIO program, state machine, DMA channel, IRQ line, clock, buffer, and pin resource cleanly on exit across repeated start/stop cycles.

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
- **THEN** zero PIO instruction words, DMA channels, or IRQ handlers are leaked

### Requirement: Controller availability

Every controller advertised in README or help SHALL be selectable or explicitly marked unavailable in the same places. KS0108 parallel mode SHALL be documented as unavailable due to the 8-pin BIO header limitation.

#### Scenario: Controller availability
- **WHEN** the behavior related to controller availability is exercised
- **THEN** KS0108 is consistently marked unavailable across CLI options, help text, and status menus
