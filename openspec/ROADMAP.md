# RB-Pirate OpenSpec Program Plan

> This is the execution source of truth for the stability and control/data-plane rearchitecture.
> It is deliberately a **program plan**, not a single OpenSpec change. Each numbered change below
> becomes its own `openspec/changes/<name>/` folder with `proposal.md`, delta specs, `design.md`
> and `tasks.md`, and is archived independently.

Working branch: `refactor/kernel`

---

## 1. How this program is executed

1. Pick the next change whose dependencies are archived.
2. Create its folder with `openspec new change <name>`.
3. Write its artifacts using the templates and the project rules in `openspec/config.yaml`.
4. Run `openspec validate --all --strict`.
5. Implement with `/opsx-apply` (OpenCode: `opsx-apply`), updating `tasks.md` as work progresses.
6. Run the change's verification tasks on the host and on a real RB-Pirate.
7. Archive the change with `openspec archive <name>`; the delta specs merge into `openspec/specs/`.
8. Update the status column in this file.

A change is not complete until:

- every tracked task is checked,
- the build and the relevant host tests pass,
- the hardware verification for that change passes,
- flash/RAM deltas are recorded,
- the change is archived.

Background analysis:

- [../docs/architecture/rearchitecture-plan.md](../docs/architecture/rearchitecture-plan.md)
- [../docs/architecture/rearchitecture-todo.md](../docs/architecture/rearchitecture-todo.md)
- [FEATURES.md](FEATURES.md) — feature origin, port status, known defects and owner changes

The ARCH-### identifiers below are the work items from the TODO document. This roadmap maps every
one of them to a change, so nothing lives only in chat or in a loose checklist. The Phase F track
comes first because several merged features are still unverified or partially ported; the
architecture work must not hide those defects.

---

## 1.1 Level order (revised 2026-11-04)

The user wants the multicore architecture first. Feature parity is therefore **Level 4**, not
Level 1. F2 was already completed early as prerequisite groundwork and is archived.

| Level | Scope | Changes | Status |
|---|---|---|---|
| **0** | Planning, specs, checklists | — | complete |
| **1** | Core runtime + multicore control/data split | A1, A3, B0, B1–B8 | complete |
| **2** | Reliability of the split | A2, A4, A5, A6 | complete |
| **3** | Command conversion | C1, C2 | complete |
| **4** | Feature parity / static audits | F1, F3–F10 (F2 archived) | complete |
| **5** | Hardening and cleanup | C3, C4, C5 | complete |

Level 1 bootstrap safety:

- A1 non-blocking terminal output must land before B4 (terminal ownership).
- A3 watchdog/fault record must land before any long migration.
- B0 static memory model must land before B1/B2/B3 allocate any pools or queues.
- A2 is no longer a separate gate; its cross-core I/O issues are absorbed into B4/B5.

The multicore process model is documented in
[../docs/architecture/multicore-process-model.md](../docs/architecture/multicore-process-model.md).

## 2. Target architecture

| Core | Role | Owns |
|---|---|---|
| **Core 0 — CTRL** | Control plane / UI | TinyUSB CDC0/CDC1/MSC, terminal/VT100/toolbars, command parser, mode manager, job dispatcher, config, FatFS + SD, ADC/AMUX, PSU, buttons, RGB, watchdog supervisor |
| **Core 1 — PROTO** | Data plane / protocols | protocol engines, PIO0/PIO1, protocol DMA/IRQs, captures, decoders, SUMP/BPIO/legacy engines, FALA, LCDTAP capture, RF/IR/RFID/RF24 |

Program invariants:

- No cross-core function calls; only typed queues and seqlock snapshots.
- Core 1 never calls `printf`, `tud_*` or `f_*`.
- Core 0 dispatches protocol jobs and remains responsive; it never blocks waiting for a job.
- Every PIO/SM/DMA/IRQ/pin/buffer is leased and released on error, job end or mode exit.
- Every wait has a timeout; every long job is cancellable.
- Command syntax, translation keys, FlatBuffers schema, PIO programs and wire timing are preserved.

---

## 3. Baseline & Progression

| Stage | Commit / Tag | Flash | RAM | Core 0 Stack | Core 1 Stack | Host Tests | Strict OpenSpec |
|---|---|---|---|---|---|---|---|
| Pre-program Baseline | baseline | 968,828 B / 16 MB (5.77%) | 307,664 B / 512 KB (58.68%) | 4 KB (SCRATCH_Y) | 4 KB (SCRATCH_X) | IR/SubGHz pass; SPSC broken | Baseline |
| A1 (`stabilize-terminal-output`) | `52feaab` | 971,836 B / 16 MB (5.79%) | 307,604 B / 512 KB (58.67%) | 4 KB (SCRATCH_Y) | 4 KB (SCRATCH_X) | 4/4 pass (terminal, spsc, ir, subghz) | 13/13 pass (archived) |
| A3 (`add-watchdog-and-crash-recovery`) | `0f91284` | 975,676 B / 16 MB (5.82%) | 307,832 B / 512 KB (58.71%) | 4 KB (SCRATCH_Y) | 4 KB (SCRATCH_X) | 5/5 pass (+crash_record ASan/UBSan) | 14/14 pass (archived) |
| B0 (`adopt-static-memory-model`) | `41355ce` | 976,932 B / 16 MB (5.82%) | 332,336 B / 512 KB (63.39%) | 4 KB (SCRATCH_Y) | 4 KB (SCRATCH_X) | 7/7 pass (+mem_pool, memory_model) | 15/15 pass (archived) |
| B1 (`add-service-kernel`) | `5979bb8` | 981,876 B / 16 MB (5.85%) | 333,760 B / 512 KB (63.66%) | 4 KB (SCRATCH_Y) | 4 KB (SCRATCH_X) | 8/8 pass (+kernel ASan/UBSan) | 16/16 pass (archived) |
| B2 (`add-protocol-job-runtime`) | `ec1d735` | 984,332 B / 16 MB (5.87%) | 335,048 B / 512 KB (63.91%) | 4 KB (SCRATCH_Y) | 4 KB (SCRATCH_X) | 9/9 pass (+job ASan/UBSan) | 17/17 pass (archived) |
| B3 (`add-resource-lease-manager`) | `1291d52` | 987,052 B / 16 MB (5.88%) | 349,612 B / 512 KB (66.68%) | 4 KB (SCRATCH_Y) | 4 KB (SCRATCH_X) | 10/10 pass (+resource soak) | 18/18 pass (archived) |
| B4 (`route-terminal-io-through-control-core`) | `e52ca49` | 987,052 B / 16 MB (5.88%) | 350,284 B / 512 KB (66.81%) | 4 KB (SCRATCH_Y) | 4 KB (SCRATCH_X) | 11/11 pass (+terminal ASan/UBSan) | 18/18 pass (archived) |
| B5 (`move-storage-to-control-core`) | `de18c71` | 990,764 B / 16 MB (5.91%) | 350,872 B / 512 KB (66.92%) | 4 KB (SCRATCH_Y) | 4 KB (SCRATCH_X) | 12/12 pass (+storage ASan/UBSan) | 19/19 pass (archived) |
| B6 (`move-binary-modes-to-transport-queues`) | `adee107` | 993,420 B / 16 MB (5.92%) | 352,148 B / 512 KB (67.16%) | 4 KB (SCRATCH_Y) | 4 KB (SCRATCH_X) | 13/13 pass (+bin_transport ASan/UBSan) | 20/20 pass (archived) |
| B7 (`split-control-and-protocol-cores`) | `43b3553` | 998,116 B / 16 MB (5.95%) | 356,420 B / 512 KB (67.98%) | 4 KB (SCRATCH_Y) | 4 KB (SCRATCH_X) | 15/15 pass (+mode, nmea ASan/UBSan) | 21/21 pass (archived) |
| B8 (`split-config-and-ui-snapshot`) | `899fa69` | 1,002,060 B / 16 MB (5.97%) | 327,804 B / 512 KB (62.52%) | 4 KB (SCRATCH_Y) | 4 KB (SCRATCH_X) | 16/16 pass (+config ASan/UBSan) | 21/21 pass (archived) |
| A5 (`fix-adc-and-psu-ownership`) | `2216b98` | 1,002,660 B / 16 MB (5.97%) | 327,812 B / 512 KB (62.52%) | 4 KB (SCRATCH_Y) | 4 KB (SCRATCH_X) | 17/17 pass (+power_monitor ASan/UBSan) | 22/22 pass (archived) |
| A6 (`harden-storage-and-msc-hotplug`) | `HEAD` | 996,964 B / 16 MB (5.94%) | 358,040 B / 512 KB (68.29%) | 4 KB (SCRATCH_Y) | 4 KB (SCRATCH_X) | 18/18 pass (+msc_storage ASan/UBSan) | 22/22 pass (archived) |
| C1 (`convert-rf-and-ir-commands-to-jobs`) | `HEAD` | 1,000,036 B / 16 MB (5.96%) | 358,092 B / 512 KB (68.30%) | 4 KB (SCRATCH_Y) | 4 KB (SCRATCH_X) | 19/19 pass (+rf_ir_jobs ASan/UBSan) | 22/22 pass (archived) |
| C2 (`convert-capture-and-file-commands-to-jobs`) | `HEAD` | 1,001,396 B / 16 MB (5.97%) | 358,092 B / 512 KB (68.30%) | 4 KB (SCRATCH_Y) | 4 KB (SCRATCH_X) | 20/20 pass (+capture_file_jobs ASan/UBSan) | 22/22 pass (archived) |
| F3 (`fix-infrared-port-parity`) | `6058dc5` | 1,004,500 B / 16 MB (5.99%) | 358,096 B / 512 KB (68.30%) | 4 KB (SCRATCH_Y) | 4 KB (SCRATCH_X) | 21/21 pass (+ir_protocols ASan/UBSan) | 21/21 pass (archived) |
| F4 (`fix-subghz-port-parity`) | `HEAD` | 1,004,476 B / 16 MB (5.99%) | 358,096 B / 512 KB (68.30%) | 4 KB (SCRATCH_Y) | 4 KB (SCRATCH_X) | 21/21 pass (+subghz ASan/UBSan) | 21/21 pass (archived) |

