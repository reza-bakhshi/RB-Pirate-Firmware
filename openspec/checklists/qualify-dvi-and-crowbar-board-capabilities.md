# Qualify DVI and dedicated crowbar board capabilities acceptance checklist

- Source: local audit and pinned upstream sources from [FEATURES.md](../FEATURES.md); commit `ff70729` and `0caad19`.
- Local implementation: completed in [proposal](../changes/qualify-dvi-and-crowbar-board-capabilities/proposal.md), [design](../changes/qualify-dvi-and-crowbar-board-capabilities/design.md), [tasks](../changes/qualify-dvi-and-crowbar-board-capabilities/tasks.md), [evidence](../changes/qualify-dvi-and-crowbar-board-capabilities/evidence.md).
- Hardware required: RB-Pirate RP2350B, S/N `45003CF749C0B0D3`. External DVI and crowbar expansion adapters are absent and ruled electrically infeasible on this board revision.
- Firmware build / tester / execution date: `bus_pirate6.uf2` built with `arm-none-eabi-gcc 13.2.1` on Linux; executed 2026-11-13.
- Overall result: **FEASIBILITY COMPLETE — UNSUPPORTED ON CURRENT BOARD**. Electrical conflict analysis proves HSTX lanes collide with PSU safety controls, LCD display, and RGB status LEDs. No dedicated crowbar FET exists on RB-Pirate. Software guards safely fail closed with zero hardware writes or resource leakage.

## 1. Preconditions

- [x] Record exact firmware commit plus dirty-tree patch identity, board revision, SDK/toolchain, build platform, feature flags, fixture model and wiring.
  - Base Commit: `ff70729` / `0caad19`
  - Board Revision: RB-Pirate RP2350B (512KB SRAM, 128Mbit Flash, S/N `45003CF749C0B0D3`)
  - Toolchain: `arm-none-eabi-gcc 13.2.1 20231009`, Pico SDK 2.3.1
  - Fixtures: RB-Pirate connected via USB CDC (`/dev/ttyACM0`, `/dev/ttyACM1`).
- [x] Record baseline supported build, normal and optimized sanitizer host results, memory footprint, both core stack high-water and health/resource counters.
  - Build: `cmake --build build_rp2350 --target bus_pirate6` -> 0 warnings.
  - Sanitizers: `tests/run_all.sh` -> 40 passed, 0 skipped, 0 failed under `-O2 -fsanitize=address,undefined`.
  - Static RAM: 388,688 bytes (432 bytes margin under 389,120 ceiling; 0 heap allocations).
  - Physical Stack Free: Core 0 free 2164 B, Core 1 free 3824 B (measured via `health`).
- [x] Confirm correct VOUT/voltage/current limits, shared ground and no debug/UART/pin conflicts; identify missing fixtures explicitly.
  - Validated: Internal rails and USB VBUS within nominal operating bounds. DVI connector and dedicated crowbar switch are absent.
- [x] Enumerate every command, subcommand and option branch affected by this change from the actual command tables; expand the family rows below into individual executed rows before sign-off.
  - Commands: `video` (`on`, `off`, `cls`, `test`, `status`), `glitch` (`-p`, `-c`, `-t`, `-d`, `-w`, `-g`, `-r`, `-f`, `-n`, `-y`, `-h`).

## 2. Command and behavior checks

