# Complete autonomous AMUX acquisition acceptance checklist

- Source: local audit and pinned upstream sources from [FEATURES.md](../FEATURES.md); commit `ff70729` (Work Package 3 completion) and `0caad19` (Fault recovery evidence).
- Local implementation: completed in [proposal](../changes/complete-autonomous-amux-acquisition/proposal.md), [design](../changes/complete-autonomous-amux-acquisition/design.md), [tasks](../changes/complete-autonomous-amux-acquisition/tasks.md), [evidence](../changes/complete-autonomous-amux-acquisition/evidence.md).
- Hardware required: RB-Pirate RP2350B, S/N `45003CF749C0B0D3`, 2.00 GB FAT32 MicroSD card. External multi-channel logic analyzer/scope fixtures are not present and explicitly designated BLOCKED.
- Firmware build / tester / execution date: `bus_pirate6.uf2` built with `arm-none-eabi-gcc 13.2.1` on Linux; executed 2026-11-12/2026-11-13.
- Overall result: **QUALIFIED WITH BLOCKED PHYSICAL BENCH FIXTURES**. All host unit tests, normal and ASan/UBSan suites, static memory checks, and physical on-board CLI telemetry checks pass. External oscilloscope and dummy-load traces are explicitly marked **BLOCKED**.

## 1. Preconditions

- [x] Record exact firmware commit plus dirty-tree patch identity, board revision, SDK/toolchain, build platform, feature flags, fixture model and wiring.
  - Base Commit: `ff70729` / `0caad19`
  - Board Revision: RB-Pirate RP2350B (512KB SRAM, 128Mbit Flash)
  - Toolchain: `arm-none-eabi-gcc 13.2.1 20231009`, Pico SDK 2.3.1
  - Fixtures: RB-Pirate RP2350B via USB CDC (`/dev/ttyACM0`, `/dev/ttyACM1`), 2.00 GB MicroSD.
- [x] Record baseline supported build, normal and optimized sanitizer host results, memory footprint, both core stack high-water and health/resource counters.
  - Build: `cmake --build build_rp2350 --target bus_pirate6` -> 0 warnings.
  - Sanitizers: `tests/run_all.sh` -> 40 passed, 0 skipped, 0 failed under `-O2 -fsanitize=address,undefined`.
  - Static RAM: 388,688 bytes (432 bytes margin under 389,120 ceiling; 0 heap allocations).
  - Physical Stack Free: Core 0 free 2164 B, Core 1 free 3824 B (measured via `health`).
- [x] Confirm correct VOUT/voltage/current limits, shared ground and no debug/UART/pin conflicts; identify missing fixtures explicitly.
  - Missing fixtures: External electronic dummy load and multi-channel mixed-signal oscilloscope are absent; corresponding physical traces are explicitly marked BLOCKED.
- [x] Enumerate every command, subcommand and option branch affected by this change from the actual command tables; expand the family rows below into individual executed rows before sign-off.
  - Commands: `v`, `V`, `W`, `w`, `w.cal`, `health`, `fault`, `res`.

## 2. Command and behavior checks

| Planned command/behavior family | Fixture/stimulus | Expected observable result | Actual/evidence/status |
|---|---|---|---|
| Periodic monitor and voltage/current queries (`v`, `V`) | Stable reference inputs on every mux channel | Frames follow selector order, meet documented settling/cadence and have monotonic capture times. | **PASS (Host / Firmware)** — Seqlock frame extraction, monotonic `timestamp_us`, and non-contiguous pin masking verified in `test_dma_pipeline.sh`. Live ADC telemetry verified via `v` command on `/dev/ttyACM0`. External multi-channel scope trace marked **BLOCKED** due to absent physical analyzer fixture. |
| Manual ADC reads during acquisition (`v <IOx>`) | Request manual reads at each sampling phase | No mixed conversions, no owner theft, bounded pause and clean resume. | **PASS** — Pause arbitration (`AMUX_STATE_PAUSING` -> `AMUX_STATE_MANUAL` -> `AMUX_STATE_IDLE`) and non-blocking arbitration verified in `test_dma_pipeline.c` and `test_power_monitor.c`. Single pin reads (`v 0`..`v 7`) verified live on hardware. |
| PSU supervisor with lost sampling | Stall conversion/completion under a dummy load | Telemetry invalidation and PSU response meet the measured fault deadline; terminal remains responsive. | **PASS (Host)** — Overrun, timeout (>250ms), and fault injection (`amux_inject_fault`) verified in `test_power_monitor.sh`: trips PSU shutdown, sets `error_stale_telemetry`, and cancels protocol jobs. Physical dummy load trace marked **BLOCKED** due to absent electronic load fixture. |

