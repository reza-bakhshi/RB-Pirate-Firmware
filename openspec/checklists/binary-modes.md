# Binary modes Acceptance Checklist

- Source: Bus Pirate upstream `e2429e0c1412ea0c6315827aafdc8cf054d19d87` + local FALA
- Local implementation: `src/binmode/*`
- Hardware required: Host tool for SUMP/BPIO/legacy/FALAIO/IRToy/CH32V003
- Firmware build: `bus_pirate6`
- Tester:
- Date:
- Overall result: PASS / FAIL / BLOCKED

## 1. Preconditions

- [ ] Firmware builds: `cmake --build build_rp2350 --parallel --target bus_pirate6`
- [ ] Host test passes (if the feature has one): `host tools under `tools/` (record coverage)`
- [ ] Required hardware is powered from VOUT at the correct voltage
- [ ] Wiring matches the mode help
- [ ] `health` shows no prior fault/overflow
- [ ] Terminal/debug UART is not using the feature pins

## 2. Command checks


### `sump`

- Input: `binmode sump` then host tool
- Expected: Host tool connects, exchanges data, disconnects cleanly
- Actual:
- Evidence:
- Pass: [ ]

### `bpio`

- Input: `binmode bpio` then host tool
- Expected: Host tool connects, exchanges data, disconnects cleanly
- Actual:
- Evidence:
- Pass: [ ]

### `legacy`

- Input: `binmode legacy` then host tool
- Expected: Host tool connects, exchanges data, disconnects cleanly
- Actual:
- Evidence:
- Pass: [ ]

### `falaio`

- Input: `binmode falaio` then host tool
- Expected: Host tool connects, exchanges data, disconnects cleanly
- Actual:
- Evidence:
- Pass: [ ]

### `irtoy`

- Input: `binmode irtoy` then host tool
- Expected: Host tool connects, exchanges data, disconnects cleanly
- Actual:
- Evidence:
- Pass: [ ]

### `ch32v003`

- Input: `binmode ch32v003` then host tool
- Expected: Host tool connects, exchanges data, disconnects cleanly
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
