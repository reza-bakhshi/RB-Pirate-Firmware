## ADDED Requirements

### Requirement: Authoritative capacity and media generation

Storage requests SHALL be checked against the current mounted medium capacity and identity before physical access.

#### Scenario: End of medium
- **WHEN** a request ends at the last valid sector
- **THEN** it succeeds if all other preconditions hold, while a request extending one sector beyond is rejected before SPI access.

#### Scenario: Unknown or replaced medium
- **WHEN** capacity is unknown or a queued request refers to an earlier card generation
- **THEN** the request fails without touching the new medium and MSC capacity/readiness is refreshed consistently.

#### Scenario: Address width boundary
- **WHEN** a sector count or byte-addressed argument cannot be represented by the driver
- **THEN** initialization or the request returns an explicit unsupported/range error without integer truncation.

### Requirement: Validated calibration candidate recovery

Calibration loading SHALL choose the first fully valid supported candidate and SHALL search backups after a present primary fails validation.

#### Scenario: Corrupt primary valid backup
- **WHEN** the primary exists but is malformed, truncated, out of range or checksum-invalid
- **THEN** the valid backup is applied with a recovery diagnostic; the invalid primary is never applied.

#### Scenario: No valid candidate
- **WHEN** all candidates fail or the card is absent
- **THEN** factory defaults are applied with a visible load status and PSU protection remains enabled.

#### Scenario: Version and integrity
- **WHEN** a supported versioned candidate is loaded
- **THEN** all coefficients and version pass the persisted checksum; unsupported versions and incomplete or ambiguous JSON are rejected before narrowing.

### Requirement: Recoverable calibration persistence ownership

Calibration persistence SHALL run through the Core 0 storage service and preserve at least one previously validated candidate on tested failure paths.

#### Scenario: Interrupted replacement
- **WHEN** power removal or an injected I/O error interrupts any save stage
- **THEN** restart selects a complete validated old or new candidate, or reports defaults when no valid candidate ever existed.

#### Scenario: Unmount and late completion
- **WHEN** unmount or worker timeout races with calibration or streaming I/O
- **THEN** open-file accounting blocks unsafe unmount and late completion cannot access reclaimed buffers or misreport success.

