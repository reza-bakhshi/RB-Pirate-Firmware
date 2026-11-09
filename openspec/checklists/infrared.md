# INFRARED Acceptance Checklist

- Source: ESP32-Bit-Pirate `8f42d9f40fc86f97b6b1247d039a3cd899c5d442` + MakeHex
- Local implementation: `src/mode/infrared.c`, `src/pirate/ir_protocols.c`, `src/pirate/irio_pio.c`, `src/commands/infrared/*`
- Hardware required: IR receiver (38 kHz demodulator or barrier), IR LED, IR remote
- Firmware build: `bus_pirate6`
- Tester:
- Date:
- Overall result: PASS / FAIL / BLOCKED

## 1. Preconditions

- [ ] Firmware builds: `cmake --build build_rp2350 --parallel --target bus_pirate6`
- [ ] Host test passes (if the feature has one): ``tests/test_ir_protocols.sh``
- [ ] Required hardware is powered from VOUT at the correct voltage
- [ ] Wiring matches the mode help
- [ ] `health` shows no prior fault/overflow
- [ ] Terminal/debug UART is not using the feature pins

## 2. Command checks


### `irtoy_test`

- Input: `irtoy_test`
- Expected: Test frame is transmitted/received; no hang
- Actual:
- Evidence:
- Pass: [ ]

### `tvbgone`

- Input: `tvbgone`
- Expected: TV-B-Gone sequence starts and completes/cancels
- Actual:
- Evidence:
- Pass: [ ]

### `irtx`

- Input: `irtx`
- Expected: Raw IR frame is transmitted on the configured TX pin
- Actual:
- Evidence:
- Pass: [ ]

### `irrx`

- Input: `irrx`
- Expected: Raw IR frame is captured and displayed
- Actual:
- Evidence:
- Pass: [ ]

### `send`

- Input: `send nec 0x04 0x00 0x08`
- Expected: NEC frame is transmitted; a receiver decodes it
- Actual:
- Evidence:
- Pass: [ ]

### `receive`

- Input: `receive`
- Expected: Decodes a real remote; prints protocol/device/function
- Actual:
- Evidence:
- Pass: [ ]

### `setprotocol`

- Input: `setprotocol nec`
- Expected: Engine protocol changes and persists
- Actual:
- Evidence:
- Pass: [ ]

### `devicebgone`

- Input: `devicebgone`
- Expected: Device-B-Gone sequence runs and cancels
- Actual:
- Evidence:
- Pass: [ ]

### `remote`

- Input: `remote`
- Expected: Built-in remote sends the selected key
- Actual:
- Evidence:
- Pass: [ ]

### `record`

- Input: `record test.ir`
- Expected: Records a real remote to `.ir`
- Actual:
- Evidence:
- Pass: [ ]

### `load`

- Input: `load test.ir`
- Expected: Loads and lists `.ir` signals
- Actual:
- Evidence:
- Pass: [ ]

### `replay`

- Input: `replay test.ir`
- Expected: Replays the loaded signal
- Actual:
- Evidence:
- Pass: [ ]

### `jam`

- Input: `jam`
- Expected: IR jam starts and any key stops it
- Actual:
- Evidence:
- Pass: [ ]

### `config`

- Input: `config`
- Expected: Shows TX/RX pins, protocol and carrier
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
