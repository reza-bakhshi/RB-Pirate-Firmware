# USB & Binary Communication

> Developer guide to USB interfaces, non-blocking queueing, and cross-core binary streaming in RB-Pirate firmware.

---

## 1. TinyUSB Interfaces & Core Ownership

All USB endpoints are managed exclusively by **Core 0 (Control Plane)** to eliminate cross-core interrupt races and ensure deterministic USB enumeration:

| Interface | CDC Channel | Core Owner | Purpose |
|-----------|-------------|------------|---------|
| **Terminal** | CDC 0 | Core 0 | Interactive user shell, VT100 prompt, syntax execution, toolbar updates |
| **Binary Mode** | CDC 1 | Core 0 (I/O) / Core 1 (Data) | Programmatic binary protocol, SUMP logic analyzer, BPIO, high-speed streaming |
| **Mass Storage** | MSC | Core 0 | USB MSC access to onboard SPI flash or TF card via FatFS |

---

## 2. Terminal Subsystem (CDC 0)

Interactive terminal communication is governed by `src/services/bp_terminal.c`:
- **Lock-Free Ring Buffers**: Terminal input and output flow through power-of-two circular buffers (`bp_terminal_tx` and `rx_fifo`).
- **Backpressure & Non-Blocking Writes**: If the USB CDC FIFO or UART output buffer is full, output functions (`bp_terminal_write`, `bp_terminal_putchar`) apply bounded retries and count backpressure stalls without blocking the cooperative scheduler.
- **Linenoise CLI**: Terminal input provides real-time history, auto-completion (`bp_cmd`), dynamic hints, syntax highlighting, and non-blocking sub-prompt evaluation.

---

## 3. Binary Transport Architecture (CDC 1)

Binary mode decouples host packet transport from protocol execution using typed lock-free SPSC queues (`src/services/bp_bin_transport.c`):

```
+-------------------+       +-----------------------+       +-------------------+
| Host Application  | <---> | TinyUSB CDC 1 (Core 0)| <---> | SPSC Ring Queues  |
+-------------------+       +-----------------------+       +-------------------+
                                                                     |
                                                                     v
                                                            +-------------------+
                                                            | Core 1 Protocol   |
                                                            | Engines & Decoders|
                                                            +-------------------+
```

### Transport Queues:
- `bin_rx_fifo`: Core 0 receives raw USB CDC 1 packets from the host and enqueues them into the SPSC buffer. Core 1 dequeues packets and feeds them to the active binary engine (SUMP, BPIO, or mode-specific binary parser).
- `bin_tx_fifo`: Core 1 streams sampled data, packet decodes, or logic analyzer frames into the SPSC buffer. Core 0 services the buffer and transmits it over USB CDC 1 without blocking Core 1 protocol loops.

---

## 4. Health Telemetry & Diagnostic Counters

USB and binary queues maintain monotonic diagnostic counters accessible via the `health` CLI command:
- `terminal_tx_bytes` / `terminal_rx_bytes`: Volume of interactive terminal traffic.
- `terminal_tx_stalls`: Count of flow-control backpressure events.
- `bin_tx_bytes` / `bin_rx_bytes`: Volume of binary protocol traffic.
- `bin_tx_drops`: Frames discarded due to unserviced queue overflow.
