# Design

## Context

See `proposal.md` — Why. The fork merges four sources with different hardware assumptions:

- Bus Pirate upstream: RP2040/RP2350 base, PS2 sniffer, USB sniffer, PIO infrastructure.
- ESP32-Bit-Pirate: INFRARED command set, CELL, RFID/RF24 service patterns, SubGHz references.
- Flipper Zero: SubGHz protocol definitions and `.sub`/`.ir` formats.
- Vendor drivers: PN532, nRF24L01, Shapoco LcdTap.

A port can compile and still be wrong: wrong pin mux, wrong UART instance, leaked PIO program,
clock not restored, IRQ handler left installed, or a command that only prints "not implemented".
The audit therefore checks lifecycle and hardware assumptions, not only compilation.

## Goals / Non-Goals

**Goals:**

- One authoritative feature matrix in `openspec/FEATURES.md`.
- Every feature has an owner change and a concrete verification method.
- Every known porting defect has a file/line reference and a severity.
- Upstream parity claims are backed by a pinned commit and a diff or protocol test.
- The first fix change (F2) is ready to implement when the audit finishes.

**Non-Goals:**

- Fixing code in this change.
- Porting every ESP32/Flipper feature (Wi-Fi, Bluetooth, SDR, FM, etc.).
- Replacing the architecture; that remains Phase A/B.
- Buying or requiring hardware the user does not have; unavailable hardware is recorded as
  `unverified`, not assumed working.

## Decisions

### 1. Pin sources by commit, not branch

Every source gets an exact commit hash in `FEATURES.md`. The audit compares local files against that
checkout. Branch names are not evidence.

### 2. Status is evidence-based

Status values are defined in `FEATURES.md`. A feature cannot move to `verified` without:

- a host test that exercises its parser/logic, and
- a hardware test record from a real RB-Pirate.

If hardware is unavailable, the status stays `ported` or `partial` and the missing test is listed.

### 3. Lifecycle checklist is the primary audit tool

For every feature, check:

1. PIO block, program offset and state machine ownership.
2. `pio_add_program` / `pio_remove_program` balance.
3. `pio_sm_unclaim` / `pio_sm_set_enabled(false)` on exit.
4. DMA channel claim, IRQ enable and release.
5. GPIO function and buffer-direction restoration.
6. UART/SPI/I2C instance ownership and debug/terminal conflict.
7. Clock changes restored (`set_sys_clock_khz`).
8. PSU enable/disable through `psucmd_*`.
9. Large-buffer allocation failure handled.
10. File I/O errors handled and files closed.

### 4. Upstream comparison rules

- Compare behavior and protocol constants, not coding style.
- A deliberate divergence is allowed if it is documented and covered by a test.
- For SubGHz/IR, compare protocol names, timing constants and `.sub`/`.ir` field handling.
- For PS2/USB, diff against the pinned Bus Pirate upstream first; these files were merged directly.
- For RFID/RF24/CELL, compare command semantics against ESP32-Bit-Pirate, then verify with hardware.

### 5. Defect severity

| Severity | Meaning | Fix track |
|---|---|---|
| `S1` | Crash, hang, resource theft, clock corruption | F2 first |
| `S2` | Feature command does not work or gives wrong result | F3-F9 |
| `S3` | Missing/advertised command, poor error handling | F3-F9 |
| `S4` | Documentation or help mismatch | F10/docs |

### 6. Hardware matrix

The audit records which modules are available. At minimum:

- CC1101 module
- PN532 module
- two nRF24L01 modules
- IR receiver and IR LED/remote
- PS/2 keyboard/mouse
- USB low/full-speed target
- LCD target for LCDTAP
- SIMCom modem for CELL

If a module is unavailable, the feature stays `unverified` and the reason is recorded. It is not a
failure of the audit.

### 7. Output artifacts

- `openspec/FEATURES.md` is the living matrix.
- Each fix change gets its own `proposal.md`, delta specs, `design.md` and `tasks.md`.
- The audit change itself has `skip_specs: true`; it changes no product behavior.

## Risks / Trade-offs

- **Risk: audit becomes a second architecture project.** → Mitigation: time-box it to evidence
  gathering; no production code changes.
- **Risk: hardware unavailable.** → Mitigation: mark `unverified`; never claim working.
- **Risk: upstream moves during the audit.** → Mitigation: pin commits and record the date.
- **Risk: the matrix goes stale.** → Mitigation: update it in every feature-fix archive.

## Migration Plan

1. Pin sources and write the matrix.
2. Run static lifecycle audit.
3. Run host tests and record coverage.
4. Run hardware checks that are available.
5. Record defects and create fix changes.
6. Archive the audit; start F2.
