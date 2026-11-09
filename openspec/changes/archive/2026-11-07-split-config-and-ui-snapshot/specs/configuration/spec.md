# Configuration Specification

## Purpose
Defines the clean architectural separation between persisted configuration (`bp_config_t`), volatile control plane runtime state (`bp_runtime_t`), and lockless atomic UI snapshots (`bp_ui_snapshot_t`) rendered across cores.

## ADDED Requirements

### Requirement: Persisted Configuration Schema and Validation
The system SHALL maintain a dedicated persisted configuration structure (`bp_config_t`) containing user-facing settings serialized to and from `bpconfig.bp`. The schema SHALL include an explicit integer version field. All deserialized fields SHALL be strictly validated against bounds before application.

#### Scenario: Valid configuration load
- **GIVEN** a well-formed `bpconfig.bp` file on storage
- **WHEN** `bp_config_load()` is invoked
- **THEN** all parameters are parsed, validated, and loaded into active configuration.

### Requirement: Volatile Control Plane Runtime Separation
Volatile control plane runtime state (`bp_runtime_t`) such as mount state, active mode index, error flags, and buffer owner SHALL be maintained separately from persisted configuration. Dynamic state SHALL NOT be written to `bpconfig.bp` during runtime state changes.

#### Scenario: Mode switch does not dirty persisted configuration
- **GIVEN** the system is running in `HiZ` mode
- **WHEN** a mode switch to `UART` occurs
- **THEN** the active mode is updated in `bp_runtime_t` without modifying or dirtying `bp_config_t`.

### Requirement: Seqlock-Protected Atomic UI Snapshot
The system SHALL provide an atomic, lockless snapshot structure (`bp_ui_snapshot_t`) containing current mode, voltages, currents, and pin assignments. The snapshot SHALL be protected by a monotonically incrementing sequence counter (seqlock). Core 1 or UI toolbars SHALL be able to read consistent snapshots without taking locks or spinning on mutexes.

#### Scenario: Atomic snapshot read during update
- **GIVEN** Core 0 is updating the UI snapshot
- **WHEN** Core 1 or a toolbar calls `bp_ui_snapshot_read()`
- **THEN** the reader detects the in-progress update via sequence counter and returns a consistent snapshot.

### Requirement: Corrupt Configuration Recovery and Fallback
If `bpconfig.bp` is missing, damaged, unparseable, or contains out-of-bounds parameters, the system SHALL safely fall back to compiled-in default configuration settings and continue boot without hanging or panicking.

#### Scenario: Corrupt configuration file detected
- **GIVEN** `bpconfig.bp` contains invalid syntax or corrupted bytes
- **WHEN** configuration loading is attempted
- **THEN** default configuration is loaded and the system boots into a safe operational state.
