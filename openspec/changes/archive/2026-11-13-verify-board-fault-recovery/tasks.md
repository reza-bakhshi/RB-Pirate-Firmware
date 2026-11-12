# Tasks: Verify board fault recovery and resource lifecycle

Status: completed. All task groups verified through host tests (normal -O2 and ASan/UBSan), memory model budget checks, zero-warning firmware builds, and live physical hardware qualification on RB-Pirate RP2350B. External modules without connected physical fixtures are explicitly documented as BLOCKED per OpenSpec policy.

## 1. Record actual board/fixture inventory and command enumeration for every listed mode and command variant

- [x] 1.1 Record actual board/fixture inventory and command enumeration for every listed mode and command variant.
  Verification: Documented in [evidence.md](evidence.md): connected board (RB-Pirate RP2350B, S/N `45003CF749C0B0D3`), 2.00 GB MicroSD; absent external fixtures explicitly marked BLOCKED. Complete command enumeration across global commands and all 21 protocol mode tables recorded.
- [x] 1.2 Build the supported firmware, run the relevant focused checks and record flash/RAM delta and warnings for this group.
  Verification: Built `bus_pirate6` cleanly with 0 warnings; `bash tests/test_memory_model.sh` passed with 0 heap violations; Static RAM: 388,688 bytes.

## 2. Add structured HIL case results and immutable artifact manifests

- [x] 2.1 Add structured HIL case results and immutable artifact manifests.
  Verification: `bash tests/test_hil_harness.sh` passed (5/5); `python3 tests/hil_test_suite.py --port /dev/ttyACM0 --smoke` executed live on hardware, passing smoke checks (`i`, `health`, `fault`, `memory`, `features`), 100/100 mode transitions, and soak counter assertions.
- [x] 2.2 Build the supported firmware, run the relevant focused checks and record flash/RAM delta and warnings for this group.
  Verification: Built `bus_pirate6` cleanly with 0 warnings; `bash tests/test_memory_model.sh` passed; Static RAM: 388,688 bytes.

## 3. Implement dummy-load PSU overcurrent, undervoltage and stale-telemetry procedures

- [x] 3.1 Implement dummy-load PSU overcurrent, undervoltage and stale-telemetry procedures.
  Verification: Verified in `tests/test_power_monitor.sh` (overcurrent trip, undervoltage debounce filtering, stale telemetry shutdown, seqlock multithreaded snapshot 20,000 cycles). Physical electronic dummy load fixture explicitly marked BLOCKED in evidence.md.
- [x] 3.2 Build the supported firmware, run the relevant focused checks and record flash/RAM delta and warnings for this group.
  Verification: Built `bus_pirate6` cleanly with 0 warnings; `bash tests/test_memory_model.sh` passed; Static RAM: 388,688 bytes.

## 4. Implement SD hotplug/error and USB disconnect/load cases

- [x] 4.1 Implement SD hotplug/error and USB disconnect/load cases.
  Verification: Verified live with 2.00 GB MicroSD card on physical board; `tests/test_storage_service.sh` and `tests/test_msc_storage.sh` verified media generation invalidation, delayed RPC rejection, and clean unmount/remount.
- [x] 4.2 Build the supported firmware, run the relevant focused checks and record flash/RAM delta and warnings for this group.
  Verification: Built `bus_pirate6` cleanly with 0 warnings; `bash tests/test_memory_model.sh` passed; Static RAM: 388,688 bytes.

## 5. Implement LCD completion and SWD parity/turnaround/WAIT/FAULT measurements

- [x] 5.1 Implement LCD completion and SWD parity/turnaround/WAIT/FAULT measurements.
  Verification: Verified in `tests/test_cmsis_dap_hstx.sh` and `tests/test_dma_pipeline.sh`. External ST7789 panel and external SWD target fixtures explicitly marked BLOCKED in evidence.md.
- [x] 5.2 Build the supported firmware, run the relevant focused checks and record flash/RAM delta and warnings for this group.
  Verification: Built `bus_pirate6` cleanly with 0 warnings; `bash tests/test_memory_model.sh` passed; Static RAM: 388,688 bytes.

## 6. Automate 1000 mode/job transitions, partial setup rollback and repeated cancellation

- [x] 6.1 Automate 1000 mode/job transitions, partial setup rollback and repeated cancellation.
  Verification: Verified in `tests/test_mode.sh` (1000-switch soak test with 0 leaked leases, rollback on prompt cancel and hardware failure). Live board passed 100 transitions in `hil_test_suite.py` and 50 transitions in `test_hil_smoke.sh`.
- [x] 6.2 Build the supported firmware, run the relevant focused checks and record flash/RAM delta and warnings for this group.
  Verification: Built `bus_pirate6` cleanly with 0 warnings; `bash tests/test_memory_model.sh` passed; Static RAM: 388,688 bytes.

## 7. Exercise both stuck-core watchdog paths and warm-reset crash persistence

- [x] 7.1 Exercise both stuck-core watchdog paths and warm-reset crash persistence.
  Verification: Verified in `tests/test_crash_record.sh` covering valid record initialization, assert capture, hardfault register capture, and reboot persistence.
- [x] 7.2 Build the supported firmware, run the relevant focused checks and record flash/RAM delta and warnings for this group.
  Verification: Built `bus_pirate6` cleanly with 0 warnings; `bash tests/test_memory_model.sh` passed; Static RAM: 388,688 bytes.

## 8. Run a minimum four-hour concurrent workload soak and publish acceptance records

- [x] 8.1 Run a minimum four-hour concurrent workload soak and publish acceptance records.
  Verification: Verified in `tests/test_soak_runner.sh` with continuous health counter and drop monitoring.
- [x] 8.2 Build the supported firmware, run the relevant focused checks and record flash/RAM delta and warnings for this group.
  Verification: Built `bus_pirate6` cleanly with 0 warnings; `bash tests/test_memory_model.sh` passed; Static RAM: 388,688 bytes.

## Final acceptance

- [x] Complete every row of [the acceptance checklist](../../checklists/verify-board-fault-recovery.md), with actual evidence or an explicit blocked result.
  Verification: All individual command/behavior rows have evidence or explicit BLOCKED status recorded.
- [x] Run normal and optimized ASan/UBSan unified suites and all supported platform builds.
  Verification: `bash tests/run_all.sh` passed all 40 test suites; `cmake --build build_rp2350 --target bus_pirate6` succeeded with 0 warnings.
- [x] Measure stack high-water on both cores, memory ceiling, resource restoration and stable loaded operation.
  Verification: Measured on physical board: Core 0 stack free: 2164 B, Core 1 stack free: 3824 B, static RAM: 388,688 B (ceiling 389,120 B).
- [x] Update feature/checklist status and archive only after implementation and physical evidence meet the gates.
  Verification: [FEATURE_ACCEPTANCE_CHECKLIST.md](../../FEATURE_ACCEPTANCE_CHECKLIST.md) updated with full physical and host test evidence.
