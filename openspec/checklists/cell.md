# CELL (SIMCom AT) Acceptance Checklist

- Source: ESP32-Bit-Pirate `8f42d9f40fc86f97b6b1247d039a3cd899c5d442`
- Local implementation: `src/commands/cell/*`, UART mode hooks
- Hardware required: SIMCom-compatible modem, powered SIM, UART wiring
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


### `cellmodem`

- Input: `cellmodem`
- Expected: Command completes with a parsed AT response; no hang
- Actual:
- Evidence:
- Pass: [ ]

### `cellnet`

- Input: `cellnet`
- Expected: Command completes with a parsed AT response; no hang
- Actual:
- Evidence:
- Pass: [ ]

### `cellops`

- Input: `cellops`
- Expected: Command completes with a parsed AT response; no hang
- Actual:
- Evidence:
- Pass: [ ]

### `cellsim`

- Input: `cellsim`
- Expected: Command completes with a parsed AT response; no hang
- Actual:
- Evidence:
- Pass: [ ]

### `cellpin`

- Input: `cellpin`
- Expected: Command completes with a parsed AT response; no hang
- Actual:
- Evidence:
- Pass: [ ]

### `cellbook`

- Input: `cellbook`
- Expected: Command completes with a parsed AT response; no hang
- Actual:
- Evidence:
- Pass: [ ]

### `cellsms`

- Input: `cellsms`
- Expected: Command completes with a parsed AT response; no hang
- Actual:
- Evidence:
- Pass: [ ]

### `cellcall`

- Input: `cellcall`
- Expected: Command completes with a parsed AT response; no hang
- Actual:
- Evidence:
- Pass: [ ]

### `cellussd`

- Input: `cellussd`
- Expected: Command completes with a parsed AT response; no hang
- Actual:
- Evidence:
- Pass: [ ]

### `cellmode`

- Input: `cellmode`
- Expected: Command completes with a parsed AT response; no hang
- Actual:
- Evidence:
- Pass: [ ]

### `cellat`

- Input: `cellat`
- Expected: Command completes with a parsed AT response; no hang
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
