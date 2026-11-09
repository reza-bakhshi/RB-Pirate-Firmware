# SUBGHZ / CC1101 Acceptance Checklist

- Source: Flipper Zero `9981d9d4aa5ec7b14bb46cf15763dd293a7dec6a` + ESP32-Bit-Pirate `8f42d9f40fc86f97b6b1247d039a3cd899c5d442`
- Local implementation: `src/mode/subghz*.c`, `src/pirate/cc1101.c`
- Hardware required: CC1101 module, antenna, 433/868/915 MHz remote or second CC1101
- Firmware build: `bus_pirate6`
- Tester:
- Date:
- Overall result: PASS / FAIL / BLOCKED

## 1. Preconditions

- [ ] Firmware builds: `cmake --build build_rp2350 --parallel --target bus_pirate6`
- [ ] Host test passes (if the feature has one): ``tests/test_subghz.sh``
- [ ] Required hardware is powered from VOUT at the correct voltage
- [ ] Wiring matches the mode help
- [ ] `health` shows no prior fault/overflow
- [ ] Terminal/debug UART is not using the feature pins

## 2. Command checks


### `setfrequency`

- Input: `setfrequency`
- Expected: Command completes with a meaningful result; see help for arguments
- Actual:
- Evidence:
- Pass: [ ]

### `rssi`

- Input: `rssi`
- Expected: Command completes with a meaningful result; see help for arguments
- Actual:
- Evidence:
- Pass: [ ]

### `preset`

- Input: `preset`
- Expected: Command completes with a meaningful result; see help for arguments
- Actual:
- Evidence:
- Pass: [ ]

### `protocol`

- Input: `protocol`
- Expected: Command completes with a meaningful result; see help for arguments
- Actual:
- Evidence:
- Pass: [ ]

### `info`

- Input: `info`
- Expected: Command completes with a meaningful result; see help for arguments
- Actual:
- Evidence:
- Pass: [ ]

### `scan`

- Input: `scan`
- Expected: Command completes with a meaningful result; see help for arguments
- Actual:
- Evidence:
- Pass: [ ]

### `sweep`

- Input: `sweep`
- Expected: Command completes with a meaningful result; see help for arguments
- Actual:
- Evidence:
- Pass: [ ]

### `trace`

- Input: `trace`
- Expected: Command completes with a meaningful result; see help for arguments
- Actual:
- Evidence:
- Pass: [ ]

### `waterfall`

- Input: `waterfall`
- Expected: Command completes with a meaningful result; see help for arguments
- Actual:
- Evidence:
- Pass: [ ]

### `receive`

- Input: `receive`
- Expected: Command completes with a meaningful result; see help for arguments
- Actual:
- Evidence:
- Pass: [ ]

### `hop`

- Input: `hop`
- Expected: Command completes with a meaningful result; see help for arguments
- Actual:
- Evidence:
- Pass: [ ]

### `replay`

- Input: `replay`
- Expected: Command completes with a meaningful result; see help for arguments
- Actual:
- Evidence:
- Pass: [ ]

### `record`

- Input: `record`
- Expected: Command completes with a meaningful result; see help for arguments
- Actual:
- Evidence:
- Pass: [ ]

### `load`

- Input: `load`
- Expected: Command completes with a meaningful result; see help for arguments
- Actual:
- Evidence:
- Pass: [ ]

### `send`

- Input: `send`
- Expected: Command completes with a meaningful result; see help for arguments
- Actual:
- Evidence:
- Pass: [ ]

### `decode`

- Input: `decode`
- Expected: Command completes with a meaningful result; see help for arguments
- Actual:
- Evidence:
- Pass: [ ]

### `clone`

- Input: `clone`
- Expected: Command completes with a meaningful result; see help for arguments
- Actual:
- Evidence:
- Pass: [ ]

### `savekey`

- Input: `savekey`
- Expected: Command completes with a meaningful result; see help for arguments
- Actual:
- Evidence:
- Pass: [ ]

### `monitor`

- Input: `monitor`
- Expected: Command completes with a meaningful result; see help for arguments
- Actual:
- Evidence:
- Pass: [ ]

### `keyload`

- Input: `keyload`
- Expected: Command completes with a meaningful result; see help for arguments
- Actual:
- Evidence:
- Pass: [ ]

### `keys`

- Input: `keys`
- Expected: Command completes with a meaningful result; see help for arguments
- Actual:
- Evidence:
- Pass: [ ]

### `keyadd`

- Input: `keyadd`
- Expected: Command completes with a meaningful result; see help for arguments
- Actual:
- Evidence:
- Pass: [ ]

### `txallow`

- Input: `txallow`
- Expected: Command completes with a meaningful result; see help for arguments
- Actual:
- Evidence:
- Pass: [ ]

### `power`

- Input: `power`
- Expected: Command completes with a meaningful result; see help for arguments
- Actual:
- Evidence:
- Pass: [ ]

### `bitrate`

- Input: `bitrate`
- Expected: Command completes with a meaningful result; see help for arguments
- Actual:
- Evidence:
- Pass: [ ]

### `scanprofile`

- Input: `scanprofile`
- Expected: Command completes with a meaningful result; see help for arguments
- Actual:
- Evidence:
- Pass: [ ]

### `jam`

- Input: `jam`
- Expected: Command completes with a meaningful result; see help for arguments
- Actual:
- Evidence:
- Pass: [ ]

### `bruteforce`

- Input: `bruteforce`
- Expected: Command completes with a meaningful result; see help for arguments
- Actual:
- Evidence:
- Pass: [ ]

### `burst`

- Input: `burst`
- Expected: Command completes with a meaningful result; see help for arguments
- Actual:
- Evidence:
- Pass: [ ]

### `ear`

- Input: `ear`
- Expected: Command completes with a meaningful result; see help for arguments
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
