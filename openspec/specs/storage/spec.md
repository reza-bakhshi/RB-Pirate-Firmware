# storage Specification

## Purpose
The storage capability establishes exclusive ownership of file systems (FatFS), card mounting, configuration persistence, and storage RPC on Core 0, while isolating Core 1 from direct filesystem APIs and protecting shared block devices with hardware concurrency locks.

## Requirements

### Requirement: Core 0 Storage Service and Mount State Machine
The file system (FatFS), storage mount/unmount operations, formatting, and file metadata SHALL be owned and operated exclusively on Core 0 through a non-blocking mount state machine (`bp_storage`).

#### Scenario: Clean filesystem mount on Core 0
- **GIVEN** a valid FAT formatted storage media is inserted
- **WHEN** Core 0 invokes `bp_storage_mount()`
- **THEN** FatFS mounts the volume, records filesystem type and capacity, updates state to `BP_STORAGE_STATE_MOUNTED`, and returns `BP_STATUS_OK`

#### Scenario: Guarded unmount prevents unmounting with open files
- **GIVEN** one or more open file handles exist in the storage service
- **WHEN** an unmount operation is requested
- **THEN** the storage service rejects the unmount with `BP_STATUS_ERR_BUSY` and preserves the active mount state

#### Scenario: Mount failure records error state and guidance
- **GIVEN** a corrupted or unformatted storage device
- **WHEN** Core 0 attempts to mount the filesystem
- **THEN** the storage service transitions to `BP_STORAGE_STATE_ERROR`, records the error code, and increments the mount error counter

### Requirement: Core 1 Filesystem Isolation and Debounced Card Detection
Core 1 SHALL NOT invoke any FatFS (`f_*`) or filesystem mounting routines; card detection hardware signals SHALL be debounced on Core 0 before triggering mount transitions.

#### Scenario: Core 1 reports raw card presence event
- **GIVEN** the hardware ADC or detection line detects a change in card insertion
- **WHEN** Core 1 samples the hardware presence state
- **THEN** Core 1 notifies Core 0 via `bp_storage_notify_card_detect` without calling `f_mount` or filesystem APIs

#### Scenario: Core 0 debounces transient insertion glitches
- **GIVEN** noisy contact bounce during SD card insertion
- **WHEN** transient pulses occur for less than the debounce stability threshold
- **THEN** Core 0 suppresses premature mount attempts until the presence signal remains stable

### Requirement: Block Device Concurrency Serialization
Access to raw physical storage media (`disk_read`, `disk_write`, `disk_ioctl`) SHALL be serialized using a mutual exclusion lock (`blockdev_lock`) across cores to prevent data corruption between Core 0 filesystem operations and Core 1 TinyUSB MSC block transfers.

#### Scenario: Concurrent block read during active filesystem transaction
- **GIVEN** Core 0 is executing a FatFS sector read
- **WHEN** Core 1 TinyUSB MSC issues a SCSI READ10 sector transfer
- **THEN** `blockdev_lock` serializes the physical transfers so that sectors are read sequentially without bus contention

### Requirement: Storage RPC for Protocol Jobs
Protocol jobs executing on Core 1 requiring file operations SHALL communicate asynchronously with Core 0 via typed RPC requests and responses without invoking blocking filesystem calls on Core 1.

#### Scenario: Protocol job executes file write via RPC
- **GIVEN** a protocol job running on Core 1 needing to persist dumped data
- **WHEN** the job enqueues a `BP_STORAGE_RPC_WRITE` request to the storage RPC queue
- **THEN** Core 0 processes the request during `bp_storage_service()`, executes the file write on Core 0, and returns the byte count and status to Core 1

#### Scenario: RPC timeout on stalled storage request
- **GIVEN** a storage request submitted by Core 1
- **WHEN** the storage subsystem fails to respond within the configured timeout
- **THEN** the RPC client aborts the wait, returns `BP_STATUS_ERR_TIMEOUT`, and does not hang Core 1

### Requirement: Streaming Capture Handoff
The storage subsystem SHALL provide a streaming capture buffer handoff mechanism (`bp_storage_stream_*`) allowing high-speed capture engines on Core 1 to pass fixed memory blocks to Core 0 for background filesystem writing.

#### Scenario: High-speed capture streams chunks to disk
- **GIVEN** a capture engine filling memory blocks on Core 1
- **WHEN** the engine commits a completed buffer chunk to the storage stream
- **THEN** Core 0 flushes the chunk to the active capture file on disk in FIFO order without interrupting Core 1 capture timing

### Requirement: Atomic Configuration Persistence and Fallback
System and mode configuration files SHALL be written atomically via temporary staging files and validated on load, falling back to safe factory defaults if a configuration file is missing or corrupted.

#### Scenario: Atomic configuration write survives power cut
- **GIVEN** an updated configuration payload
- **WHEN** `bp_storage_save_config()` is called
- **THEN** data is written to a temporary staging file, flushed, and renamed over the existing file, ensuring the previous file remains intact if write fails

#### Scenario: Corrupted configuration falls back to safe defaults
- **GIVEN** a configuration file with truncated or invalid JSON
- **WHEN** `bp_storage_load_config()` parses the file
- **THEN** the parser detects the corruption, restores default system parameters, and returns a non-fatal warning without crashing

### Requirement: Storage Telemetry and Status Command
The storage service SHALL maintain diagnostic counters (mount attempts, errors, unmounts, read/write errors) and provide a `storage` CLI command displaying filesystem status, metrics, and recovery guidance.

#### Scenario: Storage command displays filesystem health
- **GIVEN** a mounted FAT volume
- **WHEN** the user executes `storage`
- **THEN** the terminal displays current mount state, filesystem format, capacity, and zero error counts

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
