# Tasks: Convert RF and IR Commands to Jobs

- [x] 1. Implement `bp_job_dispatch()` and automatic `BP_RES_OWNER_JOB` resource lease cleanup in `src/kernel/bp_job.h` and `src/kernel/bp_job.c` <!-- id: 1.1 -->
- [x] 2. Convert Sub-GHz CC1101 long-running commands (`scan`, `sweep`, `receive`, `record`, `waterfall`, `jam`, `bruteforce`) into cancellable jobs (ARCH-305) <!-- id: 2.1 -->
- [x] 3. Convert RFID PN532 long-running commands (`scanner` inventory, `read`, `write`) into cancellable jobs (ARCH-306) <!-- id: 3.1 -->
- [x] 4. Convert RF24 nRF24L01 long-running commands (`scan`, `sweep`, `waterfall`, `receive`, `jam`) into cancellable jobs (ARCH-307) <!-- id: 4.1 -->
- [x] 5. Convert Infrared commands (`record`, `send`, `replay`, `jam`) into cancellable jobs <!-- id: 5.1 -->
- [x] 6. Convert PS/2 and USB sniffer capture loops into cancellable jobs with tracked peripheral leases (ARCH-308, ARCH-309) <!-- id: 6.1 -->
- [x] 7. Integrate FALA logic analyzer DMA channel leasing with `bp_res_*` and clean teardown (ARCH-313) <!-- id: 7.1 -->
- [x] 8. Implement dedicated host test suite `tests/test_rf_ir_jobs.sh` (with ASan and UBSan) validating dispatch, progress, timeout, and cancellation <!-- id: 8.1 -->
- [x] 9. Run all host test suites in `tests/*.sh` and confirm zero failures under AddressSanitizer and UndefinedBehaviorSanitizer <!-- id: 9.1 -->
- [x] 10. Build RP2350 target `bus_pirate6.uf2` and verify static RAM <= 380 KB and fixed 4096-byte stacks <!-- id: 10.1 -->