Record a new row after every archived change.

---

## 4. Change dependency graph

```
Phase F — Feature parity gate

F1 audit-ported-feature-parity
 └─ F2 fix-ported-feature-lifecycles
     ├─ F3 fix-infrared-port-parity
     ├─ F4 fix-subghz-port-parity
     ├─ F5 fix-rfid-port-parity
     ├─ F6 fix-rf24-port-parity
     ├─ F7 fix-lcdtap-port-parity
     ├─ F8 fix-cell-port-parity
     ├─ F9 fix-ps2-and-usb-sniffer-parity
     └─ F10 add-feature-conformance-tests

Phase A — Stabilize (Level 1 bootstrap)

A1 stabilize-terminal-output
 └─ A3 add-watchdog-and-crash-recovery
     └─ B0 adopt-static-memory-model
         └─ B1 add-service-kernel

A2 fix-cross-core-io-violations (absorbed into B4/B5, Level 2)
A4 make-mode-switching-transactional (Level 2)
A5 fix-adc-and-psu-ownership (Level 2)
A6 harden-storage-and-msc-hotplug (Level 2)

Phase B — Control/data split (Level 1)

B0 adopt-static-memory-model
 └─ B1 add-service-kernel
     ├─ B2 add-protocol-job-runtime
     │   ├─ B4 route-terminal-io-through-control-core
     │   ├─ B5 move-storage-to-control-core
     │   └─ B6 move-binary-modes-to-transport-queues
     └─ B3 add-resource-lease-manager
         └─ B7 split-control-and-protocol-cores
             └─ B8 split-config-and-ui-snapshot

Phase C — Convert and harden

B7
 ├─ C1 convert-rf-and-ir-commands-to-jobs
 ├─ C2 convert-capture-and-file-commands-to-jobs
 └─ C5 remove-dead-code-and-update-docs

A3
 ├─ C3 harden-syntax-and-command-parser
 └─ C4 add-hil-and-soak-test-suite
```

---

## 5. Phase 0 — Program bootstrap

These are setup tasks, not product-behavior changes. They are recorded here so the program does not
depend on undocumented local state.

| Step | Status | Evidence |
|---|---|---|
| Create branch `refactor/kernel` | done | `git branch --show-current` |
| Record baseline build size and RAM | done | this file, section 3 |
| Record baseline host-test result | done | IR/SubGHz pass; SPSC path broken |
| Fix `tests/test_spsc_queue.sh` hardcoded path | planned | tracked as A1 task 5.5 |
| Add health counters | planned | A1 |
| Add reproduction scripts | planned | A1 (paused host) and C4 (full suite) |

ARCH coverage: ARCH-001, ARCH-002, ARCH-003, ARCH-006, ARCH-007, ARCH-008.

---

## 6. Detailed change plan

### F1 — `audit-ported-feature-parity`

**Execution level:** 4  
**Status:** active  
**Hardware:** all modules available (see `HARDWARE.md`); every checklist is `READY`  
**Specs:** feature specs already exist under `openspec/specs/` for all ten feature groups  
**Capability:** none (`skip_specs: true`)  
**Depends on:** —  
**OpenSpec folder:** `openspec/changes/audit-ported-feature-parity/`  
**Evidence file:** `openspec/FEATURES.md`

**Intent:** Establish what actually works before changing architecture.

**In scope:**

- Pin upstream source commits.
- Inventory every feature and mode command.
- Create the per-feature acceptance checklist from `FEATURE_ACCEPTANCE_CHECKLIST.md`.
- Static lifecycle audit: PIO/SM/DMA/IRQ/clock/pins/UART/PSU/buffers.
- Run existing host tests and record coverage.
- Compare ports with upstream.
- Run available hardware checks.
- Record defects and create fix changes F2-F10.

**Out of scope:**

- Fixing code.
- Porting ESP32 Wi-Fi/BT/SDR/FM or other hardware-specific features.

**Verification:**

- `openspec/FEATURES.md` has no unowned feature row.
- Every feature has a status, a verification method and a linked checklist.
- Every known defect has severity, file/line and owner change.
- F2-F10 exist and validate.

**Archive condition:** the matrix is complete, hardware availability is recorded, and the first fix change is ready.

---

### F2 — `fix-ported-feature-lifecycles`

**Status:** active (static fixes applied; checklist evidence pending)  
**Capability:** `resource-lifecycle` (new)  
**Depends on:** F1  
**Planned folder:** `openspec/changes/fix-ported-feature-lifecycles/`

**Intent:** Remove the shared lifecycle defects that make ported features unreliable.

**Known defects to fix (from the initial audit):**

- `src/pirate/irio_pio.c` adds 5 PIO programs but removes only 4; `measure_mod_freq_program` is never removed.
- `src/pirate/irio_pio.c` deinit can run before init and does not disable/clear all SMs before removing programs.
- `src/mode/lcdtap_backend.cpp` SPI3 path adds `spi_3line_mode0_program` but never removes it on stop.
- `src/mode/usb.c` changes the system clock to 120 MHz and never restores it.
- `src/mode/usb.c` and `src/mode/ps2.c` call `pio_destroy()`, which unclaims every SM on PIO0/PIO1 and clears both instruction memories.
- `src/mode/usb.c` calls `psu_enable`/`psu_disable` directly instead of `psucmd_*`.
- Modes can configure pins marked `BP_PIN_DEBUG`; they must refuse or release the debug UART first.

