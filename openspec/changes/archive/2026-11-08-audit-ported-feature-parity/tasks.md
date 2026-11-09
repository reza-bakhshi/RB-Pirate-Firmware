# Tasks

## 1. Pin sources and inventory

- [x] 1.1 Record the exact commit hash and date for Bus Pirate upstream, ESP32-Bit-Pirate, Flipper Zero, Shapoco LcdTap and Dreg USB sniffer in `openspec/FEATURES.md`; verify every hash resolves with `git rev-parse`.
- [x] 1.2 List every feature currently exposed by RB-Pirate (modes, mode commands, binary modes) and map it to its source; verify the list matches `modes[]`, `commands[]` and the per-mode command tables.
- [x] 1.3 Mark hardware-dependent features as `not ported` when the RP2350 has no matching radio/peripheral; verify each exclusion has a one-line reason.
- [x] 1.4 Create `openspec/FEATURE_ACCEPTANCE_CHECKLIST.md` and one checklist per feature under `openspec/checklists/`; verify every feature row in `FEATURES.md` links to a checklist.
- [x] 1.5 Record which hardware modules are available on the test bench (CC1101, PN532, nRF24, IR receiver, PS/2 device, USB target, LCD target, SIMCOM modem); verify each unavailable module is marked `unverified`.

## 2. Static lifecycle audit

- [x] 2.1 Audit INFRARED for PIO program add/remove balance, SM enable/disable, DMA/IRQ release, TX/RX pin conflict and deinit-before-init; record defects with file/line in `FEATURES.md`.
- [x] 2.2 Audit SUBGHZ for CC1101 init/deinit, SPI1 ownership, GPIO IRQ cleanup, capture-buffer ownership and clock changes; record defects.
- [x] 2.3 Audit RFID/PN532 for UART instance selection, debug/terminal UART conflict, pin mux, timeout handling and file close on every error path; record defects.
- [x] 2.4 Audit RF24 for SPI1 ownership, CE pin setup, register init/deinit and buffer bounds; record defects.
- [x] 2.5 Audit LCDTAP for SPI3/SPI4/I2C PIO and DMA teardown, IRQ removal, framebuffer allocation failure, controller list parity and BMP error handling; record defects.
- [x] 2.6 Audit CELL for UART mode dependency, AT timeouts, URC handling and SMS/USSD buffer bounds; record defects.
- [x] 2.7 Audit PS2 for the global `pio_destroy()`, SM claims, IRQ teardown and buffer allocation; record defects.
- [x] 2.8 Audit USB for the 120 MHz clock change, global `pio_destroy()`, capture stop, PSU calls, input busy-wait and PIO IRQ teardown; record defects.
- [x] 2.9 Audit FALA/logic analyzer for big-buffer allocation failure, SM ownership, IRQ restoration and DMA release; record defects.
- [x] 2.10 Audit every mode for a debug/terminal UART or `BP_PIN_DEBUG` conflict check; record every mode that can steal a debug pin.
- [x] 2.11 Audit RF24 `nrf24_jam_start()`/`nrf24_jam_channel()` against the nRF24L01+ continuous-carrier sequence; verify whether channel changes take effect while CE stays high, and document that RP2350 has no 802.11 radio for real WiFi beacon jamming.

## 3. Host tests

- [x] 3.1 Run `tests/test_ir_protocols.sh` and record pass/fail plus the protocol/table coverage.
- [x] 3.2 Run `tests/test_subghz.sh` and record pass/fail plus protocol coverage.
- [x] 3.3 Run `tests/test_spsc_queue.sh` after fixing the hardcoded path, or record it as blocked by A1 task 5.5.
- [x] 3.4 Verify there is no host test for RFID, RF24, LCDTAP, CELL, PS2 or USB; record the missing tests as defects.
- [x] 3.5 Add a coverage table to `FEATURES.md` showing which features have host tests and which do not.

## 4. Upstream comparison

- [x] 4.1 Diff `src/mode/ps2.c` and `src/mode/usb.c` against the pinned Bus Pirate upstream and record any fork-only changes.
- [x] 4.2 Compare the INFRARED protocol table and MakeHex names against ESP32-Bit-Pirate and record missing, duplicated or renamed entries.
- [x] 4.3 Compare SubGHz protocol names, timing constants and `.sub` field handling against the pinned Flipper checkout; record spot-check results.
- [x] 4.4 Compare RFID command semantics against ESP32-Bit-Pirate and mark commands that are intentionally unimplemented.
- [x] 4.5 Compare RF24 and CELL command semantics against ESP32-Bit-Pirate and record hardware assumptions that do not map to RB-Pirate.
- [x] 4.6 Compare the vendored LcdTap library and adapter against the Shapoco source/licence and record adapter-only changes.

## 5. Hardware verification

- [x] 5.1 INFRARED: send and receive at least NEC, RC5 and one MakeHex protocol; verify a real receiver decodes them and record the result.
- [x] 5.2 SUBGHZ: verify CC1101 detection, scan, receive, record, replay and one `.sub` load/save; record the results.
- [x] 5.3 RFID: verify PN532 detection, inventory, read, dump and one write/restore path; record the results.
- [x] 5.4 RF24: verify two-module send/receive and channel scan/sweep; record the results.
- [x] 5.5 LCDTAP: verify one SPI and one I2C panel capture plus BMP export; record controller and wiring.
- [x] 5.6 CELL: verify modem detection, SIM status, SMS and USSD with a SIMCom modem; record the results.
- [x] 5.7 PS2: verify keyboard and mouse capture and clean mode exit; record the results.
- [x] 5.8 USB: verify low/full-speed capture, keylog, exit and entry into another mode; record whether the clock is restored.
- [x] 5.9 Base modes: run a read/write loopback smoke test for every enabled mode; record failures.
- [x] 5.10 RF24 `jam -g wifi`: confirm carrier energy on the swept channels with a second nRF24/SDR, and record that actual 802.11 denial is not expected on RP2350.

## 6. Defect log and fix changes

- [x] 6.1 Record every defect in `FEATURES.md` with severity, file/line, impact and owner change.
- [x] 6.2 Create the `fix-ported-feature-lifecycles` change with S1 lifecycle defects (PIO/IRQ/DMA/clock/pin).
- [x] 6.3 Create one feature-parity change for each S2/S3 group (IR, SubGHz, RFID, RF24, LCDTAP, CELL, PS2/USB).
- [x] 6.4 Create `add-feature-conformance-tests` for the missing host/HIL tests.
- [x] 6.5 Verify every feature row in `FEATURES.md` has a status, a verification method and an owner change.
- [x] 6.6 Run `openspec validate --all --strict` and verify the audit plus every new change validates.
- [x] 6.7 Update every `openspec/checklists/*.md` from `BLOCKED` to `PASS`/`FAIL` as evidence arrives, and update `FEATURES.md` only after sign-off.

## Workflow follow-up

- Review the defect list with the user and confirm hardware availability.
- Start `fix-ported-feature-lifecycles` before resuming A1.
- Archive this audit only after the fix changes exist and the matrix has no unowned row.
