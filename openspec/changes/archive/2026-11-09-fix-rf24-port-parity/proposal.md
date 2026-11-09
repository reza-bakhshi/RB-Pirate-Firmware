# Change: Fix RF24 Port Parity and Hardware Arbitration (F6)

## Why

The Bus Pirate RF24 mode supports nRF24L01/nRF24L01+ 2.4 GHz transceivers over SPI1 with configurable CE pin (BIO0-BIO3). To achieve full feature parity, robust crash recovery, and kernel resource safety on RP2350 (Core 0 CTRL / Core 1 PROTO), RF24 must incorporate hardware lease arbitration for SPI1, enforce clean CE and SPI pin teardown, guarantee non-blocking cooperative cancellation in all channel/sample loops (< 100 ms), provide honest carrier-sweep documentation for WiFi-band jamming, and add dedicated host test coverage.

## What Changes

- **Peripheral Lease Arbitration & Pin Lifecycle (`src/pirate/nrf24l01.c`, `src/mode/rf24.c`)**:
  - In `nrf24_init()`, acquire peripheral lease `BP_RES_PERIPH_SPI1` via `bp_res_acquire(BP_RES_CLASS_PERIPH, BP_RES_PERIPH_SPI1, BP_RES_OWNER_MODE_BASE, "rf24")`. Reject initialization if contested.
  - In `nrf24_deinit()`, release `BP_RES_PERIPH_SPI1` via `bp_res_release()`, configure the CE pin to input high-Z with `bio_input(nrf_ce_pin)` and `bio_set_function(nrf_ce_pin, GPIO_FUNC_SIO)`, and call `hwspi_deinit()` to release SPI pins to safe GPIO defaults.
  - In `rf24_cleanup()`, ensure full teardown of the radio, release of all mode pin labels, and restoration of all GPIO functions.
- **Absent Module & Error Handling (`src/mode/rf24.c`, `src/pirate/nrf24l01.c`)**:
  - If nRF24L01 is not detected or ceases responding during operation, release the SPI1 lease and CE pin immediately and output a clear error message with wiring guidelines without hanging.
- **Cooperative Cancellation Hardening (`src/mode/rf24.c`)**:
  - Add `bp_job_check_cancelled()` checks inside `channel_hits()` and the inner channel loops of `waterfall_handler()` to ensure long dwell or high sample counts cancel cooperatively in < 100 ms.
- **WiFi Carrier-Sweep Documentation Honesty (`src/mode/rf24.c`, `src/pirate/nrf24l01.c`)**:
  - Clarify in command usage and execution output that `jam -g wifi` performs an nRF24 continuous carrier sweep across WiFi-overlapping channels and does not inject 802.11 beacon/data frames, which is impossible on RP2350 hardware.
- **Host Unit Test Suite (`tests/test_rf24.c`, `tests/test_rf24.sh`)**:
  - Create host tests verifying nRF24L01 register manipulation, command encoding, RPD energy detection logic, payload width checks, address scan framing, and kernel resource arbitration under ASan and UBSan.
- **Regression and Memory Model Validation**:
  - Verify static memory consumption stays within the 380 KB budget with 4 KB fixed stacks, and run all 22 host test scripts.

## Core & Resource Ownership

- **Core**: Core 0 (CTRL) executes the command handlers and mode lifecycle; Core 1 (PROTO) owns the protocol engines.
- **Resources Owned**: `BP_RES_PERIPH_SPI1` peripheral lease, CE GPIO pin (BIO0..BIO3), and SPI1 pins (MISO/BIO4, CSN/BIO5, SCK/BIO6, MOSI/BIO7).

## Out of Scope

- 802.11 Wi-Fi frames or beacon injection (hardware cannot generate 802.11 modulation).
- Bluetooth classic baseband demodulation (nRF24 only demodulates GFSK packets conforming to its own preamble/sync format).
- Cross-core protocol dispatching changes (already handled in F2).

## Capabilities

### New Capabilities
None.

### Modified Capabilities
- `rf24`: Require SPI1 peripheral lease arbitration, clean CE and SPI pin teardown, responsive cancellation (< 100 ms) in all sample loops, absent-module safety, and honest WiFi-band carrier sweep documentation.

## Impact

- Safe SPI1 and CE pin lease management preventing peripheral conflicts.
- Responsive user interaction and job cancellation during long sweeps/scans.
- Host test coverage for all RF24 driver framing and register logic.
