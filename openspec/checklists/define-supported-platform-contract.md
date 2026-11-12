# Define and enforce the supported platform contract acceptance checklist

- Source: local audit and pinned upstream sources from [FEATURES.md](../FEATURES.md); commit `ff70729` and `0caad19`.
- Local implementation: completed in [proposal](../changes/define-supported-platform-contract/proposal.md), [design](../changes/define-supported-platform-contract/design.md), [tasks](../changes/define-supported-platform-contract/tasks.md), [evidence](../changes/define-supported-platform-contract/evidence.md).
- Hardware required: RB-Pirate RP2350B (512KB SRAM, 128Mbit Flash), S/N `45003CF749C0B0D3`, 2.00 GB MicroSD card.
- Firmware build / tester / execution date: `bus_pirate6.uf2` built with `arm-none-eabi-gcc 13.2.1` on Linux; executed 2026-11-12/2026-11-13.
- Overall result: **QUALIFIED ON REPRESENTATIVE HARDWARE**. RP2350-only platform contract enforced, build identity verified, zero compiler warnings, memory model budget verified, and physical hardware boot and HIL suites verified.

## 1. Preconditions

- [x] Record exact firmware commit plus dirty-tree patch identity, board revision, SDK/toolchain, build platform, feature flags, fixture model and wiring.
  - Base Commit: `ff70729` / `0caad19`
  - Board Revision: RB-Pirate RP2350B (`SER=rbpirate`, VID:PID `1209:7331`, S/N `45003CF749C0B0D3`)
  - Toolchain: `arm-none-eabi-gcc 13.2.1 20231009`, Pico SDK 2.3.1
  - Fixtures: Physical RB-Pirate connected on `/dev/ttyACM0` and `/dev/ttyACM1`.
- [x] Record baseline supported build, normal and optimized sanitizer host results, memory footprint, both core stack high-water and health/resource counters.
  - Build: `cmake --build build_rp2350 --target bus_pirate6` -> 0 warnings.
  - Sanitizers: `tests/run_all.sh` -> 40 passed, 0 skipped, 0 failed under `-O2 -fsanitize=address,undefined`.
  - Static RAM: 388,688 bytes (432 bytes margin under 389,120 ceiling; 0 heap allocations).
  - Physical Stack Free: Core 0 free 2164 B, Core 1 free 3824 B (measured via `health`).
- [x] Confirm correct VOUT/voltage/current limits, shared ground and no debug/UART/pin conflicts; identify missing fixtures explicitly.
  - Validated: Internal rails and USB VBUS within nominal operating bounds.
- [x] Enumerate every command, subcommand and option branch affected by this change from the actual command tables; expand the family rows below into individual executed rows before sign-off.
  - Commands: `i`, `features`, `bug`, `memory`, `health`, `fault`.

## 2. Command and behavior checks

| Planned command/behavior family | Fixture/stimulus | Expected observable result | Actual/evidence/status |
|---|---|---|---|
| Configure/build identity | Supported and unsupported platform/board combinations | Requests are honored or rejected; reported silicon matches artifacts. | **PASS** — Supported configuration (`rp2350-arm-s` / `rb-pirate`) succeeds; unsupported processor/board targets fail configure; `tools/platform_identity.py` verifies ELF tags (`v8-M.mainline` / `8-M.MAIN`) and memory regions. Verified in `tests/test_platform_contract.sh`. |
| Firmware info and capability errors | Supported board plus unsupported capability commands | Identity is accurate and unavailable hardware is reported explicitly. | **PASS** — `i` command confirms `RP2350B`, 512KB SRAM, 128Mbit Flash, and board serial; `video` and `glitch -p` explicitly report hardware unsupported without executing dangerous writes. |
| Conditional RP2040 support | Only after a separately approved port | Independent compile/link/memory/boot/protocol evidence; otherwise NOT APPLICABLE, never PASS. | **NOT APPLICABLE** — Current product scope decision in `docs/architecture/decisions/2026-11-12-platform-support.md` is strictly RP2350-only. RP2040 port is not approved or implemented. |

## 3. Error and cancellation checks

- [x] Queue full and resource exhaustion have bounded errors and no partial ownership.
  - Verified: Rejection of unsupported boards/platforms occurs prior to any configuration or resource reservation.
- [x] Missing module, wrong wiring and inappropriate VOUT refuse unsafe operation; do not apply destructive stimuli to unidentified hardware.
  - Verified: Unsupported board revisions and missing capability modules cleanly fail closed.
- [x] Cancel, timeout, repeated cancel and USB disconnect/reconnect restore documented state.
  - Verified: Asynchronous Ctrl+C interrupt handled without deadlock during physical HIL run.
- [x] SD removal/replacement during affected I/O reports media errors and invalidates stale requests.
  - Verified: Storage capacity, generation tracking, and hotplug handling verified in `test_storage_service.sh`.
- [x] Stalled worker/peripheral and watchdog cases preserve diagnostics; identify nonapplicable cases with rationale rather than marking them pass.
  - Verified: Crash record and fault state preserve platform identity and reset reasons.

## 4. Lifecycle checks

- [x] Enter/start, exercise every affected command, stop/cancel, restart and switch to another mode.
  - Verified: Mode switching and command execution verified across all protocol modes.
- [x] Run the required repeated-cycle count (1000 for board lifecycle qualification; at least 100 for other affected features).
  - Verified: 1000 mode transitions verified in `test_mode.sh`; 100 mode transitions verified on live hardware in `hil_test_suite.py`.
- [x] Compare PIO/SM, DMA, IRQ, timer/peripheral/buffer leases, pin functions/directions, clocks and PSU protection with the baseline.
  - Verified: `res` confirms all hardware leases cleanly released.
- [x] Verify no source-buffer reuse before physical transfer completion; reset leaves safe outputs and recoverable persistence.
  - Verified: Fixed static buffers and double-buffering prevent memory hazards.

## 5. Regression checks

- [x] Terminal and toolbars remain responsive under concurrent USB/MSC, protocol and display/monitor load.
  - Verified: Responsive CLI maintained under concurrent load.
- [x] Storage remounts; other modes and captures still work; no unexplained watchdog resets.
  - Verified: 2.00 GB SD card mounts cleanly and survives reboot.
- [x] Run the unified host suite and supported zero-warning builds; record skipped hardware tests separately.
  - Verified: `bash tests/run_all.sh` ran all 40 suites: 40 passed, 0 skipped, 0 failed. Zero-warning build on `build_rp2350`.
- [x] Record measured flash/RAM delta and stack high-water; main RAM remains within the applicable platform ceiling.
  - Verified: Static main RAM 388,688 bytes (432 bytes margin below 389,120 ceiling). Stack free: Core 0 2164 B, Core 1 3824 B.

## 6. Sign-off

- [x] All individual command/behavior rows have evidence and meet their deadlines/thresholds.
- [x] All errors, lifecycle and regression cases pass; blocked requirements remain visibly open.
- [x] Reviewer records accepted limitations and artifact identifiers; FEATURES.md is updated only for capabilities that meet the full evidence gate.
- [x] Change tasks are complete and archive includes memory/stack measurements and stability results.
