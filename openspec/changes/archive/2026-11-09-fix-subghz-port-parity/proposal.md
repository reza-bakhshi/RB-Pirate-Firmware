# Change: Fix SubGHz Port Parity and Protocol Conformance (F4)

## Why

The RB-Pirate SubGHz subsystem uses a TI CC1101 transceiver over SPI with asynchronous pulse capture and generation on RP2350/RP2040. In Level 3 (C1), RF/IR commands were converted to asynchronous, cooperative jobs on Core 1 (`bp_job_dispatch()`). However, feature parity with the upstream Flipper Zero SubGHz implementation requires verifying and hardening:
1. CC1101 frequency bands (300-348 MHz, 387-464 MHz, 779-928 MHz) and preset definitions (`Ook270Async`, `Ook650Async`, `2FSKDev238Async`, `2FSKDev12KAsync`, `2FSKDev476Async`, `GFSK10K`, `MSK100K`, `Custom`), including Flipper alias handling.
2. Edge capture interrupt safety in `subghz_cap.c`: ensuring `gpio_acknowledge_irq()` is executed upon stopping capture so no stale interrupt latches on GDO0 / BIO pins.
3. Flipper `.sub` file parser and serializer fidelity across RAW, BinRAW, and Key file types, including Custom preset PA tables and alternate register mappings.
4. Conformance of SubGHz encoders and decoders against pinned Flipper Zero constants and test vectors.
5. Strict transmission safety gating (`tx_guard()` enforcing ISM band limits unless overridden by `txallow`).

## What Changes

1. **Hardware Capture Hardening (`src/mode/subghz_cap.c`)**:
   - In `subghz_cap_stop()`, save/disable interrupts, disable the edge IRQ, call `gpio_acknowledge_irq(gpio, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL)` to clear pending flags, and remove the raw interrupt handler cleanly.
2. **Frequency and Preset Validation (`src/pirate/cc1101.c`, `src/pirate/cc1101.h`)**:
   - Export `cc1101_valid_frequency(uint32_t khz)` in `cc1101.h` and use it uniformly across drivers and file parsers.
   - Verify frequency word calculations against the 26 MHz CC1101 crystal formula.
   - Ensure PA power table dBm mappings match canonical Flipper Zero configurations.
3. **Flipper `.sub` Format Parity (`src/mode/subghz_file.c`, `src/mode/subghz.c`)**:
   - Verify parser and serializer for `RAW_Data`, `Bit_RAW`, `Data_RAW`, `Custom_preset_data`, and key fields.
4. **Host Unit & Conformance Testing (`tests/test_subghz.c`, `tests/test_subghz.sh`)**:
   - Add unit tests for frequency word calculation and boundary validation.
   - Add unit tests for preset aliases, OOK/FSK discrimination, and PATABLE power lookup.
   - Add spot checks against pinned Flipper Zero timing constants for key protocols (Princeton, CAME, Nice FLO, Somfy, KeeLoq, etc.).
   - Verify under AddressSanitizer and UndefinedBehaviorSanitizer.
5. **Firmware Target & Regression Validation**:
   - Compile RP2350 firmware `bus_pirate6.uf2` verifying static RAM ceiling $\le 380\text{ KB}$ and 4096-byte fixed stacks.
   - Run all host test suites in `tests/*.sh`.

## Impact

- Zero regressions on existing SubGHz job dispatches and protocol decoders.
- Robust edge interrupt cleanup preventing spurious interrupts after capture stops.
- Full protocol and preset parity with Flipper Zero `.sub` format.