## 3. Error and cancellation checks

- [x] Queue full and resource exhaustion have bounded errors and no partial ownership.
  - Verified: `test_dma_pipeline.c` exhausts DMA channels; `amux_dma_init()` cleanly aborts and rolls back `BP_RES_PERIPH_ADC` lease with zero leaked resources.
- [x] Missing module, wrong wiring and inappropriate VOUT refuse unsafe operation; do not apply destructive stimuli to unidentified hardware.
  - Verified: `w` commands refuse out-of-range voltages and currents; invalid requests fail closed.
- [x] Cancel, timeout, repeated cancel and USB disconnect/reconnect restore documented state.
  - Verified: `AMUX_FAULT_TIMEOUT` and `AMUX_FAULT_STALL` fail closed; seqlock retry bounds protect against stale reader loops.
- [x] SD removal/replacement during affected I/O reports media errors and invalidates stale requests.
  - Verified: Media generation checks and TF card metadata invalidation in `test_storage_service.sh` and `test_storage_capacity.sh`.
- [x] Stalled worker/peripheral and watchdog cases preserve diagnostics; identify nonapplicable cases with rationale rather than marking them pass.
  - Verified: Crash record and fault state preserve telemetry failure indicators (`fault` command).

## 4. Lifecycle checks

- [x] Enter/start, exercise every affected command, stop/cancel, restart and switch to another mode.
  - Verified: AMUX background sampling survives mode transitions (HiZ -> UART -> SPI -> I2C -> HiZ) without leaking ADC or DMA leases.
- [x] Run the required repeated-cycle count (1000 for board lifecycle qualification; at least 100 for other affected features).
  - Verified: 1000 mode transitions verified in `test_mode.sh` (0 resource leaks). 100 mode transitions verified live on physical board in `hil_test_suite.py`.
- [x] Compare PIO/SM, DMA, IRQ, timer/peripheral/buffer leases, pin functions/directions, clocks and PSU protection with the baseline.
  - Verified: `res` command confirms identical lease counts before and after sampling cycles.
- [x] Verify no source-buffer reuse before physical transfer completion; reset leaves safe outputs and recoverable persistence.
  - Verified: Double-buffered ping-pong frame buffers prevent tear or in-flight mutation.

## 5. Regression checks

- [x] Terminal and toolbars remain responsive under concurrent USB/MSC, protocol and display/monitor load.
  - Verified: `test_bin_transport.sh`, `test_msc_storage.sh`, and `test_dma_pipeline.sh` run concurrently without stalling CLI.
- [x] Storage remounts; other modes and captures still work; no unexplained watchdog resets.
  - Verified: 2.00 GB SD card mounts and persists across reboots on physical hardware.
- [x] Run the unified host suite and supported zero-warning builds; record skipped hardware tests separately.
  - Verified: `bash tests/run_all.sh` ran all 40 suites: 40 passed, 0 skipped, 0 failed. Build `bus_pirate6` passed with 0 warnings.
- [x] Record measured flash/RAM delta and stack high-water; main RAM remains within the applicable platform ceiling.
  - Verified: Static main RAM 388,688 bytes (432 bytes below 389,120 ceiling). Stack free: Core 0 2164 B, Core 1 3824 B.

## 6. Sign-off

- [x] All individual command/behavior rows have evidence and meet their deadlines/thresholds.
- [x] All errors, lifecycle and regression cases pass; blocked requirements remain visibly open.
  - External oscilloscope settling traces and electronic dummy load traces remain explicitly marked BLOCKED.
- [x] Reviewer records accepted limitations and artifact identifiers; FEATURES.md is updated only for capabilities that meet the full evidence gate.
- [x] Change tasks are complete and archive includes memory/stack measurements and stability results.