**In scope:**

- PIO program/SM/DMA/IRQ add-remove symmetry for IR, LCDTAP, USB and PS2.
- Clock save/restore on mode entry/exit.
- Remove global PIO clears; release only owned resources.
- Debug/terminal UART pin conflict checks.
- PSU access through `psucmd_*`.
- Add a lifecycle unit test or static check for add/remove balance.

**Out of scope:**

- Full resource-lease manager (B3).
- Protocol parity fixes (F3-F9).

**Verification:**

- 100 enter/exit cycles for IR, LCDTAP, USB and PS2 with no PIO program leak.
- `clock_get_hz(clk_sys)` returns to the original value after USB mode exit.
- No `pio_destroy` / `pio_clear_instruction_memory` calls remain in mode code.
- Starting a mode on a debug UART pin is refused with a clear error.
- `res`/`health` counters show no leaked resources.

**Archive condition:** lifecycle checks pass and no S1 defect remains.

---

### F3 — `fix-infrared-port-parity`

**Status:** completed  
**Archived:** 2026-11-08  
**Capability:** `infrared`  
**Depends on:** F2  
**Archived folder:** `openspec/changes/archive/2026-11-08-fix-infrared-port-parity/`

**Intent:** Make the ESP32-Bit-Pirate INFRARED feature set actually work on RB-Pirate hardware.

**In scope:**

- Remove duplicate protocol names (`jvc`, `panasonic`, `rc5`, `rc6`, `sharp`, `denon`, `mitsubishi`) or make the intended entry explicit.
- Validate raw mark/space timings before PIO transmission; reject 0 or >16-bit values.
- Verify hand-written vs MakeHex protocol selection and repeats.
- Test NEC, RC5 and representative MakeHex protocols with a real receiver.
- Test `.ir` load/save/replay and TV-B-Gone.

**Verification:**

- Host test covers duplicate lookup and timing bounds.
- HIL send/receive test passes for NEC, RC5 and one MakeHex protocol.
- `.ir` round-trip and replay pass.
- No PIO leak after 100 send/receive cycles.

---

### F4 — `fix-subghz-port-parity`

**Status:** completed  
**Capability:** `subghz` (updated)  
**Depends on:** F2  
**Archived folder:** `openspec/changes/archive/2026-11-09-fix-subghz-port-parity/`

**Intent:** Verify the large Flipper/ESP32 SubGHz port against real CC1101 hardware.

**In scope:**

- CC1101 detection, preset setup, RX/TX and RSSI.
- Scan/sweep/waterfall.
- Receive/record/replay, `.sub` load/save.
- Protocol spot checks against pinned Flipper constants.
- Jam/bruteforce safety gates and cancellation.

**Verification:**

- HIL CC1101 detection and scan.
- Record/replay a known remote frame.
- `.sub` round-trip loads in the same firmware and matches the expected protocol.
- Spot-check at least 10 protocols against Flipper constants.
- 100 receive/cancel cycles with no PIO/IRQ/buffer leak.

---

### F5 — `fix-rfid-port-parity`

**Status:** completed  
**Capability:** `rfid`  
**Depends on:** F2  
**Archived folder:** `openspec/changes/archive/2026-11-09-fix-rfid-port-parity/`

**Intent:** Make PN532 HSU reliable and explicit about what is not supported.

**In scope:**

- UART instance selection vs debug/terminal UART.
- Pin mux and buffer direction.
- Inventory/read/dump/restore/write/clone paths.
- Missing tag operations: either implement or return a clear `unsupported` result.
- SMS-like long operations are not relevant; focus on tag timeouts and cancellation.

**Verification:**

- HIL PN532 inventory with a known tag.
- Read/dump a MIFARE Classic and an Ultralight/NTAG.
- One write/restore on a magic card.
- No UART conflict when terminal/debug UART is enabled.
- 100 inventory cycles with no leak.

---

### F6 — `fix-rf24-port-parity`

**Status:** complete  
**Capability:** `rf24` (new)  
**Depends on:** F2  
**Archived change:** `openspec/changes/archive/2026-11-09-fix-rf24-port-parity/`

**Intent:** Verify nRF24L01 packet and scan behavior with real modules, and document the WiFi-jam limitation honestly.

**In scope:**

- SPI1 ownership and CE pin.
- `jam -g wifi`: document that RP2350 has no 802.11 radio; this command is a carrier sweep over WiFi-overlapping channels, not beacon injection.
- Presence detection, channel set, scan/sweep/waterfall.
- Send/receive between two modules.
- Jam behavior and safety gating.
- Error handling when the module is absent.

**Verification:**

- Two-module send/receive passes.
- Scan/sweep shows a known channel.
- Absent-module path returns a clear error, no hang.
- 100 init/deinit cycles with no SPI/pin leak.

---

### F7 — `fix-lcdtap-port-parity`

**Status:** complete  
**Capability:** `lcdtap`  
**Depends on:** F2  
**Archive:** `openspec/changes/archive/2026-11-09-fix-lcdtap-port-parity/`

**Intent:** Make the advertised LCDTap controllers actually selectable and capture correctly.

**In scope:**

- SPI3 program removal on stop.
- KS0108 parity: either implement/select it or remove the README/help claim.
- Controller list and help consistency.
- SPI4/I2C capture and BMP export.
- DMA/IRQ teardown.

**Verification:**

- ST7789/ILI9341 SPI capture and SSD1306 I2C capture produce a correct BMP.
- ST7032 text capture works.
- KS0108 is either selectable or explicitly marked unavailable everywhere.
- 100 start/stop cycles with no PIO/DMA/IRQ leak.

---

### F8 — `fix-cell-port-parity`

**Status:** completed  
**Capability:** `cell`  
**Depends on:** F2  
**Planned folder:** `openspec/changes/fix-cell-port-parity/`

**Intent:** Verify the SIMCom AT command set on real hardware.

**In scope:**

- Modem detection, SIM status, network/operator.
- SMS send/list/read/delete.
- Calls and USSD.
- URC handling and timeouts.
- Buffer bounds and line parsing.

**Verification:**

- HIL modem detection and SIM status.
- SMS send/receive and USSD with a live SIM.
- Timeout path returns cleanly when the modem is absent.
- 100 AT commands with no buffer overrun or hang.

---

### F9 — `fix-ps2-and-usb-sniffer-parity`

**Status:** completed  
**Capability:** `ps2-sniffer`, `usb-sniffer`  
**Depends on:** F2  
**Archived change:** `openspec/changes/archive/2026-11-09-fix-ps2-and-usb-sniffer-parity/`

**Intent:** Make the upstream sniffer ports safe to enter and exit.

**In scope:**

- Remove global PIO clears; release only owned SMs/programs.
- Restore the system clock after USB mode.
- Implement or remove the USB "capture stop" command.
- PS/2 IRQ teardown and SM release.
- USB keylog and buffer display.
- PSU through `psucmd_*`.

**Verification:**

- PS/2 keyboard and mouse capture pass.
- USB low/full-speed capture and keylog pass.
- Exit USB mode, verify clock restored, enter I2C/SPI and verify timing.
- 100 enter/exit cycles with no PIO/IRQ leak.

---

### F10 — `add-feature-conformance-tests`

**Status:** completed  
**Capability:** none (`skip_specs: true`)  
**Depends on:** F3-F9  
**Archived change:** `openspec/changes/archive/2026-11-09-add-feature-conformance-tests/`

**Intent:** Prevent ported features from silently regressing again.

**In scope:**

- Host tests for IR, SubGHz, RFID, RF24, LCDTAP, CELL, PS2, USB.
- HIL scripts for every hardware feature.
- A `features` command or CI report showing status per feature.
- A CI gate that fails when a feature has no test evidence.

