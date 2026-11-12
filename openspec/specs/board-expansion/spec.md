# board-expansion Specification

## Purpose
Define hardware feasibility and evidence gates for optional DVI and dedicated voltage-crowbar circuitry, preserving PSU protection and unsupported-board behavior until qualification succeeds.

## Requirements

### Requirement: Hardware-qualified capability enablement

DVI and voltage crowbar support SHALL remain disabled unless an identified board/adapter has a validated wiring and electrical capability record.

#### Scenario: Missing or conflicting wiring
- **WHEN** no qualified mapping exists or a required pin overlaps PSU/debug/storage use
- **THEN** enable fails before hardware side effects and reports the missing/conflicting capability.

#### Scenario: Qualified optional board
- **WHEN** a supported board mapping and acceptance evidence are supplied
- **THEN** only that mapping may expose the capability, with explicit resource ownership and documented electrical limits.

### Requirement: Physical video timing qualification

A supported DVI implementation SHALL provide complete active and blanking periods with documented actual pixel/frame clocks and bounded contention behavior.

#### Scenario: Loaded frame transmission
- **WHEN** qualified hardware outputs the defined 800-by-525 timing under concurrent load
- **THEN** independent traces confirm line/frame timing, TMDS control intervals, correct rows and no active-buffer overwrite.

#### Scenario: DMA starvation or shutdown
- **WHEN** video DMA starves, loses completion or is disabled
- **THEN** the backend reports the fault and stops or emits its documented safe blank output, releasing resources without disturbing PSU controls.

### Requirement: Independent crowbar pulse safety

Any voltage crowbar backend SHALL use dedicated qualified circuitry with an independent maximum pulse bound and SHALL preserve PSU protection.

#### Scenario: Worker or DMA stalls
- **WHEN** the CPU, DMA or trigger path stalls while a crowbar pulse is active
- **THEN** hardware ends the pulse within the validated maximum and independent current protection remains effective.

#### Scenario: Cancellation or invalid request
- **WHEN** setup fails, a request exceeds limits, or the job is cancelled
- **THEN** no unbounded pulse occurs, the dedicated switch is inactive and leased resources return to baseline.
