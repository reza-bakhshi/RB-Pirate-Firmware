# Proposal: Move Binary Modes to Transport Queues

## Why

Binary modes on Bus Pirate (SUMP logic analyzer, BPIO FlatBuffers, legacy DirtyProto, FALAIO, IRToy, and Arduino CH32V003 programmer) currently perform raw, uncoordinated TinyUSB CDC1 calls (`tud_cdc_n_*`) directly from Core 0 main loops and service handlers while TinyUSB device tasks (`tud_task`) and CDC RX tasks execute concurrently on Core 1. Because TinyUSB is configured without an RTOS mutex (`CFG_TUSB_OS = OPT_OS_NONE`), this concurrent access causes race conditions, corrupted endpoint states, missed packets, and hard device freezes.

Furthermore, several binary modes spin in blocking loops waiting for CDC1 buffer space or connection (`spsc_queue_add_blocking`, `while (!tud_cdc_n_write_available(1))`). When CDC1 is not opened by a host application, these loops permanently hang Core 0, locking the terminal interface and tripping watchdog timeouts.

Step B6 moves all binary mode communication behind a unified, non-blocking binary transport service (`bp_bin_transport`). CDC1 hardware I/O is owned and driven through dedicated lock-free SPSC transport rings. Binary protocol engines running on either core interact strictly through non-blocking and bounded-timeout transport APIs with comprehensive drop counters and backpressure protection.

## What Changes

- **Binary Transport Subsystem (`src/services/bp_bin_transport.h`, `src/services/bp_bin_transport.c`)**:
  - Implement a lock-free SPSC packet and byte transport service bridging USB CDC1 with protocol engines.
  - SPSC TX ring buffer (4096 bytes) and RX ring buffer (2048 bytes) statically allocated.
  - Non-blocking and bounded-wait send/receive APIs (`bp_bin_transport_read`, `bp_bin_transport_write`, `bp_bin_transport_getc`, `bp_bin_transport_putc`, `bp_bin_transport_getc_timeout`).
  - Strict backpressure and disconnect policy: if CDC1 is disconnected or the host stops reading, output writes drop excess bytes without blocking and increment `tx_dropped`.
  - Comprehensive telemetry: RX/TX buffer levels, free space, drop counters, packet counts, and connection state.
- **Binary Engine Migration**:
  - Eliminate all direct `tud_cdc_n_*` and `tud_*` calls across `src/binmode/` (`sump.c`, `bpio.c`, `falaio.c`, `irtoy-air.c`, `irtoy-irman.c`, `binmodes.c`) and `src/lib/arduino-ch32v003-swio/`.
  - Migrate SUMP logic analyzer to queue-based framing and non-blocking transmit chunks.
  - Migrate BPIO (DirtyProto / FlatBuffers) to framed COBS packet queue transmission.
  - Migrate FALAIO logic analyzer to queue-based notification and sample dumping.
  - Migrate legacy4third, IRToy (AIR/IRMAN), and Arduino CH32V003 to non-blocking and bounded-timeout transport calls.
- **Service Kernel Integration**:
  - Register `bp_bin_transport_service()` into the cooperative kernel dispatch (`bp_service(0)`).
  - Bridge CDC1 hardware drainage and reception to the transport queues.
- **Diagnostics and Health Reporting**:
  - Update `binmode` command (`binmode -s` / `binmode --status`) and `health` command to report binary transport telemetry and drop statistics.
- **Automated Verification**:
  - Host test suite (`tests/test_bin_transport.c`, `tests/test_bin_transport.sh`) verifying packet queue framing, FIFO ordering, backpressure drop policy, bounded timeouts, and disconnect resilience under AddressSanitizer and UndefinedBehaviorSanitizer.

## Capabilities

### New Capabilities
<!-- None -->

### Modified Capabilities
- `binary-modes`: Establish non-blocking transport queue ownership, framed packet rings, CDC1 backpressure safety, and complete elimination of direct TinyUSB calls from binary mode engines.

## Impact

- **Affected Code**:
  - `src/services/bp_bin_transport.h`, `src/services/bp_bin_transport.c`: New binary transport service.
  - `src/binmode/sump.c`: Convert to `bp_bin_transport`.
  - `src/binmode/bpio.c`: Convert to `bp_bin_transport`.
  - `src/binmode/falaio.c`: Convert to `bp_bin_transport`.
  - `src/binmode/irtoy-air.c`, `src/binmode/irtoy-irman.c`: Convert to `bp_bin_transport`.
  - `src/binmode/legacy4third.c`: Convert to `bp_bin_transport`.
  - `src/binmode/binmodes.c`: Remove direct `tud_cdc_n_*` calls.
  - `src/lib/arduino-ch32v003-swio/arduino_ch32v003.c`: Convert to `bp_bin_transport`.
  - `src/usb_tx.c`, `src/usb_rx.c`: Adapt legacy `bin_*_fifo` calls to delegate to `bp_bin_transport`.
  - `src/kernel/bp_kernel.c`: Hook `bp_bin_transport_service()` into service loop.
  - `src/commands/global/cmd_binmode.c`, `src/commands/global/health.c`: Add transport status reporting.
- **Memory & Resource Budget**:
  - Zero dynamic heap allocation (`malloc`, `free`, `new`, `delete`).
  - Total static RAM remains strictly <= 380 KB on RP2350.
