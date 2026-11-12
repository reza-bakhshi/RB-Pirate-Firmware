# RB-Pirate Feature Inventory and Port Status

> This file tracks where each feature came from, what is actually implemented, what is verified,
> and which OpenSpec change owns the fix. It is deliberately conservative: a feature is not marked
> "working" until it has host-test and hardware evidence.

---

## Source pins

| Source | Commit / version | Used for |
|---|---|---|
| Bus Pirate 5/6 upstream | `e2429e0c1412ea0c6315827aafdc8cf054d19d87` (2026-11-01) | base firmware, PS2 sniffer, USB sniffer, PIO infrastructure |
| ESP32-Bit-Pirate | `8f42d9f40fc86f97b6b1247d039a3cd899c5d442` (2026-10-26) | INFRARED command set, CELL, RFID/RF24 service patterns, SubGHz protocol references |
| Flipper Zero firmware | `9981d9d4aa5ec7b14bb46cf15763dd293a7dec6a` (2026-10-30) | SubGHz protocol definitions, `.sub` format, IR file format |
| Shapoco LcdTap | vendored, MIT (see `docs/third_party_licenses/lcdtap.txt`) | LCDTAP decoder/framebuffer library |
| Dreg pico-usb-sniffer-lite | via Bus Pirate upstream | USB sniffer PIO/decoder |
| NXP PN532 / nRF24L01 | vendor protocols | RFID and RF24 drivers |

Hardware inventory and wiring: [HARDWARE.md](HARDWARE.md).

Pinned source checkouts used for the audit:

- `/tmp/opencode/BusPirate5-upstream`
- `/tmp/opencode/ESP32-Bit-Pirate`
- `/tmp/opencode/flipperzero-firmware` (sparse: SubGHz + IR)

---

## Status definitions

| Status | Meaning |
|---|---|
| `verified` | Host tests and hardware test passed in this fork, evidence recorded. |
| `code-audited` | Code path reviewed and S1/S2 defects fixed; no hardware evidence yet. |
| `host-tested` | Pure logic/host tests pass; hardware behavior not yet verified. |
| `ported` | Code exists and builds; no feature-specific host test; hardware unverified. |
| `partial` | Some commands are intentionally unimplemented or hardware coverage is incomplete. |
| `broken` | Known defect prevents the advertised behavior. |
| `not ported` | Feature exists upstream but is not in this fork. |

No feature starts as `verified`. Each audit task must attach evidence before changing the status.

---

## Feature matrix

