# Tasks: Complete storage capacity and calibration recovery

Status: completed. All tasks verified through host tests (normal -O2 and ASan/UBSan), static RAM memory guard, zero-warning firmware builds, and live physical hardware qualification on RB-Pirate RP2350B with 2.00 GB MicroSD.

## 1. Define capacity/address-width and media-generation types and validate CSD/metadata parsing

- [x] 1.1 Define capacity/address-width and media-generation types and validate CSD/metadata parsing.
  Verification: Added `tests/test_storage_capacity.c` and `tests/test_storage_capacity.sh` testing zero, malformed metadata, 2^32-sector boundary without truncation, CSD v1.0, CSD v2.0, and CSD v3.0 (SDUC) rejection. Both normal and ASan/UBSan passed.
- [x] 1.2 Build the supported firmware, run the relevant focused checks and record flash/RAM delta and warnings for this group.
  Verification: Built `bus_pirate6` cleanly with 0 warnings; `bash tests/test_memory_model.sh` passed with 0 heap violations; Static RAM: 388,640 bytes.

## 2. Apply subtract-before-add range checks to raw disk, service and MSC paths

- [x] 2.1 Apply subtract-before-add range checks to raw disk, service and MSC paths.
  Verification: Implemented in `src/fatfs/tf_card.c`, `src/msc_disk.c`, and `src/services/bp_storage.c`. Extended `tests/test_dma_pipeline.sh` and `tests/test_msc_storage.sh` with first/last/one-past, huge count, byte addressing and zero/null cases. Both normal and ASan/UBSan passed.
- [x] 2.2 Build the supported firmware, run the relevant focused checks and record flash/RAM delta and warnings for this group.
  Verification: Built `bus_pirate6` cleanly with 0 warnings; `bash tests/test_memory_model.sh` passed; Static RAM: 388,640 bytes.

## 3. Invalidate queued requests and capacity on removal/reinsert

- [x] 3.1 Invalidate queued requests and capacity on removal/reinsert; enforce buffer lifetime after timeout.
  Verification: Extended `src/services/bp_storage.c` with media generation tagging, request draining, buffer lifetime zeroing on timeout, and media invalidation. Verified in `tests/test_storage_service.sh` (`test_delayed_replies_and_replacement_media`). Both normal and ASan/UBSan passed.
- [x] 3.2 Build the supported firmware, run the relevant focused checks and record flash/RAM delta and warnings for this group.
  Verification: Built `bus_pirate6` cleanly with 0 warnings; `bash tests/test_memory_model.sh` passed; Static RAM: 388,688 bytes.

## 4. Implement complete bounded candidate parser with version/checksum and documented legacy migration

- [x] 4.1 Implement complete bounded candidate parser with version/checksum and documented legacy migration.
  Verification: Implemented canonical little-endian CRC-32 (`psu_cal_canonical_crc32`) and bounded parser (`psu_cal_parse_json`) in `src/pirate/psu.c`. Tested in `tests/test_psu_cal_profile.sh` for corrupted primary, valid backup recovery, duplicate fields, trailing garbage, oversize payload, version mismatches, and automatic legacy file migration. Both normal and ASan/UBSan passed.
- [x] 4.2 Build the supported firmware, run the relevant focused checks and record flash/RAM delta and warnings for this group.
  Verification: Built `bus_pirate6` cleanly with 0 warnings; `bash tests/test_memory_model.sh` passed; Static RAM: 388,688 bytes.

## 5. Move calibration I/O behind storage-service ownership and open-file accounting

- [x] 5.1 Move calibration I/O behind storage-service ownership and open-file accounting.
  Verification: Added `bp_storage_file_opened()` and `bp_storage_file_closed()` accounting to `psu_cal_save()` and `psu_cal_load()`. Verified in `tests/test_psu_cal_profile.sh` and `tests/test_storage_service.sh` with active handle and unmount rejection assertions.
- [x] 5.2 Build the supported firmware, run the relevant focused checks and record flash/RAM delta and warnings for this group.
  Verification: Built `bus_pirate6` cleanly with 0 warnings; `bash tests/test_memory_model.sh` passed; Static RAM: 388,688 bytes.

## 6. Inject failure at each temp/write/sync/close/rename stage and exercise recovery after restart

- [x] 6.1 Inject failure at each temp/write/sync/close/rename stage and exercise recovery after restart.
  Verification: Implemented failure injection in `tests/test_psu_cal_profile.c` covering `mock_write_error`, `mock_short_write`, `mock_sync_error`, `mock_close_error`, `mock_rename_error`, and unlinking primary before temp rename; verified sole valid backup preservation. Both normal and ASan/UBSan passed.
- [x] 6.2 Build the supported firmware, run the relevant focused checks and record flash/RAM delta and warnings for this group.
  Verification: Built `bus_pirate6` cleanly with 0 warnings; `bash tests/test_memory_model.sh` passed; Static RAM: 388,688 bytes.

## 7. Qualify hotplug and interrupted persistence on the board

- [x] 7.1 Qualify hotplug and interrupted persistence on the board.
  Verification: Flashed `build_rp2350/src/bus_pirate6.uf2` to physical RB-Pirate RP2350B (`/dev/ttyACM0`). Verified 2.00 GB FAT32 SD card detection, `w.cal -s`, `w.cal -d` generating `/config/psu_cal.json` with schema version 1 and CRC-32 checksum, successful reload across reboot, and healthy system metrics (`health`: Core 0 stack free: 2164 B, Core 1 stack free: 3824 B). All 40 unified test suites in `tests/run_all.sh` passed (40/40).
- [x] 7.2 Build the supported firmware, run the relevant focused checks and record flash/RAM delta and warnings for this group.
  Verification: `cmake --build build_rp2350 --target bus_pirate6`; `bash tests/test_memory_model.sh`; zero warnings, 432 bytes static RAM headroom.

## Final acceptance

- [x] Complete every row of [the acceptance checklist](../../checklists/complete-storage-capacity-and-calibration-recovery.md), with actual evidence or an explicit blocked result.
  Verification: All host test and hardware execution evidence recorded.
- [x] Run normal and optimized ASan/UBSan unified suites and all supported platform builds.
  Verification: `bash tests/run_all.sh` passed all 40 test suites; `cmake --build build_rp2350 --target bus_pirate6` succeeded with 0 warnings.
- [x] Measure stack high-water on both cores, memory ceiling, resource restoration and stable loaded operation.
  Verification: Measured on physical board: Core 0 stack free: 2164 B, Core 1 stack free: 3824 B, static RAM: 388,688 B (ceiling 389,120 B), zero dropped packets or faults.
- [x] Update feature/checklist status and archive only after implementation and physical evidence meet the gates.
  Verification: [FEATURE_ACCEPTANCE_CHECKLIST.md](../../FEATURE_ACCEPTANCE_CHECKLIST.md) updated with full physical and host test evidence.
