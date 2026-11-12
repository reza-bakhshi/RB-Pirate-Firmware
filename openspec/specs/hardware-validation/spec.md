# hardware-validation Specification

## Purpose
Define repeatable physical qualification of PSU safety, media faults, display and SWD timing, mode/resource lifecycle, two-core watchdog recovery and sustained operation with traceable evidence.

## Requirements

### Requirement: Traceable physical acceptance evidence

Each hardware-dependent capability SHALL have a repeatable acceptance record linking its firmware, board, wiring, stimuli, independent measurement and actual result.

#### Scenario: Missing fixture
- **WHEN** the selected case has no connected or identified fixture
- **THEN** the harness records BLOCKED or SKIP with the reason and the feature remains unverified.

#### Scenario: Reproducible result
- **WHEN** a physical test completes
- **THEN** its record includes commands, expected thresholds, measured values, logs and artifact identifiers sufficient to repeat the case.

### Requirement: Fault recovery under concurrent load

The board SHALL recover from tested peripheral faults and user cancellation without losing the control plane or leaking resources.

#### Scenario: SD removal and USB load
- **WHEN** a card is removed during multiblock I/O while USB is active
- **THEN** I/O fails within its deadline, filesystem state is invalidated and reconnect/remount follows the documented recovery path.

#### Scenario: PSU trip
- **WHEN** overcurrent or persistent undervoltage occurs on the specified dummy load
- **THEN** hardware measurements confirm the defined shutdown deadline, output is disabled and the active worker is cancelled.

#### Scenario: LCD and SWD load
- **WHEN** a display transfer and SWD WAIT/FAULT cases run under storage/USB traffic
- **THEN** the display source is not reused before physical completion and SWD ACK/parity/turnaround behavior agrees with analyzer captures.

### Requirement: Repeated lifecycle and watchdog qualification

Mode and job lifecycle qualification SHALL demonstrate repeatable resource restoration and two-core watchdog supervision.

#### Scenario: Repeated transitions
- **WHEN** 1000 enter/run/cancel/exit transitions execute with the required fixtures
- **THEN** each iteration restores baseline leases, pin functions, buffer directions and clocks with no unexplained reset or counter overflow.

#### Scenario: Stuck core
- **WHEN** one core is intentionally stalled by a designated test build
- **THEN** watchdog feeding stops, the configured reset occurs and the next boot exposes a valid persistent crash record.
