# Design: Move Binary Modes to Transport Queues

## 1. Overview and Architecture

The Bus Pirate supports two USB CDC communication interfaces:
- **CDC 0**: Interactive VT100 terminal, CLI command processor, and system telemetry (owned by `bp_terminal` on Core 0).
- **CDC 1**: Binary protocol transport (SUMP logic analyzer, BPIO FlatBuffers, legacy DirtyProto, FALAIO, IRToy, Arduino CH32V003 programmer).

Previously, binary modes implemented in `src/binmode/` called TinyUSB CDC APIs (`tud_cdc_n_*`) directly from Core 0 while Core 1 ran `tud_task()` and drained USB endpoints. Because TinyUSB is unshielded by mutex locks (`CFG_TUSB_OS = OPT_OS_NONE`), this concurrent access caused USB stack corruption and lockups. Additionally, legacy functions in `src/usb_tx.c` like `bin_tx_fifo_put()` used unbounded blocking loops (`spsc_queue_add_blocking()`), freezing Core 0 whenever CDC 1 was not open.

This change introduces `bp_bin_transport`, a dedicated, non-blocking binary transport subsystem. It isolates all protocol engines behind lock-free Single-Producer Single-Consumer (SPSC) transport rings with deterministic backpressure drop accounting.

```
+-------------------------------------------------------------------------------+
|                                    CORE 0                                     |
|                                                                               |
|   +-----------------------+                    +--------------------------+   |
|   |   bp_service(0) Loop  |                    |      CLI / Commands      |   |
|   +-----------+-----------+                    +--------------------------+   |
|               |                                                               |
|               v                                                               |
|   +-----------------------+                    +--------------------------+   |
|   |  bp_bin_transport_    | <----------------- |  cmd_binmode -s / health |   |
|   |  service()            |                    |  (Telemetry Display)     |   |
|   +-----------+-----------+                    +--------------------------+   |
|               |                                                               |
+---------------|---------------------------------------------------------------+
                |
  (Lock-free SPSC Queues)
  - TX Ring: 4096 bytes (Core 1 Producer -> Core 0 Consumer)
  - RX Ring: 2048 bytes (Core 0 Producer -> Core 1 Consumer)
                |
+---------------|---------------------------------------------------------------+
|               v                    CORE 1                                     |
|   +-----------------------------------------------------------------------+   |
|   |                         bp_bin_transport API                          |   |
|   |  - bp_bin_transport_read()         - bp_bin_transport_write()         |   |
|   |  - bp_bin_transport_getc()         - bp_bin_transport_putc()          |   |
|   |  - bp_bin_transport_getc_timeout() - bp_bin_transport_tx_flush()     |   |
|   +-----------------------------------+-----------------------------------+   |
|                                       |                                       |
|         +-----------------------------+-----------------------------+         |
|         |              |              |              |              |         |
|         v              v              v              v              v         |
|     +-------+      +-------+      +--------+     +-------+     +----------+   |
|     | SUMP  |      | BPIO  |      | FALAIO |     | legacy|     | IRToy /  |   |
|     | Logic |      | COBS  |      | Logic  |     | 4third|     | CH32V003 |   |
|     +-------+      +-------+      +--------+     +-------+     +----------+   |
+-------------------------------------------------------------------------------+
```

---

## 2. Queue Sizing and Memory Model

In accordance with the project's static memory model (`memory-model` capability):
- Zero dynamic allocation (`malloc`, `free`, `new`, `delete`).
- Statically allocated SPSC queue buffers:
  - `s_bin_tx_buf`: 4096 bytes (power of 2, absorbs full USB packet bursts up to 64 packets of 64 bytes).
  - `s_bin_rx_buf`: 2048 bytes (power of 2, buffers host commands without stalling USB reception).
- Both queues reside in standard SRAM data memory, well within the RP2350's <= 380 KB static RAM budget.

---

## 3. Backpressure and Disconnect Policy

If CDC 1 is closed or the host stops reading:
1. `bp_bin_transport_write()` writes as many bytes as will fit into the free space of `s_bin_tx_q`.
2. Any unwritten remainder is immediately dropped (no blocking spin-loop).
3. The count of dropped bytes is atomically accumulated in `stats.tx_dropped`.
4. `bp_bin_transport_putc()` returns `BP_STATUS_QUEUE_FULL` on overflow and increments `stats.tx_dropped`.
5. `bp_bin_transport_putc_timeout()` waits up to `timeout_ms` checking for queue space while feeding the watchdog supervisor. If space does not become available before the timeout expires, it returns `BP_STATUS_TIMEOUT`.
6. `bp_bin_transport_is_connected()` queries the CDC 1 connection state safely.

---

## 4. Binary Mode Conversions

### 4.1 SUMP Logic Analyzer (`src/binmode/sump.c`)
- Replace direct `tud_cdc_n_write()` and `tud_cdc_n_write_str()` with `bp_bin_transport_write()`.
- Replace `tud_cdc_n_write_available() >= sizeof(buf)` with `bp_bin_transport_tx_free() >= sizeof(buf)`.
- Replace `tud_cdc_n_available()` and `tud_cdc_n_read()` with `bp_bin_transport_read()`.
- Replace `tud_cdc_n_connected(1)` with `bp_bin_transport_is_connected()`.

### 4.2 BPIO Protocol (`src/binmode/bpio.c`)
- In `bpio_send_packet()`: replace chunked `tud_cdc_n_write()` while loop with a single non-blocking `bp_bin_transport_write()` followed by `bp_bin_transport_tx_flush()`.
- In `dirtyproto_mode()`: replace `tud_cdc_n_available()` / `tud_cdc_n_read()` with `bp_bin_transport_read()`.
- Replace `tud_cdc_n_connected(CDC_INTF)` with `bp_bin_transport_is_connected()`.

### 4.3 FALAIO (`src/binmode/falaio.c`)
- Replace notification packet transmission (`$FALADATA;...`) and `falaio_tx8()` dumps with `bp_bin_transport_write()` and `bp_bin_transport_tx_flush()`.
- Replace polling on `tud_cdc_n_available()` with `bp_bin_transport_rx_available()`.

### 4.4 Legacy / IRToy / CH32V003
- Replace `bin_tx_fifo_put()` calls with `bp_bin_transport_putc()`.
- Replace `bin_rx_fifo_try_get()` with `bp_bin_transport_getc()`.
- Replace `bin_rx_fifo_get_blocking()` with `bp_bin_transport_getc_timeout()`, eliminating indefinite hang risks.

---

## 5. Verification Strategy

1. **Host Unit Tests (`tests/test_bin_transport.c`, `tests/test_bin_transport.sh`)**:
   - Initialize transport service queues.
   - Verify non-blocking packet write and bounded byte reception.
   - Verify SPSC FIFO ordering and multi-chunk framing.
   - Verify backpressure drop accounting when TX ring fills without active drain.
   - Verify timeout expiration and watchdog servicing during stalled receive.
   - Execute under AddressSanitizer and UndefinedBehaviorSanitizer.
2. **Static Memory & Grep Verification**:
   - Verify zero occurrences of `tud_cdc_n_*` in `src/binmode/`.
   - Verify static memory remains <= 380 KB on RP2350 target.
3. **Hardware Smoke Verification**:
   - Verify `binmode -s` and `health` commands on physical RP2350 target.
