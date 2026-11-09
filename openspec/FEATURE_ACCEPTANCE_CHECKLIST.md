# Feature Acceptance Checklist

> Mandatory gate for every ported feature or protocol.  
> A feature is not `verified` in `openspec/FEATURES.md` until this checklist is complete for every
> command the feature exposes, with host-test and hardware evidence attached.

---

## Rules

1. **Every command gets a row.** If the mode command table has 7 commands, the checklist has 7
   command checks. Hidden subcommands count too.
2. **No row may be marked pass from code reading alone.** A row needs an observed result from a real
   RB-Pirate, or a host test for pure logic.
3. **Hardware unavailable = blocked, not pass.** Record the missing module and keep the feature
   `unverified`.
4. **Lifecycle is part of acceptance.** Enter, run every command, cancel, exit, and enter another
   mode. Check PIO/SM/DMA/IRQ/clock/pin/PSU restoration.
5. **Error paths are tested.** Missing module, wrong wiring, no VOUT, cancel key, USB disconnect.
6. **Evidence is attached.** Serial log, second-device output, screenshot, or test output. A
   checklist without evidence is not complete.
7. **Update `FEATURES.md` only after sign-off.** The status moves to `verified` only when every row
   passes.

---

## Template

Copy this file to `openspec/checklists/<feature>.md` and fill it in.

```markdown
# <Feature> Acceptance Checklist

- Source commit:
- Local implementation:
- Hardware required:
- Firmware build:
- Tester:
- Date:
- Overall result: PASS / FAIL / BLOCKED

## 1. Preconditions

- [ ] Firmware builds: `cmake --build build_rp2350 --parallel --target bus_pirate6`
- [ ] Host test passes (if the feature has one): `<command>`
- [ ] Required module is powered from VOUT at the correct voltage
- [ ] Wiring matches the mode help
- [ ] `health` shows no prior fault/overflow
- [ ] Debug/terminal UART is not using the feature pins

## 2. Command checks

For each command in the mode command table:

### `<command>`

- Input:
- Expected observable result:
- Actual result:
- Evidence:
- Pass: [ ]

### `<command>`

- Input:
- Expected observable result:
- Actual result:
- Evidence:
- Pass: [ ]

## 3. Error and cancellation checks

- [ ] Missing module returns a clear error, no hang
- [ ] Wrong wiring returns a clear error, no hang
- [ ] No VOUT refuses to start
- [ ] Any-key cancel returns to the prompt
- [ ] USB disconnect/reconnect does not hang the feature
- [ ] Repeated cancel does not leak resources

## 4. Lifecycle checks

- [ ] Enter the mode, run every command, exit, enter again: works
- [ ] Enter a different mode after exit: works
- [ ] No PIO program/SM leak after 100 enter/exit cycles
- [ ] No DMA/IRQ handler left installed
- [ ] Clock restored to the pre-mode value
- [ ] Pins/buffer directions restored
- [ ] PSU and pin labels restored
- [ ] `health`/`res` shows no leak

## 5. Regression checks

- [ ] Terminal still responds after the feature
- [ ] Toolbars/status bar still update
- [ ] FALA/logic analyzer still works
- [ ] Storage still mounts
- [ ] Other modes still start

## 6. Sign-off

- [ ] All command checks pass
- [ ] All error/cancel checks pass
- [ ] All lifecycle checks pass
- [ ] All regression checks pass
- [ ] `openspec/FEATURES.md` status updated with evidence link
```

---

## Required checklists before A1 resumes

At minimum, these files must exist and be filled:

- `openspec/checklists/infrared.md`
- `openspec/checklists/subghz.md`
- `openspec/checklists/rfid.md`
- `openspec/checklists/rf24.md`
- `openspec/checklists/lcdtap.md`
- `openspec/checklists/cell.md`
- `openspec/checklists/ps2.md`
- `openspec/checklists/usb-sniffer.md`
- `openspec/checklists/base-modes.md`
- `openspec/checklists/binary-modes.md`

A checklist may be marked `BLOCKED` when hardware is unavailable, but the feature cannot be marked
`verified`.
