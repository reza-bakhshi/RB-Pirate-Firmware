## ADDED Requirements

### Requirement: Autonomous acquisition completion

The power monitor SHALL support board-qualified periodic acquisition without synchronous per-channel polling in the control service, and SHALL publish only completed frames.

#### Scenario: Complete scheduled frame
- **WHEN** a sampling frame is requested
- **THEN** the supported acquisition backend completes every configured mux channel and current sense in the documented order and publishes one timestamped generation.

#### Scenario: Unavailable backend
- **WHEN** autonomous acquisition is unsupported or a required resource is unavailable
- **THEN** startup reports the capability or resource error and the documented bounded fallback remains available without leaked leases.

### Requirement: Manual acquisition exclusion

The ADC owner SHALL prevent autonomous sampling and manual reads from reprogramming the peripheral concurrently.

#### Scenario: Manual request during sampling
- **WHEN** a manual read arrives while acquisition is active
- **THEN** it waits only until the documented pause deadline, returns a status on failure, and cannot change an active conversion.

#### Scenario: Stalled owner
- **WHEN** the existing owner stops advancing
- **THEN** the requester times out without taking ownership and the watchdog policy remains authoritative.

### Requirement: Telemetry freshness and pipeline faults

Telemetry consumers SHALL distinguish fresh complete measurements from stale or invalid frames without blocking a protocol worker.

#### Scenario: Producer laps reader
- **WHEN** sampling advances during a snapshot read
- **THEN** the reader obtains one complete generation or a bounded stale/error result, never mixed channel generations.

#### Scenario: Pipeline failure
- **WHEN** DMA/FIFO completion stalls or overruns
- **THEN** the control service stops affected transfers, records a fault, marks data invalid and applies the documented PSU fail-safe deadline without starving USB.