**Verification:**

- `tests/run_all.sh` covers every feature with a host-testable core.
- HIL suite passes on hardware.
- CI reports feature coverage and fails on missing evidence.

---

### A1 — `stabilize-terminal-output`

**Execution level:** 1 (bootstrap safety)  
**Status:** active (implementation in progress)  
**Capability:** `terminal-io` (new)  
**Depends on:** F2  
**ARCH coverage:** ARCH-002, ARCH-004, ARCH-005, ARCH-006, ARCH-008, ARCH-010, ARCH-011, ARCH-012, ARCH-013, ARCH-014, ARCH-015, ARCH-016, ARCH-017  
**OpenSpec folder:** `openspec/changes/stabilize-terminal-output/`

**Intent:** No command can block forever on terminal output.

**In scope:**

- Non-blocking terminal producer.
- Bounded `wait_drain(timeout)`.
- Disconnect-safe Core 1 drain.
- Dropped-byte/overflow/high-water counters.
- One truncation marker per overflow episode.
- `health` output.
- `txtest` bounded waits and truncation reporting.
- Host test and paused-host hardware test.
- Fix the SPSC test runner path.

**Out of scope:**

- Atomic whole-VT100-sequence writes.
- Moving terminal ownership to Core 0 (B4).
- Binary-mode transport (B6).

**Steps:**

1. Add `bp_terminal_health_t` and counters.
2. Change `tx_fifo_put()` to return a status and use `spsc_queue_try_add()`.
3. Change `tx_fifo_write()` to non-blocking chunking.
4. Add `tx_fifo_try_put()` compatibility alias.
5. Change `_putchar()` to the non-blocking path.
6. Implement `tx_fifo_wait_drain(timeout)`.
7. Reorder `tx_fifo_service()` so disconnect flush happens before the write-available gate.
8. Implement the pending truncation marker and low-water emission.
9. Add `health` output.
10. Update `txtest`.
11. Add host test and paused-host script.
12. Fix the SPSC test runner path.
13. Build, record size, update docs.

**Verification:**

- `tests/test_terminal_tx.sh` passes.
- Paused host during `txtest -n 1048576`: prompt returns within 5 s, output is marked truncated.
- USB yank during output: device reconnects and accepts a command.
- Normal `txtest -n 4096` CRC still matches.
- `health` shows all counters.
- `cmake --build build_rp2350 --parallel --target bus_pirate6` succeeds.

**Archive condition:** all tasks complete, host test and hardware checks pass, size recorded.

---

### A2 — `fix-cross-core-io-violations`

**Status:** planned  
**Capabilities:** `cross-core-io` (new), `storage` (new or delta)  
**Depends on:** A1  
**ARCH coverage:** ARCH-020, ARCH-021, ARCH-022, ARCH-023, ARCH-024, ARCH-025, ARCH-030, ARCH-031, ARCH-032, ARCH-033, ARCH-034  
**Planned OpenSpec folder:** `openspec/changes/fix-cross-core-io-violations/`

**Intent:** Core 1 must never call Core 0-only services or panic on recoverable I/O.

**In scope:**

- Remove `printf` from `storage_detect()`, `storage_mount()`, `storage_unmount()` when called on Core 1.
- Move card detect/mount/unmount to a Core 0 storage service.
- Replace MSC read/write error `printf` with a log event.
- Add `bp_log_post()` for Core 1.
- Add timeouts to `icm_core0_send_message_synchronous()`.
- Make callers handle timeout without freezing.
- Add a CI guard against `printf(` in Core 1 files.

**Out of scope:**

- Full storage/MSC redesign (A6).
- Full control/data split (B7).
- Binary transport (B6).

**Steps:**

1. Inventory every Core 1 entry point that calls `printf`.
2. Add fixed-size `bp_log_post()` and a Core 0 log consumer.
3. Remove prints from storage paths; post events instead.
4. Move `storage_detect()` to the Core 0 tick/service.
5. Remove prints from MSC error paths.
6. Add `icm_core0_send_message_synchronous_timeout()`.
7. Update syntax/toolbar/RGB callers to handle timeout.
8. Add `icm_timeouts` health counter.
9. Add the grep-based CI guard.
10. Add a card-insert/remove hardware test and a Core 1 stall test.

**Verification:**

- 100 SD insert/remove cycles with no panic or hang.
- Force an MSC read error; no Core 1 `printf`, no panic.
- Stall Core 1 during a syntax command; Core 0 returns an error within the timeout.
- `grep` guard finds no `printf(` in Core 1-owned files.

**Archive condition:** no Core 1 `printf`, no unbounded ICM wait, hotplug test passes.

---

### A3 — `add-watchdog-and-crash-recovery`

**Status:** complete (archived 2026-11-06)  
**Capability:** `fault-recovery` (archived to `openspec/specs/fault-recovery/spec.md`)  
**Depends on:** A1 *(A2 cross-core I/O violations are absorbed/deferred to Level 2 and B4/B5; Core 1 printf hazard in storage_detect/MSC is removed in A3 as a crash-safety prerequisite)*  
**ARCH coverage:** ARCH-060, ARCH-061, ARCH-062, ARCH-063, ARCH-064, ARCH-065, ARCH-066, ARCH-067, ARCH-070, ARCH-071, ARCH-072, ARCH-073, ARCH-074  
**Planned folder:** `openspec/changes/add-watchdog-and-crash-recovery/`

**Intent:** A stuck core or assertion must produce an RP2350 crash record and a controlled recovery, not a dead device.

**In scope:**

- Target exclusively RP2350B (Cortex-M33). RP2040 is completely out of scope.
- 2–5 s hardware watchdog configured with `pause_on_debug = true` (`watchdog_enable(timeout_ms, 1)`) so SWD/GDB debuggers do not reset during breakpoints.
- Build flag / debug bypass (`BP_WATCHDOG_DISABLE` / `#ifndef NDEBUG`) for developer environments.
- Remove Core 1 `printf` hazards from `storage_detect()` and `msc_disk.c` (post to event log instead) so routine SD card operations do not trigger `BP_ASSERT_CORE0()` -> panic/watchdog resets.
- Per-core heartbeat counters.
- RP2350 Cortex-M33 HardFault register capture: CFSR, HFSR, BFAR, MMFAR, PC, LR, SP.
- Panic/assert record with file, line and reason in `.uninitialized_data` / `.noinit` RAM section with magic word + CRC to survive `crt0.S` boot clearing.
- Boot report and safe mode after repeated faults.
- 8 KB stacks per core (16 KB total) and high-water measurement.
- `fault` and `health` commands.

**Out of scope:**

- Restarting only Core 1 (later enhancement after B7).
- Persistent flash crash log beyond the first record.

**Steps:**

1. Remove Core 1 `printf` from `storage_detect()` and `msc_disk.c` to prevent spurious asserts.
2. Add `bp_fault_record_t` in `.uninitialized_data` RAM section with magic word and CRC.
3. Capture CFSR/HFSR/BFAR/MMFAR/PC/LR/SP on HardFault.
4. Replace the infinite blink with blink → controlled reset.
5. Capture assert/panic metadata.
6. Add boot-time crash report.
7. Add safe mode after N repeated faults.
8. Enable the watchdog with `pause_on_debug = true` and feed only when both heartbeats advance.
9. Increase both stacks to 8 KB (`PICO_STACK_SIZE=8192`, `PICO_CORE1_STACK_SIZE=8192`).
10. Add stack painting and high-water reporting.
11. Add `fault` and extend `health`.
12. Add a fault-injection test command guarded by a build flag.
13. Verify watchdog reset and crash record on hardware.

**Verification:**

