# Glitch engine specification

## Purpose

Provide bounded, cancellable software logic pulses on unused BIO pins. Delay and width arguments are expressed in nanoseconds; software GPIO operations have clock quantization and execution overhead and do not guarantee nanosecond accuracy or fixed trigger jitter.

## Requirements

### Requirement: Safe board capabilities

The engine SHALL reject power glitch requests on RB-Pirate because its PSU current-limit override is not a dedicated crowbar output. A future voltage-glitch backend requires verified dedicated circuitry and an independent hardware pulse limit.

#### Scenario: No dedicated voltage glitch circuitry
- **WHEN** a power glitch is requested on RB-Pirate without a qualified dedicated crowbar output
- **THEN** the command fails without changing current-limit override or PSU protection.

### Requirement: Bounded logic pulse jobs

Logic pulses SHALL run through Core 1 protocol jobs without printing or USB access from worker callbacks. The command SHALL require HiZ mode, unused pins, no active job, and an exclusive resource lease. Output and trigger pins SHALL differ. Pins SHALL return to input and leases SHALL be released after success, cancellation, dispatch failure or trigger timeout.

#### Scenario: Timing validation
- **WHEN** delay exceeds 100000 ns, width is outside 1..100000 ns, or count is outside 1..1000
- **THEN** the command fails before changing hardware.
- **AND** malformed numeric options fail rather than falling back to defaults.

#### Scenario: Trigger edges
- **WHEN** an external trigger is configured
- **THEN** a rising edge is selected by default and `-f` selects falling.
- **AND** an inactive level must be seen before the selected active level.
- **AND** each pulse waits at most one second for its edge, yielding between checks.

#### Scenario: Cancellation
- **WHEN** the user requests job cancellation, presses the button, or sends job input
- **THEN** the worker exits before another pulse and cleanup returns the pins to HiZ.
