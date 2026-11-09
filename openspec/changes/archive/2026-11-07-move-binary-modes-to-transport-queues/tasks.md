# Tasks: Move Binary Modes to Transport Queues

## 1. Binary Transport Service

- [x] 1.1 Implement `src/services/bp_bin_transport.h` with packet and byte queues, non-blocking APIs, timeout reads, and telemetry counters (ARCH-122).
- [x] 1.2 Implement `src/services/bp_bin_transport.c` with static SPSC rings, CDC1 bridge, backpressure drop policy, and watchdog integration (ARCH-122, ARCH-128).
- [x] 1.3 Register `bp_bin_transport_service()` in `bp_service(0)` inside `src/kernel/bp_kernel.c`.
- [x] 1.4 Adapt `src/usb_tx.c` and `src/usb_rx.c` legacy binary FIFO functions to delegate to `bp_bin_transport`.

## 2. Binary Engine Conversion

- [x] 2.1 Convert `src/binmode/sump.c` to use `bp_bin_transport` for metadata, sample streaming, and command processing; remove direct `tud_cdc_n_*` (ARCH-123, ARCH-124).
- [x] 2.2 Convert `src/binmode/bpio.c` to use `bp_bin_transport` for COBS packet output and FlatBuffer command input; remove direct `tud_cdc_n_*` (ARCH-123, ARCH-125).
- [x] 2.3 Convert `src/binmode/falaio.c` to use `bp_bin_transport` for notification packets and sample dumps; remove direct `tud_cdc_n_*` (ARCH-123, ARCH-126).
- [x] 2.4 Convert `src/binmode/legacy4third.c` to use `bp_bin_transport` (ARCH-127).
- [x] 2.5 Convert `src/binmode/irtoy-air.c` and `src/binmode/irtoy-irman.c` to use `bp_bin_transport` (ARCH-127).
- [x] 2.6 Convert `src/lib/arduino-ch32v003-swio/arduino_ch32v003.c` to use `bp_bin_transport` with bounded timeouts (ARCH-127).
- [x] 2.7 Clean up `src/binmode/binmodes.c` and verify zero remaining `tud_cdc_n_*` calls across `src/binmode/` (ARCH-123).

## 3. Telemetry, Diagnostics & Sizing

- [x] 3.1 Update `src/commands/global/cmd_binmode.c` with `-s` / `--status` flag to display binary transport queue levels, drops, and connection state (ARCH-128).
- [x] 3.2 Update `src/commands/global/health.c` to include binary transport queue metrics in `health` output.
- [x] 3.3 Verify USB-FS throughput budget and sizing (ARCH-129).

## 4. Host Verification & Target Build

- [x] 4.1 Create host unit test suite `tests/test_bin_transport.c` and runner `tests/test_bin_transport.sh` validating queues, FIFO order, drop policy, and timeouts under ASan/UBSan.
- [x] 4.2 Execute all host test suites (`tests/*.sh`) and verify 100% pass rate.
- [x] 4.3 Verify static memory model guard (`tests/test_memory_model.sh`) passes with zero heap usage and static RAM <= 380 KB.
- [x] 4.4 Build RP2350 target firmware binary (`build_rp2350/src/bus_pirate6.uf2`) and record flash/RAM metrics.