- Inject a Core 0 hang; device resets within 2 s and prints the record on boot.
- Inject a Core 1 hang; same.
- Trigger a controlled assert; record contains file/line/reason.
- Stack high-water shows >25% free at peak load.
- Build succeeds with 8 KB stacks and records the new RAM usage.

**Archive condition:** watchdog and crash record verified on hardware; stack margin recorded.

---

### A4 — `make-mode-switching-transactional`

**Status:** planned  
**Capability:** `protocol-modes` (new)  
**Depends on:** A2  
**ARCH coverage:** ARCH-050, ARCH-051, ARCH-052, ARCH-053  
**Planned folder:** `openspec/changes/make-mode-switching-transactional/`

**Intent:** Switching modes must stop and release the old mode before the new mode touches hardware, and roll back on failure.

**In scope:**

- Reorder `ui_mode_enable_args()`: stop/cleanup old mode first.
- Add rollback if `protocol_setup_exc()` fails.
- Add a `bp_mode_switch()` wrapper.
- Ensure failed modes return to HiZ and release partial resources.
- Add a mode-switch soak test.

**Out of scope:**

- Full mode lifecycle rewrite (B7).
- Resource leases (B3).

**Steps:**

1. Document the current order and failure paths.
2. Implement stop → safe state → configure → start.
3. Add rollback and error reporting.
4. Wrap existing mode tables in `bp_mode_switch()`.
5. Add a mode-switch soak script across every enabled mode.
6. Verify no stale PIO/IRQ/DMA state after a failed setup.

**Verification:**

- 1000 mode switches across all enabled modes with no crash.
- Force a setup failure (invalid config); device returns to HiZ and the next mode starts cleanly.
- No leaked IRQ/PIO state after failure.

**Archive condition:** soak and forced-failure tests pass.

---

### A5 — `fix-adc-and-psu-ownership`

**Status:** planned  
**Capability:** `power-monitor` (new)  
**Depends on:** A2  
**ARCH coverage:** ARCH-040, ARCH-041, ARCH-042, ARCH-043  
**Planned folder:** `openspec/changes/fix-adc-and-psu-ownership/`

**Intent:** ADC/AMUX and PSU safety must have one owner and no cross-core race.

**In scope:**

- Fix `adc_busy_wait()` volatile/lock release.
- Move ADC/AMUX sampling to Core 0 (control plane).
- Publish a monitor snapshot for display and protocol `v` requests.
- Keep PSU overcurrent/fuse response on Core 0.
- Add a request/response path for protocol ADC reads.

**Out of scope:**

- Full control/data split (B7).
- Config split (B8).

**Steps:**

1. Rewrite `adc_busy_wait()` with correct locking.
2. Move `amux_sweep()` into the Core 0 monitor service.
3. Add a seqlock `bp_monitor_snapshot_t`.
4. Update `system_monitor` and toolbars to the snapshot.
5. Add `ADC_READ` request handling for protocol code.
6. Verify PSU overcurrent trip during a protocol job.
7. Add a monitor stress test.

**Verification:**

- No ADC lock timeout/race under 24 h monitor load.
- PSU cuts power on injected overcurrent while Core 1 is busy.
- `v` command returns a valid reading.
- Toolbar voltage/current values update consistently.

**Archive condition:** stress and PSU fault tests pass.

---

### A6 — `harden-storage-and-msc-hotplug`

**Status:** planned  
**Capability:** `storage` (delta)  
**Depends on:** A2  
**ARCH coverage:** ARCH-080, ARCH-081, ARCH-082, ARCH-083, ARCH-400, ARCH-401, ARCH-402, ARCH-403, ARCH-406, ARCH-407  
**Planned folder:** `openspec/changes/harden-storage-and-msc-hotplug/`

**Intent:** Card removal, MSC I/O and capacity changes must never read out of bounds, corrupt the filesystem or hang.

**In scope:**

- Bounds-check the MSC demo-disk fallback.
- Eject/sense on card removal during MSC transfer.
- Block-device mutex for raw `disk_read/write/ioctl`.
- Read-only MSC policy while the terminal is connected.
- Mount state machine with debounce and retry.
- Storage error log.

**Out of scope:**

- Moving FatFS to the control core (B5; in the current code it is already Core 0 for file operations, but MSC is Core 1).
- Config migration (B8).

**Steps:**

1. Add LBA bounds checks to every MSC callback.
2. Add medium eject and SCSI sense on removal.
3. Add `blockdev_lock`.
4. Enforce read-only/writable policy.
5. Add the mount state machine and debounce.
6. Add error counters and `storage status`.
7. Run 100 hotplug cycles with FAT integrity verification.

**Verification:**

- 100 insert/remove cycles with no panic, hang or corruption.
- Host cannot read/write beyond the demo disk after removal.
- Filesystem remains mountable after every cycle.
- Error log records a forced I/O failure.

**Archive condition:** hotplug stress and integrity checks pass.

---

### B0 — `adopt-static-memory-model`

**Execution level:** 1  
**Status:** complete (archived 2026-11-06)  
**Capability:** `memory-model` (archived to `openspec/specs/memory-model/spec.md`)

**Intent:** Remove heap use from the core runtime and stop adding new heap users, tailored to the RP2350's 520 KB SRAM.

**In scope:**

- Target exclusively RP2350B (520 KB SRAM). RP2040 is completely out of scope.
- Establish static memory budget targeting <= 380 KB total static RAM (leaving >140 KB headroom for stacks, burst buffers, and DMA).
- Enforce “no malloc/calloc/realloc/free/new/delete” in new code.
- Set FatFS `FF_USE_LFN` to `2` (static LFN buffer).
- Add static pool/arena helpers for services, jobs and protocol objects.
- Replace FlatCC per-packet malloc with a static builder/output buffer.
- Replace SubGHz decoder/encoder malloc with a fixed protocol pool.
- Replace LCDTAP `new LcdTap`/framebuffer malloc with placement new + static arena.
- Move kilo/hx editor allocations to a bounded arena or static editor pool.
- Add a CI grep/allowlist guard for heap calls.
- Add a `memory` debug command showing pool/arena usage.

**Out of scope:**

- Rewriting third-party libraries that are not compiled or not on a runtime path.
- Removing the 128 KB big buffer; B3 replaces it with named leases/pools.

**Verification:**

- No heap calls in `src/kernel/`, `src/services/`, or new mode code.
- FatFS file operations run with `FF_USE_LFN=2` and no `ff_memalloc`.
- SubGHz decode/encode runs 1000 cycles without heap calls.
- BPIO binary traffic runs with a static builder buffer.
- LCDTAP start/stop runs 100 times without heap calls.
- `memory` shows stable pool/arena usage after a 1-hour stress run.
- Build, host tests, `openspec validate --all --strict`.

**Archive condition:** heap call sites are either removed or explicitly allowlisted with a measured
budget and owner.

---

### B1 — `add-service-kernel`

**Execution level:** 1  
**Status:** completed (archived `2026-11-07-add-service-kernel`)  
**Capability:** `service-kernel` (new)  
**Depends on:** A1, A3, B0  
**ARCH coverage:** ARCH-100, ARCH-101, ARCH-102, ARCH-103, ARCH-104, ARCH-105, ARCH-106, ARCH-107, ARCH-108, ARCH-109, ARCH-110, ARCH-111, ARCH-112, ARCH-113  
**Planned folder:** `openspec/changes/add-service-kernel/`

**Intent:** Replace ad-hoc loops with a small deterministic event/timer/work service that both cores can use.

**In scope:**

- Fixed-size event queue.
- 1 ms soft timers.
- Work queue.
- `bp_service()` / `bp_kernel_run()`.
- Scheduler sleep & wakeup: `__wfe()` on idle, inter-core wakeup via `__sev()` or SIO FIFO IRQ when enqueuing events across cores.
- Core cutover staging: keep `tud_task()` on Core 1 during B1–B3 as interim host; cutover to Core 0 (CTRL) is scheduled cleanly in B4/B7.
- Health counters.
- Core 0 main loop conversion.
- Core 1 loop services and heartbeat.
- Cancellation plumbing.

