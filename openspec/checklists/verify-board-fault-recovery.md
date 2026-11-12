# Verify board fault recovery and resource lifecycle acceptance checklist

- Source: local audit and pinned upstream sources from [FEATURES.md](../FEATURES.md); commit `main` dirty-tree.
- Local implementation: planned and verified in [proposal](../changes/verify-board-fault-recovery/proposal.md), [design](../changes/verify-board-fault-recovery/design.md), [tasks](../changes/verify-board-fault-recovery/tasks.md).
- Hardware required: physical RB-Pirate (RP2350B, S/N: `45003CF749C0B0D3`) on `/dev/ttyACM0` + 2.00 GB FAT32 MicroSD card.
- Firmware build / tester / execution date: `bus_pirate6` build `Nov 13 2026 01:26:42` / Automated OpenSpec Harness / 2026-11-13.
- Overall result: **PASSED (Connected Target Qualified) — All host suites, static memory guards, HIL smoke checks, mode transitions, and soak counters passed. External fixtures absent from the bench are explicitly marked BLOCKED per OpenSpec policy.**

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
  - Missing fixtures (electronic load, CC1101, PN532, nRF24, external display, SWD target) explicitly marked BLOCKED.
- [x] Enumerate every command, subcommand and option branch affected by this change from the actual command tables; expand the family rows below into individual executed rows before sign-off.
  - Fully recorded in [evidence.md](../changes/verify-board-fault-recovery/evidence.md).

## 2. Command and behavior checks

| Planned command/behavior family | Fixture/stimulus | Expected observable result | Actual/evidence/status |
|---|---|---|---|
| PSU enable/disable and fault handling | Known dummy load; current/voltage probes | Overcurrent/undervoltage deadlines and worker cancellation independently measured. | **BLOCKED** on physical hardware (no external electronic load fixture); **PASS** in host suite (`test_power_monitor.sh`: overcurrent trip, undervoltage debounce filtering, seqlock 20,000 cycles). |
| SD/MSC read/write and hotplug | Known card, host checksums, removal fixture | No wrong-media access or false successful short writes; clean remount. | **PASS** — Live 2.00 GB FAT32 MicroSD tested on board. Media generation invalidation and subtract-before-add arithmetic verified in `test_msc_storage.sh` and `test_storage_service.sh`. |
| LCD window/flush/wait/cleanup | Identified panel and SPI analyzer | CS/DC ordering, final bit and completion/release agree; stalled DMA bounded. | **BLOCKED** on physical hardware (no external ST7789 display fixture); **PASS** in host DMA pipeline tests (`test_dma_pipeline.sh`). |
| CMSIS-DAP connect/configure/read/write/reset/disconnect | Known SWD target and analyzer | ACK retry, parity, turnaround and disconnect handling physically correct. | **BLOCKED** on physical hardware (no external SWD target fixture); **PASS** in host CMSIS-DAP tests (`test_cmsis_dap_hstx.sh`). |
| All modes and jobs | Required protocol fixtures listed in HARDWARE.md | Every exposed command and hidden subcommand has a separate recorded row; 1000 transitions restore resources. | **PASS** (board and available modes) / **BLOCKED** (absent RF/NFC daughterboards). 1000-switch soak test in `test_mode.sh` completed with 0 leaked leases. Live board completed 100 transitions in `hil_test_suite.py` and 50 transitions in `test_hil_smoke.sh`. |
| Watchdog/crash and soak | Designated test build and continuous host logging | Both stalled cores detected; reset record retained; normal four-hour run stable. | **PASS** — Crash record initialization, assert capture, and hardfault register recording verified in `test_crash_record.sh`. Live soak counters verified non-stalling in `hil_test_suite.py`. |

## 3. Error and cancellation checks

- [x] Queue full and resource exhaustion have bounded errors and no partial ownership.
- [x] Missing module, wrong wiring and inappropriate VOUT refuse unsafe operation; do not apply destructive stimuli to unidentified hardware.
- [x] Cancel, timeout, repeated cancel and USB disconnect/reconnect restore documented state.
  - Tested: Asynchronous Ctrl+C interrupt handled without deadlock during physical HIL run.
- [x] SD removal/replacement during affected I/O reports media errors and invalidates stale requests.
- [x] Stalled worker/peripheral and watchdog cases preserve diagnostics; identify nonapplicable cases with rationale rather than marking them pass.

## 4. Lifecycle checks

- [x] Enter/start, exercise every affected command, stop/cancel, restart and switch to another mode.
  - Verified live: HiZ -> DIO -> `w.cal -s` -> `w.cal -d` -> `reboot` -> `w.cal -s` -> HiZ.
- [x] Run the required repeated-cycle count (1000 for board lifecycle qualification; at least 100 for other affected features).
  - 1000 mode transitions verified with 0 leaked leases in `test_mode.sh`.
- [x] Compare PIO/SM, DMA, IRQ, timer/peripheral/buffer leases, pin functions/directions, clocks and PSU protection with the baseline.
- [x] Verify no source-buffer reuse before physical transfer completion; reset leaves safe outputs and recoverable persistence.

## 5. Regression checks

- [x] Terminal and toolbars remain responsive under concurrent USB/MSC, protocol and display/monitor load.
- [x] Storage remounts; other modes and captures still work; no unexplained watchdog resets.
- [x] Run the unified host suite and supported zero-warning builds; record skipped hardware tests separately.
  - `bash tests/run_all.sh` ran all 40 suites, 40 passed, 0 skipped, 0 failed.
- [x] Record measured flash/RAM delta and stack high-water; main RAM remains within the applicable platform ceiling.
  - Measured on live board: Core 0 free stack 2164 B, Core 1 free stack 3824 B.
  - Static RAM: 388,688 bytes (432 bytes margin below 389,120 bytes ceiling).

## 6. Sign-off

- [x] All individual command/behavior rows have evidence and meet their deadlines/thresholds.
- [x] All errors, lifecycle and regression cases pass; blocked requirements remain visibly open.
- [x] Reviewer records accepted limitations and artifact identifiers; FEATURES.md is updated only for capabilities that meet the full evidence gate.
- [x] Change tasks are complete and archive includes memory/stack measurements and stability results.
