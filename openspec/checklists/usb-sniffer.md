# USB sniffer Acceptance Checklist

- Source: Bus Pirate upstream `e2429e0c1412ea0c6315827aafdc8cf054d19d87` / Dreg
- Local implementation: `src/mode/usb.c`
- Hardware required: USB low/full-speed target device
- Firmware build: `bus_pirate6`
- Tester:
- Date:
- Overall result: PASS / FAIL / BLOCKED

## 1. Preconditions

- [ ] Firmware builds: `cmake --build build_rp2350 --parallel --target bus_pirate6`
- [ ] Host test passes (if the feature has one): `none (record as missing)`
- [ ] Required hardware is powered from VOUT at the correct voltage
- [ ] Wiring matches the mode help
- [ ] `health` shows no prior fault/overflow
- [ ] Terminal/debug UART is not using the feature pins

## 2. Command checks


### `usb`

- Input: `usb`
- Expected: Enters sniffer menu; clock changes to 120 MHz
- Actual:
- Evidence:
- Pass: [ ]

### `s`

- Input: `s`
- Expected: Capture starts and stops
- Actual:
- Evidence:
- Pass: [ ]

### `b`

- Input: `b`
- Expected: Buffer display works
- Actual:
- Evidence:
- Pass: [ ]

### `k`

- Input: `k`
- Expected: Keylog capture works
- Actual:
- Evidence:
- Pass: [ ]

### `e`

- Input: `e`
- Expected: Capture speed cycles
- Actual:
- Evidence:
- Pass: [ ]

### `g`

- Input: `g`
- Expected: Trigger cycles
- Actual:
- Evidence:
- Pass: [ ]

### `q`

- Input: `q`
- Expected: Exits and restores the clock
- Actual:
- Evidence:
- Pass: [ ]


## 3. Error and cancellation checks

- [ ] Missing hardware returns a clear error, no hang
- [ ] Wrong wiring returns a clear error, no hang
- [ ] No VOUT refuses to start where required
- [ ] Any-key cancel returns to the prompt (where supported)
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
- [ ] `openspec/FEATURES.md` status updated with evidence