**Out of scope:**

- Preemptive RTOS.
- Job runtime (B2).
- Resource leases (B3).

**Steps:**

1. Add `src/kernel/bp_event.*`.
2. Add `src/kernel/bp_timer.*`.
3. Add `src/kernel/bp_work.*`.
4. Add `src/kernel/bp_kernel.*`.
5. Add `bp_status.h`, `bp_log`, `bp_health`.
6. Convert Core 0 main loop.
7. Convert Core 1 loop with sleep/wakeup.
8. Add cancellation flags for Ctrl+C/button/USB disconnect.
9. Add host tests for queue/timers/cancel.
10. Add `kernel stats`.

**Verification:**

- Host tests for event queue, timers and cancellation pass.
- A test long-op cancels within 100 ms.
- Main loops contain no unbounded waits.
- No regression in terminal or mode startup.

**Archive condition:** kernel running on both cores with tests and no regressions.

---

### B2 — `add-protocol-job-runtime`

**Status:** completed (archived `2026-11-07-add-protocol-job-runtime`)  
**Capability:** `protocol-jobs` (new)  
**Depends on:** B1  
**ARCH coverage:** ARCH-107, ARCH-110, ARCH-111, ARCH-311, ARCH-312, ARCH-314, ARCH-316  
**Planned folder:** `openspec/changes/add-protocol-job-runtime/`

**Intent:** Core 0 can submit a protocol command and remain responsive; Core 1 executes and reports progress.

**In scope:**

- `bp_job_t` submit/cancel/status/done.
- Progress and output events.
- Deadline and cancel checks.
- Job queue on Core 1.
- Runtime for one active protocol job at a time.
- Ctrl+C/button cancellation.

**Out of scope:**

- Converting all commands (C1/C2).
- Moving protocol engines (B7).

**Steps:**

1. Define `bp_job_t` and result/status codes.
2. Add submit/cancel/status messages.
3. Add Core 1 job executor.
4. Add progress/output event streaming.
5. Add deadline and watchdog supervision.
6. Add cancellation at safe points.
7. Add a test job.
8. Add host tests for submit/cancel/timeout.

**Verification:**

- Submit a 30 s test job; prompt remains responsive.
- Cancel; job stops within 100 ms.
- Timeout; Core 0 reports and recovers.
- Job output appears in order.

**Archive condition:** test job and cancellation verified on hardware.

---

### B3 — `add-resource-lease-manager`

**Status:** completed (2026-11-07)  
**Capability:** `resource-ownership` (new)  
**Depends on:** B1  
**ARCH coverage:** ARCH-200, ARCH-201, ARCH-202, ARCH-203, ARCH-204, ARCH-205, ARCH-210, ARCH-211, ARCH-212, ARCH-213, ARCH-214, ARCH-215, ARCH-216, ARCH-240, ARCH-241, ARCH-242, ARCH-243  
**Archived folder:** `openspec/changes/archive/2026-11-07-add-resource-lease-manager/`

**Intent:** No driver can steal or leak another driver's PIO, SM, DMA, IRQ, pin or buffer.

**In scope:**

- Lease API for PIO blocks/SMs, DMA, IRQ, UART/SPI/I2C, GPIO functions, big buffer.
- Release-all on mode exit/error.
- Resource table and `res` command.
- Delete global `pio_destroy()` from USB/PS2.
- Big-buffer arena with named leases.
- IRQ/DMA inventory.

**Out of scope:**

- Mode lifecycle rewrite (B7).
- Full mode migration (B7).

**Steps:**

1. Define resource classes and leases.
2. Implement acquire/release/release_all.
3. Add conflict reporting.
4. Replace `pio_claim_unused_sm(..., true)` in mode code.
5. Delete USB/PS2 global PIO clears.
6. Convert the big buffer to leases.
7. Add `res` command.
8. Add a mode-switch leak test.

**Verification:**

- 1000 mode switches leave the resource table empty.
- No `hard_assert` on resource exhaustion.
- Logic analyzer alloc failure returns an error, not a NULL deref.
- `res` shows owners and free resources.

**Archive condition:** leak test passes and no global PIO clears remain.

---

### B4 — `route-terminal-io-through-control-core`

**Status:** completed (2026-11-07)  
**Capability:** `terminal-io` (delta)  
**Depends on:** B1, B2  
**ARCH coverage:** ARCH-120, ARCH-121, ARCH-128, ARCH-129  
**Archived folder:** `openspec/changes/archive/2026-11-07-route-terminal-io-through-control-core/`

**Intent:** Core 0 owns the terminal; Core 1 output and input cross through explicit rings.

**In scope:**

- `bp_terminal` service on Core 0.
- Core 1 text output ring.
- Core 1 job input ring.
- Ctrl+C routing on Core 0.
- Remove direct terminal access from protocol code.

**Out of scope:**

- Binary CDC1 transport (B6).
- Full core split (B7).

**Steps:**

1. Add `src/services/bp_terminal.*`.
2. Route Core 0 `printf` through it.
3. Route Core 1 `printf` to the text ring.
4. Add input routing while a job is active.
5. Add Ctrl+C handling that does not consume job input.
6. Add tests for interleaving and cancel.

**Verification:**

- Core 1 prints during a job; text appears in order.
- Ctrl+C cancels the job and returns the prompt.
- Input is delivered to the job only when expected.
- Terminal remains responsive during a 30 s job.

**Archive condition:** no direct terminal access from Core 1.

---

### B5 — `move-storage-to-control-core`

**Status:** completed (2026-11-07)  
**Capability:** `storage` (archived to `openspec/specs/storage/spec.md`)  
**Depends on:** B1, B2  
**ARCH coverage:** ARCH-130, ARCH-131, ARCH-132, ARCH-133, ARCH-134, ARCH-135, ARCH-136  
**Planned folder:** `openspec/changes/archive/2026-11-07-move-storage-to-control-core/`

**Intent:** FatFS, SD, MSC and hotplug all live on Core 0; Core 1 requests file I/O by message.

**In scope:**

- Core 0 storage service.
- Storage RPC for protocol jobs.
- Streaming buffer handoff for captures.
- Config load/save through the service.
- Atomic config save and migration.

**Out of scope:**

- MSC raw block layer (A6).
- Binary capture transport (B6).

**Steps:**

1. Add `src/services/bp_storage.*`.
2. Move all `f_*` calls behind it.
3. Add request/response RPC.
4. Add streaming write handoff.
5. Move config load/save.
6. Add atomic save/migration.
7. Add host tests for request timeout and failure.

**Verification:**

- No `f_*` outside the storage service.
- Protocol job can load/save a file.
- Storage failure returns an error, no hang.
- Config corrupt file falls back to defaults.

**Archive condition:** grep guard passes and file I/O tests pass.

---

### B6 — `move-binary-modes-to-transport-queues`

**Status:** completed  
**Capability:** `binary-modes`  
**Depends on:** B1, B2, B4  
**ARCH coverage:** ARCH-122, ARCH-123, ARCH-124, ARCH-125, ARCH-126, ARCH-127, ARCH-128, ARCH-129  
**Archived folder:** `openspec/changes/archive/2026-11-07-move-binary-modes-to-transport-queues/`

**Intent:** Core 0 owns CDC1/TinyUSB; SUMP/BPIO/legacy engines run on Core 1 and exchange framed packets.

**In scope:**

- `bp_bin_transport` on Core 0.
- Packet rings for SUMP/BPIO/legacy/IRToy.
- Backpressure/drop policy.
- Remove direct `tud_cdc_n_*` from binmode code.
- Throughput benchmark.

