# RFID / PN532 HSU Acceptance Checklist

- Source: ESP32-Bit-Pirate `8f42d9f40fc86f97b6b1247d039a3cd899c5d442` + NXP PN532
- Local implementation: `src/mode/rfid.c`, `src/pirate/pn532.c`
- Hardware required: PN532 HSU module, MIFARE Classic tag, NTAG/Ultralight tag, magic UID card
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


### `scan`

- Input: `scan`
- Expected: Command completes; unsupported tag types produce an explicit message, not a hang
- Actual:
- Evidence:
- Pass: [ ]

### `transceive`

- Input: `transceive`
- Expected: Command completes; unsupported tag types produce an explicit message, not a hang
- Actual:
- Evidence:
- Pass: [ ]

### `jewel`

- Input: `jewel`
- Expected: Command completes; unsupported tag types produce an explicit message, not a hang
- Actual:
- Evidence:
- Pass: [ ]

### `forum3`

- Input: `forum3`
- Expected: Command completes; unsupported tag types produce an explicit message, not a hang
- Actual:
- Evidence:
- Pass: [ ]

### `forum4`

- Input: `forum4`
- Expected: Command completes; unsupported tag types produce an explicit message, not a hang
- Actual:
- Evidence:
- Pass: [ ]

### `read`

- Input: `read`
- Expected: Command completes; unsupported tag types produce an explicit message, not a hang
- Actual:
- Evidence:
- Pass: [ ]

### `dump`

- Input: `dump`
- Expected: Command completes; unsupported tag types produce an explicit message, not a hang
- Actual:
- Evidence:
- Pass: [ ]

### `restore`

- Input: `restore`
- Expected: Command completes; unsupported tag types produce an explicit message, not a hang
- Actual:
- Evidence:
- Pass: [ ]

### `write`

- Input: `write`
- Expected: Command completes; unsupported tag types produce an explicit message, not a hang
- Actual:
- Evidence:
- Pass: [ ]

### `clone`

- Input: `clone`
- Expected: Command completes; unsupported tag types produce an explicit message, not a hang
- Actual:
- Evidence:
- Pass: [ ]

### `erase`

- Input: `erase`
- Expected: Command completes; unsupported tag types produce an explicit message, not a hang
- Actual:
- Evidence:
- Pass: [ ]

### `ndef`

- Input: `ndef`
- Expected: Command completes; unsupported tag types produce an explicit message, not a hang
- Actual:
- Evidence:
- Pass: [ ]

### `pwauth`

- Input: `pwauth`
- Expected: Command completes; unsupported tag types produce an explicit message, not a hang
- Actual:
- Evidence:
- Pass: [ ]

### `scanner`

- Input: `scanner`
- Expected: Command completes; unsupported tag types produce an explicit message, not a hang
- Actual:
- Evidence:
- Pass: [ ]

### `nonce`

- Input: `nonce`
- Expected: Command completes; unsupported tag types produce an explicit message, not a hang
- Actual:
- Evidence:
- Pass: [ ]

### `info`

- Input: `info`
- Expected: Command completes; unsupported tag types produce an explicit message, not a hang
- Actual:
- Evidence:
- Pass: [ ]

### `tools`

- Input: `tools`
- Expected: Command completes; unsupported tag types produce an explicit message, not a hang
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
