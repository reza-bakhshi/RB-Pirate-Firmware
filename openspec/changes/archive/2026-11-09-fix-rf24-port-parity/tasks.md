# Tasks

## 1. Hardware Lease Arbitration & Teardown

- [x] 1.1 In `src/pirate/nrf24l01.c`, acquire `BP_RES_PERIPH_SPI1` lease in `nrf24_init()`, release in `nrf24_deinit()`, and restore `nrf_ce_pin` to `GPIO_FUNC_SIO` input. Verify via host test stub.
- [x] 1.2 In `src/mode/rf24.c`, update `command_ready()` and `rf24_cleanup()` to release leases immediately on module absence or exit. Verify clean exit and compilation.

## 2. Cancellation Hardening & WiFi-Sweep Honesty

- [x] 2.1 Insert `bp_job_check_cancelled()` checks in `channel_hits()` and `waterfall_handler()` inner channel loop. Verify non-blocking abort < 100 ms.
- [x] 2.2 Clarify WiFi continuous carrier sweep limitations in `rf24_help()` and `jam_handler()` documentation. Verify terminal help text matches OpenSpec requirement.

## 3. Host Unit Tests & Sanitizer Validation

- [x] 3.1 Implement `tests/test_rf24.c` covering framing, register RW, presence detection, data rate setup, dynamic payload, RPD sampling, and resource arbitration.
- [x] 3.2 Implement `tests/test_rf24.sh` test runner and verify all tests pass under ASan and UBSan.

## 4. Target Build & Full Regression

- [x] 4.1 Build RP2350 target firmware (`make -C build_rp2350 -j$(nproc)`) and verify RAM <= 380 KB and 4 KB fixed stacks.
- [x] 4.2 Run full regression test suite across all `tests/test_*.sh` and `tests/test_memory_model.sh`.

## Workflow follow-up

- Archive `fix-rf24-port-parity` with `openspec archive fix-rf24-port-parity -y`.
- Update `openspec/ROADMAP.md` marking F6 complete.
