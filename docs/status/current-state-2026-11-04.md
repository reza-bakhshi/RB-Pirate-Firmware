# RB-Pirate Firmware — Current State Report

**Date:** 2026-11-04  
**Branch:** `refactor/kernel`  
**Latest commit:** see `git log -1` on `refactor/kernel`  
**Working tree:** clean

---

## 1. Level map

| Level | Scope | Status |
|---|---|---|
| **Level 0 — Planning and specs** | OpenSpec project, roadmap, feature matrix, feature specs, checklists | complete |
| **Level 1 — Core runtime + multicore split** | A1/A3 bootstrap safety, B1–B8 service kernel, jobs, leases, control/data split | **next** |
| **Level 2 — Reliability** | A2 cross-core I/O, A4 mode switching, A5 ADC/PSU, A6 storage/MSC | planned |
| **Level 3 — Command conversion** | C1 RF/IR jobs, C2 capture/file jobs | planned |
| **Level 4 — Feature parity** | F1 audit, F3–F10 static audits/fixes (F2 archived early) | planned |
| **Level 5 — Hardening** | C3 parser, C4 HIL/soak, C5 cleanup | planned |

**Level 1 is the multicore architecture level.** It is a milestone, not a single task. The feature-parity gate is now **Level 4**.

Level 1 order:

1. A1 non-blocking terminal output (bootstrap safety).
2. A3 watchdog and crash recovery (bootstrap safety).
3. B0 static memory model (no heap in new code/pools/arenas).
4. B1 service kernel.
5. B2 protocol job runtime.
6. B3 resource lease manager.
7. B4 terminal ownership.
8. B5 storage ownership.
9. B6 binary transport.
10. B7 control/protocol core split.
11. B8 config and UI snapshot.

---

## 2. What is complete

### 2.1 OpenSpec project

- `openspec/config.yaml` — project context, dual-core target, hardware inventory, feature policy.
- `openspec/ROADMAP.md` — all 19 changes F1–F10 and A1–C5, with dependencies, steps, verification and ARCH mapping.
- `openspec/FEATURES.md` — pinned upstream commits, feature matrix, static audit results, owner changes.
- `openspec/HARDWARE.md` — full bench inventory and wiring reference; all modules available.
- `openspec/FEATURE_ACCEPTANCE_CHECKLIST.md` — mandatory per-feature checklist.
- `openspec/checklists/` — 10 feature checklists, all `READY`.

### 2.2 Feature specs

`openspec list --specs` shows 10 capabilities, each with 4 requirements:

```text
base-modes       binary-modes     cell          infrared
lcdtap           ps2-sniffer      rf24          rfid
subghz           usb-sniffer
```

### 2.3 F2 — `fix-ported-feature-lifecycles`

**Archived 2026-11-04 as `2026-11-04-fix-ported-feature-lifecycles`; `resource-lifecycle` spec created.**

Fixed:

- INFRARED PIO deinit guards, mod-freq program leak, SM disable before removal.
- LCDTAP SPI3 program leak via `spi3Deinit()`; KS0108 claim removed from README.
- USB sniffer clock save/restore.
- USB PSU through `psucmd_*`.
- USB PIO ownership: tracked release instead of global `pio_destroy()`.
- PS2 PIO ownership: tracked release of four SMs/programs; global clear removed.
- RF24 jam CE toggle and honest WiFi-band carrier-sweep labeling.
- Logic analyzer `mem_alloc()` failure handling.
- ADC spinlock release race.
- RFID debug-UART pin conflict guard.
- SPSC host-test runner path.

---

## 3. Verification

### Build

```text
FLASH: 970,196 B / 16 MB (5.77%)
RAM:   307,768 B / 512 KB (58.70%)
```

### Host tests

```text
tests/test_ir_protocols.sh   ALL PASSED
tests/test_subghz.sh         ALL PASS + ASan/UBSan PASS
tests/test_spsc_queue.sh     11/11 PASS
```

### OpenSpec

```text
Totals: 13 passed, 0 failed (13 items)
```

---

## 4. Work in progress

### F1 — `audit-ported-feature-parity`

- Status: active.
- 44 tasks, currently 0/44 checked.
- Static audit findings are recorded in `FEATURES.md`, but the F1 task list has not been reconciled with the work already done under F2.
- Hardware checklists are ready but not executed; the user has confirmed all modules are available.

### F2 — `fix-ported-feature-lifecycles`

- Archived.
- `resource-lifecycle` is now in `openspec/specs/resource-lifecycle/spec.md`.

---

## 5. Known open defects

| Defect | Owner | State |
|---|---|---|
| INFRARED duplicate protocol names (`jvc`, `panasonic`, `rc5`, `rc6`, `sharp`, `denon`, `mitsubishi`) | F3 | open |
| INFRARED raw timing validation | F3 | open |
| USB capture-stop menu key `p` unimplemented | F9 | open |
| RF24 full command parity beyond the jam fix | F6 | static audit pending |
| RFID unsupported tag operations and full command parity | F5 | static audit pending |
| LCDTAP controller parity and BMP export paths | F7 | static audit pending |
| CELL AT/URC paths | F8 | static audit pending |
| PS2/USB full command and menu audit | F9 | static audit pending |
| SubGHz Flipper protocol spot checks | F4 | static audit pending |
| All command-level behavior | F1–F10 | code-audited only; no hardware evidence |

---

## 6. Important constraints for the next AI

- Work on branch `refactor/kernel`; do not start Level 2 yet.
- Static code audit is the current method; no hardware device is required.
- Do not mark a feature `verified` without hardware evidence. Use `CODE-AUDITED` / `STATIC PASS` for code review.
- Preserve command syntax, translation keys, FlatBuffers schema, PIO timing and board pin map.
- No global `pio_clear_instruction_memory()` / `pio_destroy()` in mode code.
- No direct `psu_enable` / `psu_disable` in mode code; use `psucmd_*`.
- No new `printf` on Core 1 when the core-split work begins.
- Every OpenSpec change must pass `openspec validate --all --strict`.
- Build and host tests after every change; record flash/RAM.

---

## 7. Next actions

1. Start Level 1 with A1 `stabilize-terminal-output` (bootstrap safety).
2. Then A3 `add-watchdog-and-crash-recovery`.
3. Then B0 `adopt-static-memory-model`.
4. Then B1 `add-service-kernel`.
5. Continue B2, B3, B4–B8.
5. Do not start Level 2 reliability until the core split is live.
6. Feature parity F1/F3–F10 waits until Level 4; F2 is already archived.

The copy-paste handoff prompt is at
[`level-1-handoff-prompt.md`](level-1-handoff-prompt.md).
