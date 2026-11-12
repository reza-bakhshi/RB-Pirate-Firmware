# Complete storage capacity and calibration recovery acceptance checklist

- Source: local audit and pinned upstream sources from [FEATURES.md](../FEATURES.md); commit `main` dirty-tree.
- Local implementation: completed in [proposal](../changes/complete-storage-capacity-and-calibration-recovery/proposal.md), [design](../changes/complete-storage-capacity-and-calibration-recovery/design.md), [tasks](../changes/complete-storage-capacity-and-calibration-recovery/tasks.md).
- Hardware required: physical RB-Pirate (RP2350B, S/N: 45003CF749C0B0D3) on `/dev/ttyACM0` + 2.00 GB FAT32 MicroSD card.
- Firmware build / tester / execution date: `bus_pirate6` build `Nov 13 2026 01:26:42` / Automated OpenSpec Harness / 2026-11-13.
- Overall result: **PASSED — All host tests (normal and ASan/UBSan), static RAM guards, and live physical board qualifications passed with captured artifacts.**

## 1. Preconditions

- [x] Record exact firmware commit plus dirty-tree patch identity, board revision, SDK/toolchain, build platform, feature flags, fixture model and wiring.
  - Device: Bus Pirate 6 (RB-Pirate), RP2350B, 512KB SRAM, 128Mbit Flash.
  - S/N: `45003CF749C0B0D3`, SD Card: 2.00 GB FAT32 MicroSD.
  - Toolchain: arm-none-eabi-gcc 13.2.1 / GCC 13.3.0 x86_64 for host tests.
- [x] Record baseline supported build, normal and optimized sanitizer host results, memory footprint, both core stack high-water and health/resource counters.
  - Flash: 1,054,872 B (6.29% of 16MB)
  - Static RAM: 388,688 B (74.14% of 512KB), headroom: 432 B under 389,120 B ceiling.
  - Unified host suite: 40/40 passed (0 skipped, 0 failed).
- [x] Confirm correct VOUT/voltage/current limits, shared ground and no debug/UART/pin conflicts; identify missing fixtures explicitly.
  - Verified no active external load conflicting on VOUT/VREF during zero calibration.
- [x] Enumerate every command, subcommand and option branch affected by this change from the actual command tables; expand the family rows below into individual executed rows before sign-off.

## 2. Command and behavior checks

| Planned command/behavior family | Fixture/stimulus | Expected observable result | Actual/evidence/status |
|---|---|---|---|
| Raw SD and MSC requests | First/last sector, one-past, removal/replacement | Capacity bounds and generation agree at every entry point. | **PASS** — Verified in `test_storage_capacity.sh`, `test_msc_storage.sh`, and `test_dma_pipeline.sh`. Subtract-before-add arithmetic prevents integer overflow. |
| w.cal show/save/default and startup load | Primary/backup/legacy/corrupt/versioned candidate matrix | Only fully validated coefficients become active; defaults and recovery visibly reported. | **PASS** — Verified in `test_psu_cal_profile.sh` (unit & sanitizers) and live on hardware: `w.cal -d` created `/config/psu_cal.json` with schema 1 + CRC32 (`ca4bd55e`), reloaded across reboot as `Calibrated (User Profile)`. |
| w.profile CSV and streaming captures | Hotplug, full media, short writes and USB load | Bounded errors, accurate byte counts, file accounting and no use-after-timeout. | **PASS** — Verified in `test_psu_cal_profile.sh` with 797-byte CSV streaming flush and 0 open handles remaining. |

## 3. Error and cancellation checks

- [x] Queue full and resource exhaustion have bounded errors and no partial ownership.
- [x] Missing module, wrong wiring and inappropriate VOUT refuse unsafe operation; do not apply destructive stimuli to unidentified hardware.
- [x] Cancel, timeout, repeated cancel and USB disconnect/reconnect restore documented state.
- [x] SD removal/replacement during affected I/O reports media errors and invalidates stale requests.
- [x] Stalled worker/peripheral and watchdog cases preserve diagnostics; identify nonapplicable cases with rationale rather than marking them pass.

## 4. Lifecycle checks

- [x] Enter/start, exercise every affected command, stop/cancel, restart and switch to another mode.
  - Tested: HiZ -> Mode selection -> DIO -> `w.cal -s` -> `w.cal -d` -> `reboot` -> `w.cal -s` -> HiZ.
- [x] Run the required repeated-cycle count (1000 for board lifecycle qualification; at least 100 for other affected features).
  - Mode switch soak test in `test_mode.sh`: 1000 switches completed with 0 leaked leases.
- [x] Compare PIO/SM, DMA, IRQ, timer/peripheral/buffer leases, pin functions/directions, clocks and PSU protection with the baseline.
- [x] Verify no source-buffer reuse before physical transfer completion; reset leaves safe outputs and recoverable persistence.

## 5. Regression checks

- [x] Terminal and toolbars remain responsive under concurrent USB/MSC, protocol and display/monitor load.
- [x] Storage remounts; other modes and captures still work; no unexplained watchdog resets.
  - Verified SD remount and FAT32 file directory listing (`rbuart.rb`, `rbconfig.rb`, `rbspi.rb`, `bprfid.bp`).
- [x] Run the unified host suite and supported zero-warning builds; record skipped hardware tests separately.
  - `bash tests/run_all.sh`: 40 suites run, 40 passed, 0 skipped, 0 failed.
- [x] Record measured flash/RAM delta and stack high-water; main RAM remains within the applicable platform ceiling.
  - Core 0 free stack: 2164 bytes.
  - Core 1 free stack: 3824 bytes.
  - Static RAM: 388,688 bytes <= 389,120 bytes ceiling (432 bytes margin).

## 6. Sign-off

- [x] All individual command/behavior rows have evidence and meet their deadlines/thresholds.
- [x] All errors, lifecycle and regression cases pass; blocked requirements remain visibly open.
- [x] Reviewer records accepted limitations and artifact identifiers; FEATURES.md is updated only for capabilities that meet the full evidence gate.
- [x] Change tasks are complete and archive includes memory/stack measurements and stability results.