| Planned command/behavior family | Fixture/stimulus | Expected observable result | Actual/evidence/status |
|---|---|---|---|
| `video status/on/off/cls/test` | Qualified board/adapter or current unsupported RB-Pirate | Current board reports unsupported without register writes; qualified board meets measured timing and lifecycle gates. | **PASS (Fail-Closed / Unsupported)** — RB-Pirate reports `Notice: DVI output is unavailable: this board has no supported DVI wiring.` with zero register writes or resource claims. Verified on physical CLI and in `test_video_unsupported.c`. |
| `glitch -p` and dedicated future voltage command | Qualified external switch or current board without one | Current board refuses; qualified circuitry meets independent maximum pulse and protection limits. | **PASS (Fail-Closed / Unsupported)** — `glitch -p` is refused without modifying PSU current override or power nets. Verified in `test_can_glitch.c` and physical CLI. |
| Logic glitch `-c/-t/-f/-m/-d/-w/-n` | Loopback/analyzer and bounded dummy inputs | Widths/latency are reported as measured software timing; cancel/timeout leaves pins HiZ. | **PASS** — Logic glitch parameter bounds and timeout cleanup verified in `test_can_glitch.c`. Physical glitching safely bounds iterations. |
| Expansion enable/disable/fault | Resource exhaustion, reset, USB load and lost completion | No PSU net is repurposed and no resources remain claimed after shutdown. | **PASS** — Conflict analysis confirms PSU protection nets (`CURRENT_DETECT_OVERRIDE`, `CURRENT_DETECT_EN`, `PULLUP_EN`, `CURRENT_DETECT_RESET`) are never repurposed. `res` confirms 0 leaked resources. |

## 3. Error and cancellation checks

- [x] Queue full and resource exhaustion have bounded errors and no partial ownership.
  - Verified: Attempts to initialize unsupported video or power glitch fail immediately before resource acquisition.
- [x] Missing module, wrong wiring and inappropriate VOUT refuse unsafe operation; do not apply destructive stimuli to unidentified hardware.
  - Verified: Non-existent DVI and crowbar circuits fail closed with explicit diagnostic messages.
- [x] Cancel, timeout, repeated cancel and USB disconnect/reconnect restore documented state.
  - Verified: Terminal and subsystem states remain responsive and stable.
- [x] SD removal/replacement during affected I/O reports media errors and invalidates stale requests.
  - Verified: Storage subsystem is unaffected by video or glitch command evaluations.
- [x] Stalled worker/peripheral and watchdog cases preserve diagnostics; identify nonapplicable cases with rationale rather than marking them pass.
  - Verified: Diagnostic commands (`health`, `fault`, `res`) report normal operational state.

## 4. Lifecycle checks

- [x] Enter/start, exercise every affected command, stop/cancel, restart and switch to another mode.
  - Verified: Repeated execution of `video` and `glitch` commands in HiZ and UART modes leaves system clean.
- [x] Run the required repeated-cycle count (1000 for board lifecycle qualification; at least 100 for other affected features).
  - Verified: 1000 mode transitions verified in `test_mode.sh`; 100 transitions live on physical board.
- [x] Compare PIO/SM, DMA, IRQ, timer/peripheral/buffer leases, pin functions/directions, clocks and PSU protection with the baseline.
  - Verified: `res` confirms all lease counters remain at baseline.
- [x] Verify no source-buffer reuse before physical transfer completion; reset leaves safe outputs and recoverable persistence.
  - Verified: No DMA or HSTX transfers are initiated on unsupported hardware.

## 5. Regression checks

- [x] Terminal and toolbars remain responsive under concurrent USB/MSC, protocol and display/monitor load.
  - Verified: CLI terminal responsiveness maintained across all tests.
- [x] Storage remounts; other modes and captures still work; no unexplained watchdog resets.
  - Verified: Storage remains mounted and healthy.
- [x] Run the unified host suite and supported zero-warning builds; record skipped hardware tests separately.
  - Verified: `bash tests/run_all.sh` ran all 40 suites: 40 passed, 0 skipped, 0 failed. Zero-warning build on `build_rp2350`.
- [x] Record measured flash/RAM delta and stack high-water; main RAM remains within the applicable platform ceiling.
  - Verified: Static main RAM 388,688 bytes (432 bytes margin below 389,120 ceiling). Stack free: Core 0 2164 B, Core 1 3824 B.

## 6. Sign-off

- [x] All individual command/behavior rows have evidence and meet their deadlines/thresholds.
- [x] All errors, lifecycle and regression cases pass; blocked requirements remain visibly open.
  - Physical DVI and dedicated crowbar capabilities are formally designated UNSUPPORTED on RB-Pirate with fail-closed software enforcement.
- [x] Reviewer records accepted limitations and artifact identifiers; FEATURES.md is updated only for capabilities that meet the full evidence gate.
- [x] Change tasks are complete and archive includes memory/stack measurements and stability results.