**Out of scope:**

- Protocol engine rewrites.
- Mode job runtime (B2 already provides it).

**Steps:**

1. Add `bp_bin_transport`.
2. Move CDC1 reads/writes to Core 0.
3. Add packet rings.
4. Convert SUMP.
5. Convert BPIO.
6. Convert FALAIO.
7. Convert legacy/IRToy/CH32V003.
8. Add backpressure counters.
9. Benchmark before/after.

**Verification:**

- No `tud_*` in `src/binmode/`.
- SUMP and BPIO stress pass while terminal is active.
- CDC1 not open: producers do not block.
- Throughput is within the USB-FS budget.

**Archive condition:** binary stress and throughput results recorded.

---

### B7 — `split-control-and-protocol-cores`

**Status:** completed  
**Archived:** 2026-11-07-split-control-and-protocol-cores  
**Capability:** `core-separation` (new)  
**Depends on:** B1 … B6  
**ARCH coverage:** ARCH-220, ARCH-221, ARCH-222, ARCH-223, ARCH-224, ARCH-225, ARCH-226, ARCH-227, ARCH-228, ARCH-229, ARCH-230, ARCH-231, ARCH-244  
**Planned folder:** `openspec/changes/split-control-and-protocol-cores/`

**Intent:** Activate the final ownership split: Core 0 control plane, Core 1 data plane.

**In scope:**

- Mode lifecycle adapter (`bp_mode_t`).
- Move all protocol modes to Core 1 jobs.
- Mode start/stop RPC.
- Protocol heartbeat.
- Core 1 crash detection and recovery.
- Final pin/resource leases.
- Update docs to the new ownership model.

**Out of scope:**

- Long-command conversion (C1/C2).
- Config split (B8).

**Steps:**

1. Add `bp_mode_t` and compatibility adapter.
2. Migrate USB/PS2/LCDTAP/SUBGHZ/RF24/RFID/IR.
3. Migrate core modes (HiZ, 1-Wire, UART, HDUART, I2C, SPI, 2-Wire, 3-Wire, DIO, LED).
4. Add mode start/stop RPC.
5. Add protocol heartbeat and recovery.
6. Verify all modes through jobs.
7. Run 1000 mode switches.
8. Update architecture docs.

**Verification:**

- Every enabled mode starts/stops from Core 0 via RPC.
- Core 1 stall is detected and recovered.
- 1000 mode switches with zero leaked resources.
- UI remains responsive during every mode's long job.

**Archive condition:** full mode matrix and recovery tests pass.

---

### B8 — `split-config-and-ui-snapshot`

**Status:** completed  
**Archived:** 2026-11-07-split-config-and-ui-snapshot  
**Capability:** `configuration` (new)  
**Depends on:** B7  
**ARCH coverage:** ARCH-140, ARCH-141, ARCH-142, ARCH-143, ARCH-144, ARCH-145, ARCH-146, ARCH-150, ARCH-151, ARCH-152, ARCH-153, ARCH-154, ARCH-155, ARCH-404, ARCH-405  
**Planned folder:** `openspec/changes/split-config-and-ui-snapshot/`

**Intent:** Core 1 reads only immutable mode config; Core 0 owns persisted/runtime/UI state.

**In scope:**

- `bp_config_t`, `bp_runtime_t`, `bp_ui_snapshot_t`.
- Versioning and migration.
- Validation and corrupt-file fallback.
- UI snapshot seqlock.
- Toolbar frame queue.
- Core 1 config access removal.

**Out of scope:**

- Storage service (B5).
- Mode lifecycle (B7).

**Steps:**

1. Define the three config types.
2. Add version/migration.
3. Add validation/fallback.
4. Implement the UI snapshot.
5. Move toolbars to frame queue.
6. Remove Core 1 `system_config` access.
7. Add migration and snapshot tests.

**Verification:**

- No Core 1 access to `system_config`.
- Config migration from an old file works.
- Corrupt config boots to defaults.
- Toolbars update during a long job.
- Snapshot is consistent under seqlock stress.

**Archive condition:** config and snapshot tests pass.

---

### C1 — `convert-rf-and-ir-commands-to-jobs`

**Execution level:** 3  
**Status:** complete (archived 2026-11-08)  
**Capability:** `protocol-jobs` (delta)  
**Depends on:** B7  
**ARCH coverage:** ARCH-305, ARCH-306, ARCH-307, ARCH-308, ARCH-309, ARCH-313  
**Archived folder:** `openspec/changes/archive/2026-11-08-convert-rf-and-ir-commands-to-jobs/`

**Intent:** RF/IR long operations are cancellable jobs and never own Core 0.

**In scope:**

- SUBGHZ scan/sweep/receive/record/waterfall/jam/bruteforce.
- RFID inventory/read/write.
- RF24 scan/sweep/waterfall/receive/jam.
- IR record/send/replay/jam.
- PS2/USB capture loops.
- Progress and cancel.

**Verification:**

- Each command returns control within 100 ms of Ctrl+C.
- Terminal stays responsive during each command.
- No leaked resources after cancel.

---

### C2 — `convert-capture-and-file-commands-to-jobs`

**Execution level:** 3  
**Status:** complete (archived 2026-11-08)  
**Capability:** `protocol-jobs` (delta)  
**Depends on:** B7  
**ARCH coverage:** ARCH-302, ARCH-303, ARCH-304, ARCH-310  
**Archived folder:** `openspec/changes/archive/2026-11-08-convert-capture-and-file-commands-to-jobs/`

**Intent:** Capture, dump and editor operations are cancellable jobs with progress.

**In scope:**

- `txtest`, dump/flash read/write, logic dump/export, LCDTAP dump, editor save/load, EEPROM GUI.

**Verification:**

- Cancel within 100 ms.
- Terminal responsive.
- Partial output/file handled safely.

---

### C3 — `harden-syntax-and-command-parser`

**Status:** completed  
**Capability:** `command-parser` (new)  
**Depends on:** A3  
**ARCH coverage:** ARCH-300, ARCH-301, ARCH-315, ARCH-700, ARCH-701, ARCH-702, ARCH-703, ARCH-704, ARCH-705, ARCH-706, ARCH-707, ARCH-708, ARCH-709  
**Planned folder:** `openspec/changes/harden-syntax-and-command-parser/`

**Intent:** Parsing cannot overflow buffers or crash on malformed input.

**In scope:**

- Syntax compiler bounds checks.
- Reject `:0` repeats.
- Fuzz tests.
- Command table unification.
- Correctness backlog items 700–709.
- `-Wall -Wextra` gates.

**Verification:**

- Fuzz corpus finds no crash.
- Boundary tests for every buffer.
- Build with warnings enabled passes.

---

### C4 — `add-hil-and-soak-test-suite`

**Status:** completed  
**Capability:** none (`skip_specs: true`)  
**Depends on:** A1 … A6  
**ARCH coverage:** ARCH-007, ARCH-500, ARCH-501, ARCH-502, ARCH-503, ARCH-504, ARCH-505, ARCH-506, ARCH-507, ARCH-508, ARCH-509, ARCH-510, ARCH-511, ARCH-512, ARCH-513  
**Planned folder:** `openspec/changes/add-hil-and-soak-test-suite/`

**Intent:** Automate the stability tests so regressions are caught.

**In scope:**

- Portable test runners.
- Host tests for terminal/event/resource/config/parser.
- HIL flood, yank, hotplug, mode loop, binary stress.
- Fault injection.
- 24 h soak.
- CI gates and size budgets.

**Verification:**

- `tests/run_all.sh` passes from any directory.
- HIL suite passes on hardware.
- 24 h soak completes with zero resets/deadlocks.
- CI runs build + host tests + static analysis.

