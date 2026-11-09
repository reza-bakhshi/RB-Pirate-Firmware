# storage Specification Delta

## ADDED Requirements

### Requirement: USB MSC Logical Block Addressing and Demo Disk Bounds Protection
USB Mass Storage READ10 and WRITE10 callbacks SHALL validate logical block addresses and buffer offsets against boundaries. Fallback demo disk out-of-bounds requests SHALL return `-1` with sense `SCSI_SENSE_ILLEGAL_REQUEST` (`0x21`). Demo disk writes SHALL return `-1` with sense `SCSI_SENSE_DATA_PROTECT` (`0x27`).

#### Scenario: Host attempts out-of-bounds LBA read on demo disk
- **GIVEN** storage media is not present and USB MSC is operating in demo disk mode
- **WHEN** the host issues a SCSI READ10 command with LBA equal to or greater than `MSC_DEMO_DISK_BLOCK_NUM`
- **THEN** the MSC driver rejects the command with return code `-1` and sets sense `SCSI_SENSE_ILLEGAL_REQUEST` (`0x21`), preventing out-of-bounds memory accesses.

#### Scenario: Host attempts write to read-only demo disk
- **GIVEN** storage media is not present and the demo disk is active
- **WHEN** the host issues a SCSI WRITE10 command to any block
- **THEN** the MSC driver rejects the write with return code `-1` and sets sense `SCSI_SENSE_DATA_PROTECT` (`0x27`).

### Requirement: USB MSC Medium Ejection and SCSI Sense on Storage Hotplug
The USB MSC driver SHALL track physical storage presence transitions and deliver appropriate SCSI sense codes to the USB host. When storage media is removed, subsequent test unit ready checks SHALL report `SCSI_SENSE_NOT_READY` (ASC `0x3a` MEDIUM NOT PRESENT). When media is re-inserted, the driver SHALL report `SCSI_SENSE_UNIT_ATTENTION` (ASC `0x28` MEDIUM MAY HAVE CHANGED) before transitioning to ready state.

#### Scenario: Media removed during active USB connection
- **GIVEN** storage media is inserted and USB MSC is mounted on the host
- **WHEN** the physical card is removed
- **THEN** test unit ready callbacks set SCSI sense `SCSI_SENSE_NOT_READY` with ASC `0x3a`, informing the host operating system of media removal without hanging in-flight threads.

#### Scenario: Media inserted triggers unit attention
- **GIVEN** USB MSC was in medium not present state
- **WHEN** storage media is inserted
- **THEN** test unit ready callbacks set SCSI sense `SCSI_SENSE_UNIT_ATTENTION` with ASC `0x28` before returning ready, notifying the host to re-read partition tables and capacity.

### Requirement: Storage Subsystem Error Accounting and Status Telemetry
The storage service SHALL maintain persistent counters for block read errors, block write errors, and mount errors. These metrics SHALL be queryable through `bp_storage_get_stats()` and displayed in the `storage` command output.

#### Scenario: Block I/O error increments diagnostic counter
- **GIVEN** an active storage session
- **WHEN** a physical `disk_read` or `disk_write` operation fails
- **THEN** the storage subsystem increments the corresponding error counter and logs the failure code.
