# Proposal: Harden Storage and MSC Hotplug

## Why
When physical micro-SD storage is removed while USB Mass Storage (MSC) is active or when a host queries sectors beyond the 16-block demo disk, several safety hazards exist in the firmware:
1. `tud_msc_read10_cb()` and `tud_msc_write10_cb()` in `src/msc_disk.c` index `msc_disk[lba]` without bounds checking when storage is unavailable. Hosts requesting LBA >= 16 read arbitrary RAM/Flash or corrupt memory.
2. In-flight SCSI transfers during card removal can attempt raw disk I/O on disconnected hardware or freeze waiting for command responses.
3. Writing to the fallback demo disk must be strictly forbidden with `SCSI_SENSE_DATA_PROTECT` (Write Protected).
4. Physical card removal must update MSC media state cleanly to report SCSI Sense `SCSI_SENSE_NOT_READY` / `0x3a` (Medium Not Present) or `SCSI_SENSE_UNIT_ATTENTION` / `0x28` (Medium May Have Changed) so the host operating system unmounts or flushes cached metadata without hung I/O threads.
5. Error telemetry must account for raw block I/O failures and provide detailed diagnostics in the `storage` CLI command.

## What Changes
- Add strict LBA and buffer offset bounds checking to `tud_msc_read10_cb()` and `tud_msc_write10_cb()` for the demo disk fallback (`MSC_DEMO_DISK_BLOCK_NUM`). Return `-1` and set `SCSI_SENSE_ILLEGAL_REQUEST` (`0x21` Logical Block Address Out of Range).
- Enforce strict read-only protection on the demo disk fallback with `SCSI_SENSE_DATA_PROTECT` (`0x27` Write Protected).
- Integrate media presence status with `tud_msc_test_unit_ready_cb()`, signaling medium ejection (`0x3a`) and media change unit attention (`0x28`) on card removal/insertion.
- Track block device I/O error metrics in `bp_storage` telemetry and display them in `storage` command output.
- Add host unit tests in `tests/test_msc_storage.sh` verifying out-of-bounds LBA rejection, read-only demo disk protection, and hotplug sense state transitions.

## Capabilities
### New Capabilities
None.

### Modified Capabilities
- `storage`: Add requirements for USB MSC LBA bounds protection, medium ejection / SCSI sense state transitions, and raw block I/O error accounting.

## Impact
- Eliminates out-of-bounds memory reads/writes during USB MSC operation.
- USB Mass Storage handles SD card insertion and removal gracefully without OS filesystem hangs.
- Preserves all FatFS and storage RPC invariants established in Level 1.