---

### C5 — `remove-dead-code-and-update-docs`

**Status:** completed  
**Capability:** none (`skip_specs: true`)  
**Depends on:** B7  
**ARCH coverage:** ARCH-600, ARCH-601, ARCH-602, ARCH-603, ARCH-604, ARCH-605, ARCH-606, ARCH-607, ARCH-608  
**Planned folder:** `openspec/changes/remove-dead-code-and-update-docs/`

**Intent:** Remove obsolete shims and leave accurate architecture/developer docs.

**In scope:**

- Delete dead code and compatibility shims.
- Update dual-core, USB, kernel, mode, storage guides.
- Update `CLAUDE.md`/`AGENTS.md` ownership rules.
- Document upstream divergence.
- Final 24 h soak.

**Verification:**

- Build and tests pass after cleanup.
- Docs match the code.
- Final soak passes.

---

## 7. Complete ARCH coverage map

| ARCH range | Change |
|---|---|
| ARCH-001 … ARCH-008 | Phase 0 bootstrap + A1 |
| ARCH-010 … ARCH-017 | A1 |
| ARCH-020 … ARCH-034 | A2 |
| ARCH-040 … ARCH-043 | A5 |
| ARCH-050 … ARCH-053 | A4 |
| ARCH-060 … ARCH-067 | A3 |
| ARCH-070 … ARCH-074 | A3 |
| ARCH-080 … ARCH-083 | A6 |
| ARCH-100 … ARCH-113 | B1 |
| ARCH-120 … ARCH-129 | B4 (terminal) / B6 (binary) |
| ARCH-130 … ARCH-136 | B5 |
| ARCH-140 … ARCH-146 | B8 |
| ARCH-150 … ARCH-155 | B8 |
| ARCH-200 … ARCH-216 | B3 |
| ARCH-220 … ARCH-231 | B7 |
| ARCH-240 … ARCH-244 | B3 + B7 |
| ARCH-300 … ARCH-301 | C3 |
| ARCH-302 … ARCH-310 | C1 + C2 |
| ARCH-311 … ARCH-316 | B2 + C1/C2 |
| ARCH-400 … ARCH-407 | A6 + B5 |
| ARCH-500 … ARCH-513 | C4 |
| ARCH-600 … ARCH-608 | C5 |
| ARCH-700 … ARCH-709 | C3 |

---

## 8. Program definition of done

1. `openspec/FEATURES.md` marks every merged feature `verified` or explicitly `not ported` with a reason.
2. `openspec/specs/` describes terminal, storage, fault recovery, protocol jobs, resource ownership, binary transport and configuration.
3. All changes F1–F10 and A1–C5 are archived.
4. No `printf` on Core 1, no `tud_*` outside Core 0 USB, no `f_*` outside Core 0 storage.
5. 1000 mode switches and 100 SD hotplug cycles pass.
6. 24 h soak passes with zero resets and zero unhandled timeouts.
7. A crash always produces a readable record and a controlled recovery.
8. New modes can be added without touching Core 1, TinyUSB, FatFS or kernel internals.
9. Flash/RAM/stack budgets are documented and enforced in CI.

---

## 9. Status board

| Change | Status | Archived date | Notes |
|---|---|---|---|
| F1 `audit-ported-feature-parity` | completed | 2026-11-08 | Level 4; skip_specs; evidence in FEATURES.md |
| F2 `fix-ported-feature-lifecycles` | completed | 2026-11-04 | archived; resource-lifecycle spec created |
| F3 `fix-infrared-port-parity` | completed | 2026-11-08 | Level 4; disambiguated protocols, 16-bit loop counter, timing validation |
| F4 `fix-subghz-port-parity` | completed | 2026-11-09 | Level 4; CC1101 bounds, GDO0 IRQ ack cleanup, preset & .sub parity, 17 protocol spot-checks |
| F5 `fix-rfid-port-parity` | completed | 2026-11-09 | Level 4; UART lease arbitration, pinmux teardown, unsupported dump cleanup, cooperative cancellation |
| F6 `fix-rf24-port-parity` | completed | 2026-11-09 | Level 4; hardware SPI1 lease arbitration, CE teardown, jam wifi limitation |
| F7 `fix-lcdtap-port-parity` | completed | 2026-11-09 | Level 4; PIO teardown leak fixed, BP_RES_OWNER_LCDTAP leases, KS0108 aligned |
| F8 `fix-cell-port-parity` | completed | 2026-11-09 | Level 4; cooperative cancellation, immediate SMS prompt, USSD URC polling |
| F9 `fix-ps2-and-usb-sniffer-parity` | completed | 2026-11-09 | Level 4; 4 PIO programs 0-leak teardown, clock restore, keylogger leases |
| F10 `add-feature-conformance-tests` | completed | 2026-11-09 | Level 4; skip_specs; unified run_all.sh (26/26 pass), features command |
| A1 `stabilize-terminal-output` | completed | 2026-11-06 | Level 1 bootstrap; non-blocking ring buffer & flow control |
| A2 `fix-cross-core-io-violations` | completed | 2026-11-07 | Level 2; cross-core I/O absorbed into B4/B5 |
| A3 `add-watchdog-and-crash-recovery` | completed | 2026-11-06 | Level 1 bootstrap; hardware watchdog & flash crash dump |
| A4 `make-mode-switching-transactional` | completed | 2026-11-07 | Level 2; transactional mode rollback & soak verified in B7 |
| A5 `fix-adc-and-psu-ownership` | completed | 2026-11-07 | Level 2; Core 0 ADC/PSU ownership, reentrant locking, seqlock power snapshot |
| A6 `harden-storage-and-msc-hotplug` | completed | 2026-11-08 | Level 2; bounds-checked MSC LBA, demo disk write-protect, edge-triggered hotplug sense |
| B0 `adopt-static-memory-model` | completed | 2026-11-06 | Level 1; prerequisite for B1 |
| B1 `add-service-kernel` | completed | 2026-11-07 | Level 1; cooperative service kernel |
| B2 `add-protocol-job-runtime` | completed | 2026-11-07 | Level 1; protocol engine isolation |
| B3 `add-resource-lease-manager` | completed | 2026-11-07 | Level 1; peripheral arbitration & safety |
| B4 `route-terminal-io-through-control-core` | completed | 2026-11-07 | Level 1; Core 0 terminal ownership & isolation |
| B5 `move-storage-to-control-core` | completed | 2026-11-07 | Level 1; Core 0 storage ownership & cross-core RPC |
| B6 `move-binary-modes-to-transport-queues` | completed | 2026-11-07 | Level 1; CDC1 binary transport queues & non-blocking framing |
| B7 `split-control-and-protocol-cores` | completed | 2026-11-07 | Level 1; Core 0 control / Core 1 protocol data plane with transactional lifecycle |
| B8 `split-config-and-ui-snapshot` | completed | 2026-11-07 | Level 1; persisted config, runtime state, and seqlock UI snapshots |
| C1 `convert-rf-and-ir-commands-to-jobs` | completed | 2026-11-08 | Level 3; cancellable RF/IR/sniffer jobs with tracked peripheral leases |
| C2 `convert-capture-and-file-commands-to-jobs` | completed | 2026-11-08 | Level 3; capture, dump, flash, and file jobs with safe teardown |
| C3 `harden-syntax-and-command-parser` | completed | 2026-11-09 | Level 5; parser bounds checks, repeat validation, fuzzing suite |
| C4 `add-hil-and-soak-test-suite` | completed | 2026-11-09 | Level 5; automated HIL smoke, mode cycling, binary stress, and soak suite |
| C5 `remove-dead-code-and-update-docs` | completed | 2026-11-09 | Level 5; architecture guides, CLAUDE.md ownership rules, upstream divergence record |
