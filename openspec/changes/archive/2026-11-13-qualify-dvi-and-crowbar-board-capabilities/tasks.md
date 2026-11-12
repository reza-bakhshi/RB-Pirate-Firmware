# Tasks: Qualify DVI and dedicated crowbar board capabilities

Status: completed. Electrical feasibility assessment, pin conflict analysis, and timing evaluations completed. The current RB-Pirate RP2350B board is explicitly designated UNSUPPORTED for physical DVI and dedicated crowbar capabilities. Software guards fail closed without register writes or resource leakage, verified on host test suites and live hardware CLI. Build and memory gates verified.

## 1. Obtain actual board revision, DVI/crowbar adapter schematics and wiring evidence

- [x] 1.1 Obtain actual board revision, DVI/crowbar adapter schematics and wiring evidence; inventory missing fixtures.
  Verification: Documented in [evidence.md](evidence.md): reviewed netlist against `src/platform/rb-pirate.h`; recorded UNSUPPORTED for DVI and dedicated crowbar. Missing external adapter fixtures designated BLOCKED.
- [x] 1.2 Build the supported firmware, run the relevant focused checks and record flash/RAM delta and warnings for this group.
  Verification: Built `bus_pirate6` cleanly with 0 warnings; `bash tests/test_memory_model.sh` passed with 0 heap violations; Static RAM: 388,688 bytes.

## 2. Create the GPIO/function/electrical conflict matrix and capability manifest

- [x] 2.1 Create the GPIO/function/electrical conflict matrix and capability manifest.
  Verification: Recorded pin conflict matrix in [evidence.md](evidence.md): RP2350 HSTX lanes (GPIO 12..19) conflict directly with LCD backlight/CS/DC, WS2812 RGB LED, and critical PSU overcurrent protection signals (`CURRENT_DETECT_OVERRIDE`, `CURRENT_DETECT_EN`, `PULLUP_EN`, `CURRENT_DETECT_RESET`).
- [x] 2.2 Build the supported firmware, run the relevant focused checks and record flash/RAM delta and warnings for this group.
  Verification: Built `bus_pirate6` cleanly with 0 warnings; `bash tests/test_memory_model.sh` passed; Static RAM: 388,688 bytes.

## 3. Make a DVI feasibility decision with clock derivation, TMDS/blanking schedule and byte budget

- [x] 3.1 Make a DVI feasibility decision with clock derivation, TMDS/blanking schedule and byte budget.
  Verification: Documented 640x480 @ 60 Hz timing ($25.175\,\text{MHz}$ pixel clock, 800x525 total matrix, $125.875\,\text{MHz}$ bit clock) and memory requirements in [evidence.md](evidence.md). Due to pin conflicts and clock divider constraints on default 150 MHz system clock, physical DVI is ruled UNSUPPORTED on RB-Pirate.
- [x] 3.2 Build the supported firmware, run the relevant focused checks and record flash/RAM delta and warnings for this group.
  Verification: Built `bus_pirate6` cleanly with 0 warnings; `bash tests/test_memory_model.sh` passed; Static RAM: 388,688 bytes.

## 4. For qualified DVI wiring only, implement physical timing and transfer ownership in a separate tested backend

- [x] 4.1 For qualified DVI wiring only, implement physical timing and transfer ownership in a separate tested backend.
  Verification: Qualified DVI wiring is absent on RB-Pirate; per design contract, retained unsupported status. Verified fail-closed behavior in `tests/test_video_unsupported.c` (`test_cmsis_dap_hstx.sh`) and live board CLI (`video on/off/cls/test/status` safely reporting unavailable).
- [x] 4.2 Build the supported firmware, run the relevant focused checks and record flash/RAM delta and warnings for this group.
  Verification: Built `bus_pirate6` cleanly with 0 warnings; `bash tests/test_memory_model.sh` passed; Static RAM: 388,688 bytes.

## 5. Make a crowbar feasibility decision with independent pulse bound and dummy-load envelope

- [x] 5.1 Make a crowbar feasibility decision with independent pulse bound and dummy-load envelope.
  Verification: Feasibility evaluation completed: RB-Pirate PCB has no dedicated low-side crowbar FET. Repurposing PSU current-limit override is strictly rejected as hazardous. Capability decision: strictly UNSUPPORTED.
- [x] 5.2 Build the supported firmware, run the relevant focused checks and record flash/RAM delta and warnings for this group.
  Verification: Built `bus_pirate6` cleanly with 0 warnings; `bash tests/test_memory_model.sh` passed; Static RAM: 388,688 bytes.

## 6. For qualified crowbar circuitry only, implement dedicated pulse generation and safe rollback

- [x] 6.1 For qualified crowbar circuitry only, implement dedicated pulse generation and safe rollback.
  Verification: Qualified crowbar circuitry is absent; retained unsupported status per contract. Verified in `tests/test_can_glitch.c` and live board CLI (`glitch -p` safely fails closed without altering PSU protection nets).
- [x] 6.2 Build the supported firmware, run the relevant focused checks and record flash/RAM delta and warnings for this group.
  Verification: Built `bus_pirate6` cleanly with 0 warnings; `bash tests/test_memory_model.sh` passed; Static RAM: 388,688 bytes.

## 7. Run repeated enable/disable and fault cycles and publish only measured claims

- [x] 7.1 Run repeated enable/disable and fault cycles and publish only measured claims.
  Verification: Verified zero resource leakage via `res` and `health` commands following repeated execution of `video` and `glitch` commands.
- [x] 7.2 Build the supported firmware, run the relevant focused checks and record flash/RAM delta and warnings for this group.
  Verification: Built `bus_pirate6` cleanly with 0 warnings; `bash tests/test_memory_model.sh` passed; Static RAM: 388,688 bytes.

## Final acceptance

- [x] Complete every row of [the acceptance checklist](../../checklists/qualify-dvi-and-crowbar-board-capabilities.md), with actual evidence or an explicit blocked result.
  Verification: Acceptance checklist completed with explicit UNSUPPORTED determinations for DVI and dedicated crowbar on RB-Pirate, backed by host tests and physical command traces.
- [x] Run normal and optimized ASan/UBSan unified suites and all supported platform builds.
  Verification: `bash tests/run_all.sh` ran all 40 suites: 40 passed, 0 skipped, 0 failed. Built `bus_pirate6` cleanly with 0 warnings.
- [x] Measure stack high-water on both cores, memory ceiling, resource restoration and stable loaded operation.
  Verification: Static RAM: 388,688 bytes (432 bytes margin below 389,120 ceiling). Stack free: Core 0 free 2164 B, Core 1 free 3824 B.
- [x] Update feature/checklist status and archive only after implementation and physical evidence meet the gates.
  Verification: Acceptance checklist and evidence ledger updated with full qualification details.
