# Design: Harden Storage and MSC Hotplug

## Architecture & Boundary Protection

```
               USB Host SCSI Traffic
                        |
                        v
          +-----------------------------+
          |       src/msc_disk.c        |
          |  tud_msc_read10_cb()        |
          |  tud_msc_write10_cb()       |
          +-----------------------------+
            /                         \
           / [Storage Available]       \ [Storage Absent]
          v                             v
  +--------------------+         +------------------------+
  | Physical SD Card   |         | Demo Disk (msc_disk)   |
  | disk_read()        |         | LBA >= 16 -> Error -1  |
  | disk_write()       |         | Writes -> Read-only -1 |
  | blockdev_lock      |         | Bounds: [16][512]      |
  +--------------------+         +------------------------+
```

### 1. LBA Bounds Checking
In `src/msc_disk.c`:
- **Demo disk fallback**: `msc_disk[MSC_DEMO_DISK_BLOCK_NUM][MSC_DEMO_DISK_BLOCK_SIZE]` consists of 16 blocks of 512 bytes.
  - In `tud_msc_read10_cb()`, if `!system_config.storage_available`, verify `lba < MSC_DEMO_DISK_BLOCK_NUM` and `(offset + bufsize) <= MSC_DEMO_DISK_BLOCK_SIZE`. If out-of-bounds, set sense `SCSI_SENSE_ILLEGAL_REQUEST` (ASC `0x21` LOGICAL BLOCK ADDRESS OUT OF RANGE) and return `-1`.
  - In `tud_msc_write10_cb()`, if `!system_config.storage_available`, reject write with sense `SCSI_SENSE_DATA_PROTECT` (ASC `0x27` WRITE PROTECTED) and return `-1`.
- **Physical disk**:
  - In `tud_msc_read10_cb()` and `tud_msc_write10_cb()`, return `-1` on any non-zero `disk_read` or `disk_write` result, increment error counters in `bp_storage`, and set appropriate SCSI sense codes (`SCSI_SENSE_MEDIUM_ERROR`).

### 2. Hotplug State Machine & SCSI Sense Tracking
`tud_msc_test_unit_ready_cb()` maintains edge-triggered detection of `system_config.storage_available`:
- **Card Removed (`true -> false`)**:
  - Sets `medium_state = md_state_not_present`.
  - Reports `SCSI_SENSE_NOT_READY` with ASC `0x3a` (MEDIUM NOT PRESENT) and returns `false`.
- **Card Inserted (`false -> true`)**:
  - Sets `medium_state = md_state_medium_changed`.
  - Reports `SCSI_SENSE_UNIT_ATTENTION` with ASC `0x28` (NOT READY TO READY CHANGE, MEDIUM MAY HAVE CHANGED) and returns `false` on the first inquiry.
  - On the subsequent test unit ready call, transitions to `md_state_ready`, reports sense `0x00`, and returns `true`.

### 3. Storage Subsystem I/O Error Accounting
In `src/services/bp_storage.c`:
- Export `bp_storage_record_io_error(bool is_write)`.
- Increment `io_read_errors` and `io_write_errors` in `s_stats`.
- In `cmd_storage.c`, report `I/O Errors: read: N, write: N`.
