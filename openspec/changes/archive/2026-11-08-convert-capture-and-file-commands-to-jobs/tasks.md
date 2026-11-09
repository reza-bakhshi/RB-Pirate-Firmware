# Tasks: Convert Capture and File Commands to Jobs

- [x] 1. Convert `txtest` (`src/commands/global/txtest.c`) into a cancellable operation checking `bp_job_check_cancelled()` with safe buffer lease teardown (ARCH-302) <!-- id: 1.1 -->
- [x] 2. Convert global `dump` (`src/commands/global/dump.c`) with periodic cancellation checks and clean file close on abort (ARCH-303) <!-- id: 2.1 -->
- [x] 3. Convert SPI flash commands (`spiflash_dump`, `spiflash_load`, `spiflash_erase_verify`, `spiflash_write_test`, `spiflash_write_verify`, `spiflash_force_dump`) into cancellable loops (ARCH-303) <!-- id: 3.1 -->
- [x] 4. Convert EEPROM base operations (`eeprom_read`, `eeprom_write`, `eeprom_verify`, `eeprom_erase`, `eeprom_test`) and `i2c` dump into cancellable loops (ARCH-303) <!-- id: 4.1 -->
- [x] 5. Convert LCDTAP display capture BMP export and command dump to check cooperative cancellation (ARCH-308) <!-- id: 5.1 -->
- [x] 6. Harden `edit.c`, `hexedit.c`, and `ui_mem_gui.c` to guarantee safe big buffer lease release and clean file close on exit or abort (ARCH-310) <!-- id: 6.1 -->
- [x] 7. Ensure logic analyzer sample dumps handle cancellation without blocking or leaking DMA leases (ARCH-304) <!-- id: 7.1 -->
- [x] 8. Implement dedicated host test suite `tests/test_capture_file_jobs.sh` (with ASan and UBSan) validating cancellation, progress, and file teardown <!-- id: 8.1 -->
- [x] 9. Run all host test suites in `tests/*.sh` and confirm zero failures under AddressSanitizer and UndefinedBehaviorSanitizer <!-- id: 9.1 -->
- [x] 10. Build RP2350 target `bus_pirate6.uf2` and verify static RAM <= 380 KB and fixed 4096-byte stacks <!-- id: 10.1 -->
