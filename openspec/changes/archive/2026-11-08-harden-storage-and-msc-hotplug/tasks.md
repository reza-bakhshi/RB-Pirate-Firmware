# Tasks: Harden Storage and MSC Hotplug

- [x] 1. Add LBA bounds check and offset validation to `tud_msc_read10_cb()` in `src/msc_disk.c` for demo disk and physical media <!-- id: 1.1 -->
- [x] 2. Enforce write-protection on fallback demo disk and add bounds validation to `tud_msc_write10_cb()` in `src/msc_disk.c` <!-- id: 2.1 -->
- [x] 3. Implement edge-triggered hotplug tracking in `tud_msc_test_unit_ready_cb()` with SCSI sense `0x3a` (medium not present) and `0x28` (medium changed) <!-- id: 3.1 -->
- [x] 4. Add I/O error accounting in `src/services/bp_storage.c` and wire failure paths in `src/msc_disk.c` <!-- id: 4.1 -->
- [x] 5. Create host test suite `tests/test_msc_storage.sh` verifying out-of-bounds LBA rejection, read-only demo disk protection, and hotplug sense state transitions under ASan/UBSan <!-- id: 5.1 -->
- [x] 6. Run OpenSpec validation and all 18 host test suites <!-- id: 6.1 -->
- [x] 7. Build target firmware `build_rp2350/src/bus_pirate6.uf2` and verify RAM/Flash limits <!-- id: 7.1 -->
