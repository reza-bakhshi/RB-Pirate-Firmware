# Design: Fix RF24 Port Parity and Hardware Arbitration

## Context

The RF24 mode provides nRF24L01+ transceiver packet communication and passive 2.4 GHz spectrum analysis on RP2350. The driver interfaces via SPI1 (`spi1` on BIO4-BIO7) and a configurable CE pin (BIO0-BIO3). Currently, SPI1 initialization does not acquire or release a kernel peripheral lease, the CE pin is not returned to high-Z input upon deinit, long channel sampling loops can delay cancellation responses beyond 100 ms, and the driver lacks a host unit test suite.

See `proposal.md` for the motivation and `specs/rf24/spec.md` for the behavioral requirements.

## Goals / Non-Goals

**Goals:**
- Implement kernel lease tracking for SPI1 (`BP_RES_PERIPH_SPI1`) with `BP_RES_OWNER_MODE_BASE`.
- Ensure clean pin teardown on deinitialization: CE pin restored to `GPIO_FUNC_SIO` input, SPI pins reset via `hwspi_deinit()`.
- Guarantee non-blocking cooperative cancellation in all RF24 loops (< 100 ms).
- Handle absent or unpowered modules gracefully with clear diagnostic messages without hanging.
- Provide transparent documentation that `jam -g wifi` is a continuous carrier sweep over WiFi-overlapping channels, not 802.11 beacon injection.
- Create host unit tests under ASan and UBSan (`tests/test_rf24.c` / `tests/test_rf24.sh`).

**Non-Goals:**
- Implementing 802.11 Wi-Fi frames or raw beacon generation on RP2350.
- Supporting alternate hardware SPI ports beyond `M_SPI_PORT` (`spi1`).
- Dynamic memory allocations (strictly prohibited by architecture memory policy).

## Decisions

### 1. SPI1 Peripheral Lease Acquisition and Teardown
- **Choice**: In `nrf24_init()`, acquire `BP_RES_PERIPH_SPI1` lease before calling `spi_init()` and `hwspi_init()`. If the peripheral is already leased (e.g., contested by another mode), abort immediately and return false. In `nrf24_deinit()`, release `BP_RES_PERIPH_SPI1` back to the kernel resource registry.
- **Alternative**: Unmanaged SPI1 calls. Rejected because mode transitions or overlapping subsystem access could result in conflicting register state or pinmux corruption.

### 2. CE Pin GPIO Function & Direction Restoration
- **Choice**: When `nrf24_init()` is called, configure `nrf_ce_pin` as output low. When `nrf24_deinit()` is called, drive low, configure direction as input with `bio_input(nrf_ce_pin)`, and set function to `GPIO_FUNC_SIO` with `bio_set_function(nrf_ce_pin, GPIO_FUNC_SIO)`.
- **Alternative**: Leave CE pin configured as an active output. Rejected because leaving driven pins after exiting mode creates potential electrical contention with external signals or subsequent modes.

### 3. Responsive Cooperative Cancellation (< 100 ms)
- **Choice**: Insert `if (bp_job_check_cancelled()) return hits;` checks within `channel_hits()` after every sample, and within `waterfall_handler()`'s inner channel iteration.
- **Alternative**: Check cancellation only at the top-level outer loop. Rejected because high sample counts (e.g. 100) or high dwell times (e.g. 10 ms) would make the device unresponsive for up to 1 second before servicing abort requests.

### 4. Absent-Module Diagnostics & Clean Exit
- **Choice**: In `command_ready()`, if `nrf24_present()` returns false, immediately invoke `nrf24_deinit()`, clear `mode_config.initialized`, and emit a descriptive diagnostic guide highlighting the required connections and power command (`W 3.3`).
- **Alternative**: Hang or retry indefinitely. Rejected by OpenSpec failure handling requirements.

### 5. Honest Carrier-Sweep Documentation
- **Choice**: In `rf24_help()` and `jam_handler()`, explicitly state that the WiFi group sweeps continuous carrier energy across 2.4 GHz channels 1..13/14, and clearly document that the RP2350 lacks an 802.11 MAC/PHY and cannot transmit 802.11 management/beacon frames.
- **Alternative**: Claim Wi-Fi jamming or deauth. Rejected as technically inaccurate on RP2350.

### 6. Host Unit Test Harness
- **Choice**: Develop `tests/test_rf24.c` with a mock SPI register register bank and transfer callback simulator to test:
  - Register write/read commands (`NRF_CMD_W_REGISTER`, `NRF_CMD_R_REGISTER`).
  - Presence detection algorithm (`RF_CH` flip test).
  - Data rate configuration (250 kbps, 1 Mbps, 2 Mbps).
  - Dynamic payload width decoding (`NRF_CMD_R_RX_PL_WID`).
  - Continuous carrier jam setup and frequency sweep calculation.
  - Channel group arrays and boundary constraints.
  - Resource lease acquisition and release under `BP_RES_PERIPH_SPI1`.
- **Alternative**: Manual hardware testing only. Rejected because automated host regression is required for CI and feature parity.

## Risks / Trade-offs

- **[Risk] Contested SPI1 peripheral if previous mode did not release cleanly** → **Mitigation**: All previous mode migrations (F2, F4, F5) strictly release peripheral leases on cleanup; `bp_res_acquire()` returns `BP_ERR_BUSY` if contested and `command_ready()` reports cleanly to the user.
- **[Risk] High dwell times in RPD sampling delaying user response** → **Mitigation**: Cancellation checks inside `channel_hits()` terminate sample loops immediately when a key or job cancellation signal is received.