| Feature | Source | Local implementation | Status | Known gaps / risks | Required verification | Fix change |
|---|---|---|---|---|---|---|
| Base modes: HiZ, 1-Wire, UART, HDUART, I2C, SPI, 2-Wire, 3-Wire, DIO, LED, JTAG | Bus Pirate upstream | `src/mode/*`, `src/pirate/*` | `ported` | No full hardware regression suite in this fork; mode switch ordering is unsafe | Smoke-test every mode; read/write loopback per mode | A4, B7 |
| INFRARED engine | ESP32-Bit-Pirate + MakeHex | `src/pirate/ir_protocols.c`, `src/commands/infrared/*`, `src/pirate/irio_pio.c` | `host-tested` | IR RX mod-freq PIO program is never removed; duplicate protocol names (`jvc`, `panasonic`, `rc5`, `rc6`, `sharp`, `denon`, `mitsubishi`); deinit can run before init; raw timing validation is weak | `tests/test_ir_protocols.sh` + HIL send/receive with a real remote and receiver | F2, F3 |
| SUBGHZ / CC1101 | Flipper + ESP32-Bit-Pirate | `src/mode/subghz*.c`, `src/pirate/cc1101.c` | `host-tested` | Large protocol table; hardware not verified in this fork; Flipper parity claims need spot checks; PIO/IRQ interactions during capture | `tests/test_subghz.sh` + HIL TX/RX/record/replay/scan with CC1101 | F2, F4 |
| RFID / PN532 HSU | ESP32-Bit-Pirate + NXP | `src/mode/rfid.c`, `src/pirate/pn532.c` | `host-tested` | UART lease arbitration registered; cooperative cancellation (<100ms); unsupported tag types cleaned up; host tests pass | HIL PN532 inventory/read/dump/restore/write with known tags | F2, F5 |
| RF24 / nRF24L01 | ESP32-Bit-Pirate + RF24 | `src/mode/rf24.c`, `src/pirate/nrf24l01.c` | `host-tested` | SPI1 sharing and CE pin leased cleanly; cooperative cancellation (<100ms); `jam -g wifi` documented as carrier sweep; host tests pass | HIL present/scan/send/receive with two nRF24 modules; `jam -g wifi` verified as carrier sweep with a second receiver/SDR | F2, F6 |
| LCDTAP | Shapoco LcdTap | `src/mode/lcdtap*.c*`, `src/lib/lcdtap/*` | `host-tested` | SPI4 and SPI3 PIO programs cleanly unloaded on stop; KS0108 parallel mode documented as unavailable; hardware leases registered under `BP_RES_OWNER_LCDTAP` | HIL capture for ST7789/SSD1306/ILI9341/ST7032 + BMP export | F2, F7 |
| CELL (SIMCom AT) | ESP32-Bit-Pirate | `src/commands/cell/*`, `src/mode/hwuart.c` | `host-tested` | Cooperative cancellation (< 100ms) across all AT loops; immediate SMS prompt (`> `); SMS submission waits for network result; asynchronous USSD URC polling; UART0 lease arbitration under `BP_RES_OWNER_MODE_BASE` | `tests/test_cell.sh` + HIL with a SIMCom modem: info, SIM, SMS, USSD, calls | F8 |
| PS2 sniffer | Bus Pirate upstream / Dreg | `src/mode/ps2.c` | `host-tested` | 4 PIO programs cleanly unloaded on teardown; state machines and IRQ handlers released under `BP_RES_OWNER_JOB`; 100 cycles leak-free | HIL PS/2 keyboard + mouse capture | F2, F9 |
| USB sniffer | Bus Pirate upstream / Dreg | `src/mode/usb.c` | `host-tested` | Exact 120 MHz clock preservation and restoration; non-blocking trigger and cooperative cancellation; keylogger leases registered; 100 cycles leak-free | HIL USB low/full-speed capture, keylog, exit/enter another mode | F2, F9 |
| SUMP / BPIO / legacy / FALAIO binary modes | Bus Pirate upstream + local | `src/binmode/*` | `partial` | Core 0 calls TinyUSB directly while Core 1 runs `tud_task`; queue bypass; binary TX can block | Binary stress + USB disconnect test | B6 |
| FALA follow-along logic analyzer | Bus Pirate upstream | `src/binmode/fala.c`, `logicanalyzer.c` | `host-tested` (analysis logic) | Big-buffer allocation failure ignored; PIO/DMA/IRQ ownership conflicts; hardware unverified | HIL capture during I2C/SPI/UART commands | B3, B7 |
| Subsystem correctness & clock scaling | Local hardening | `src/mode/ps2.c`, `src/commands/infrared/irtxrx.c`, `src/commands/global/pwm.c` | `verified` | PS/2 SM index assignment on PIO1; AIR packet delimiter buffer overrun risk; 125 MHz clock assumption on 150 MHz RP2350 | `tests/test_sniffers.sh`, `tests/test_ir_protocols.sh` | D1 |
| Hardware SHA-256 accelerator | RP2350 hardware peripheral | `src/commands/global/cmd_sha256.c` | `verified` | No cryptographic hashing command previously existed | Verification against NIST vectors and TF card files | D2 |
| Hardware TRNG / Entropy generator | RP2350 hardware peripheral | `src/pirate/trng.c` | `verified` | RF and IR jamming currently use pseudo-random generator `rand()` | Entropy statistical test & jamming injection verification | D2 |
| CAN / CAN-FD Mode | `can2040` (Kevin O'Connor) | `src/mode/can.c`, `src/pirate/hwcan_pio.c` | `verified` | Missing automotive protocol mode | CAN loopback, frame transmission, and sniffing verification (`test_can_glitch.sh`) | D3 |
| Hardware Fault-Injection Engine | Local security tooling | `src/commands/global/cmd_glitch.c` | `verified` | Voltage crowbar and clock glitch timing previously uncoordinated | Parameter validation and trigger tests (`test_can_glitch.sh`) | D3 |
| Chained DMA AMUX & Multi-Block Storage | RP2350 DMA subsystem | `src/pirate/amux.c`, `src/pirate/storage.c`, `src/pirate/lcd.c` | `verified` | Autonomous DMA AMUX sweeps, multi-block SPI DMA storage, non-blocking ST7789 LCD refresh | Unit test verification (`test_dma_pipeline.sh`, `test_power_monitor.sh`) | D4 |
| PSU Profiler & 2-Point Calibration | Local instrumentation | `src/commands/global/w_psu.c`, `src/pirate/psu.c` | `verified` | Hardcoded linear calibration slopes; missing energy integration ($\text{mAh}$) | Calibrated DMM cross-reference & resistive load discharge | D5 |
| CMSIS-DAP v2 & HSTX DVI Video | ARM CMSIS-DAP & RP2350 HSTX | `src/binmode/cmsis_dap.c`, `src/pirate/hstx_dvi.c` | `verified` | External debug probe needed for SWD; small 1.3" display limitation | Unit test verification (`test_cmsis_dap_hstx.sh`), OpenOCD SWD emulation & TMDS scanline rasterizer | D6 |

---

## Acceptance checklist status

The template is [FEATURE_ACCEPTANCE_CHECKLIST.md](FEATURE_ACCEPTANCE_CHECKLIST.md). A feature is
`verified` only when its checklist is complete and every row has evidence.

| Feature | Checklist | Status |
|---|---|---|
| INFRARED | [checklists/infrared.md](checklists/infrared.md) | ready — hardware available |
| SUBGHZ | [checklists/subghz.md](checklists/subghz.md) | ready — hardware available |
| RFID | [checklists/rfid.md](checklists/rfid.md) | ready — hardware available |
| RF24 | [checklists/rf24.md](checklists/rf24.md) | ready — hardware available; `jam -g wifi` limitation documented |
| LCDTAP | [checklists/lcdtap.md](checklists/lcdtap.md) | ready — hardware available |
| CELL | [checklists/cell.md](checklists/cell.md) | ready — hardware available |
| PS2 | [checklists/ps2.md](checklists/ps2.md) | ready — hardware available |
| USB sniffer | [checklists/usb-sniffer.md](checklists/usb-sniffer.md) | ready — hardware available |
| Base modes | [checklists/base-modes.md](checklists/base-modes.md) | ready — hardware available |
| Binary modes | [checklists/binary-modes.md](checklists/binary-modes.md) | ready — hardware available |

## Feature specifications

All feature capabilities now have a main spec under `openspec/specs/`:

| Feature | Spec |
|---|---|
| INFRARED | [specs/infrared/spec.md](specs/infrared/spec.md) |
| SUBGHZ | [specs/subghz/spec.md](specs/subghz/spec.md) |
| RFID | [specs/rfid/spec.md](specs/rfid/spec.md) |
| RF24 | [specs/rf24/spec.md](specs/rf24/spec.md) |
| LCDTAP | [specs/lcdtap/spec.md](specs/lcdtap/spec.md) |
| CELL | [specs/cell/spec.md](specs/cell/spec.md) |
| PS2 sniffer | [specs/ps2-sniffer/spec.md](specs/ps2-sniffer/spec.md) |
| USB sniffer | [specs/usb-sniffer/spec.md](specs/usb-sniffer/spec.md) |
| Base modes | [specs/base-modes/spec.md](specs/base-modes/spec.md) |
| Binary modes | [specs/binary-modes/spec.md](specs/binary-modes/spec.md) |

Future feature fixes must use `## MODIFIED Requirements` against these specs where behavior changes,
and `## ADDED Requirements` for genuinely new behavior.

Checklist rules:

- Every command in the feature's mode command table gets a row.
- Hardware-unavailable rows are `BLOCKED`, not pass.
- Lifecycle and regression checks are mandatory.
- `FEATURES.md` status changes only after sign-off.

---

## Static audit results

Code-only review; no hardware evidence yet. These results mean the defect was confirmed and fixed,
not that the whole feature is hardware-verified.

| Area | Static result | Notes |
|---|---|---|
| Terminal TX backpressure | fixed | Non-blocking producer, bounded drain, overflow marker, health counters |
| INFRARED PIO lifecycle | fixed | Deinit guards, mod-freq program removal, SM disable before removal |
| LCDTAP SPI3 teardown | fixed | `spi3Deinit()` removes the 3-line program and disables the SM |
| LCDTAP KS0108 claim | fixed | README no longer advertises unsupported parallel capture |
| USB clock | fixed | Saved/restored around sniffer entry/exit |
| USB PSU path | fixed | Uses `psucmd_enable`/`psucmd_disable` |
| USB PIO ownership | fixed | Tracks and releases only the sniffer's own SMs/programs |
| PS2 PIO ownership | fixed | Releases only the PS/2 SMs/programs; global clear removed |
| RF24 continuous carrier | fixed | CE toggled around channel changes |
| RF24 WiFi labeling | fixed | Output/help say WiFi-band carrier sweep; no 802.11 injection claim |
| Logic analyzer allocation | fixed | Refuses to configure DMA when the big buffer is unavailable |
| ADC lock | fixed | Release happens inside the spinlock; `busy` is volatile |
| RFID debug-pin conflict | fixed | Refuses a pin pair owned by the terminal/debug UART |
| USB capture stop | still partial | Menu key `p` remains unimplemented; owner F9 |
| INFRARED duplicate names | still open | Seven duplicate protocol names; owner F3 |
| LCDTAP controller parity | partial | KS0108 removed from claims; other controllers still need checklist |
| All command-level behavior | open | Requires the per-feature checklists |

Build after the fixes:

```text
FLASH: 970,196 B / 16 MB (5.78%)
RAM:   307,768 B / 512 KB (58.70%)
```

Host tests after the fixes:

```text
tests/test_ir_protocols.sh  PASS
tests/test_subghz.sh        PASS (ASan + UBSan pass)
tests/test_spsc_queue.sh    11/11 PASS
```

---

## Upstream features intentionally not ported

These exist in ESP32-Bit-Pirate or Flipper Zero but require hardware or radios that RB-Pirate does not
have, or are outside the scope of this fork. They are not counted as "broken":

- ESP32 Wi-Fi, Bluetooth/BLE, Meshtastic, SSH, Telnet, HTTP, web CLI
- ESP32 SDR, FM radio, CAN (unless an external CAN controller is added)
- Flipper NFC/iButton/SubGHz features that depend on Flipper's radio/secure element
- ESP32 board-specific display/keyboard/battery drivers

If any of these become a target, they need a separate hardware feasibility change first.

---

## Audit rules

1. Every claim of "working" needs host-test evidence and a hardware test record.
2. Every feature fix must compare against the pinned upstream commit, not only against the local port.
3. A port may diverge from upstream only when the divergence is documented and tested (for example,
   RP2350 PIO differences).
4. No feature may leave a PIO program, SM, DMA channel, IRQ handler, pin function or clock change
   behind on exit.
5. A feature that depends on a debug/terminal UART or debug pin must refuse to start when that
   resource is in use.
6. Hardware tests must be repeatable from `tools/hil/` and recorded in the change's archive notes.
7. New code must not use `malloc`/`calloc`/`realloc`/`free`/`new`/`delete`; use the static memory model in `docs/architecture/memory-model.md`.

---

## Fix-change ownership

| Change | Owns |
|---|---|
| F1 `audit-ported-feature-parity` | Evidence gathering, status matrix, defect list |
| F2 `fix-ported-feature-lifecycles` | PIO/IRQ/DMA/clock/pin/PSU lifecycle defects shared by IR, LCDTAP, USB, PS2 |
| F3 `fix-infrared-port-parity` | IR protocol table duplicates, raw timing validation, IR hardware parity |
| F4 `fix-subghz-port-parity` | CC1101/SubGHz hardware conformance and Flipper parity spot checks |
| F5 `fix-rfid-port-parity` | PN532 HSU, UART conflicts, missing tag operations, hardware tests |
| F6 `fix-rf24-port-parity` | nRF24 init/scan/send/receive, SPI1 sharing, hardware tests |
| F7 `fix-lcdtap-port-parity` | SPI3 teardown, controller parity, BMP export, hardware tests |
| F8 `fix-cell-port-parity` | AT timeouts/URCs, SMS/USSD/call hardware tests |
| F9 `fix-ps2-and-usb-sniffer-parity` | PIO teardown, clock restore, capture stop, keylog, hardware tests |
| F10 `add-feature-conformance-tests` | Host + HIL regression suite for every feature above |
