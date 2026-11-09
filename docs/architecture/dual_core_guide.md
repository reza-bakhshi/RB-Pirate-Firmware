# Dual-Core Architecture

> Developer guide to the RP2350 dual-core architecture in RB-Pirate firmware: Control Plane (Core 0) and Protocol Data Plane (Core 1) separation, inter-core communication via lock-free SPSC queues, seqlock snapshots, and transactional lifecycle management.

---

## 1. Architectural Division of Responsibilities

RB-Pirate strictly decouples the asynchronous user control plane from the real-time protocol execution plane:

```
+------------------------------------------+    +------------------------------------------+
|            CORE 0 (CONTROL PLANE)        |    |          CORE 1 (PROTOCOL ENGINE)        |
+------------------------------------------+    +------------------------------------------+
| - TinyUSB Stack (CDC0, CDC1, MSC)        |    | - Real-time Protocol Engines             |
| - Terminal I/O & Linenoise Editor        |    | - PIO State Machines (PIO0 / PIO1)       |
| - Command Parser & Bytecode Compiler     |    | - Protocol DMA & Hardware IRQs           |
| - Mode Lifecycle Manager (Transactional) |    | - High-Speed Logic Analyzer (FALA)       |
| - Protocol Job Dispatcher & Leases       |    | - Sniffers & Decoders (SPI, I2C, UART)   |
| - Storage Subsystem (FatFS, SD, Flash)   |    | - LCDTAP / IR / RF24 / SubGHz / RFID     |
| - Power Supervision & AMUX Sweeps        |    | - CDC1 Binary Mode Packet Processing     |
| - Atomic Snapshot Publisher (Seqlock)    |    | - Fixed-Size Inter-Core Log Ring         |
| - Watchdog Hardware Supervisor           |    | - Cooperative Worker Slice Execution     |
+------------------------------------------+    +------------------------------------------+
                     |                                               ^
                     |====== Transactional Mode Control FIFO ========|
                     |====== Lock-Free SPSC Binary Queues ===========|
                     |====== Atomic Seqlock UI/Power Snapshots =====>|
                     |<===== Lock-Free Inter-Core Log Ring ==========|
```

### Core Invariants

1. **No TinyUSB on Core 1**: All `tud_*` calls and USB endpoint service loops execute exclusively on Core 0. Core 1 communicates with the host over CDC1 binary mode strictly via typed, lock-free SPSC queues (`bp_bin_transport`).
2. **No FatFS on Core 1**: All `f_*` FatFS filesystem calls execute on Core 0. Protocol engines requiring file streaming (captures, dumps, replay) submit storage RPC packets or lease memory buffers.
3. **No Direct `printf` on Core 1**: Unsynchronized `printf` on Core 1 causes terminal race conditions. Core 1 posts structured log records into a dedicated ring buffer for Core 0 formatting.
4. **No Unbounded Blocking Waits**: Every wait loop has a timeout and returns an error status code.
5. **Memory Stacks**: Each core runs with a dedicated 4 KB stack located in scratch SRAM (`SCRATCH_Y` for Core 0, `SCRATCH_X` for Core 1) with zero runtime dynamic heap allocation.

---

## 2. Inter-Core Communication Mechanisms

### 2.1 Hardware Mailbox (ICM FIFO)
The RP2350 SIO inter-core FIFO is used for high-priority lifecycle signals:
- Mode transition requests (`BP_ICM_SET_MODE`, `BP_ICM_EXIT_MODE`)
- Hardware synchronization and heartbeat notifications
- Terminal toolbar update broadcasts

### 2.2 Lock-Free SPSC Queues
Binary transport between Core 0 USB endpoints and Core 1 protocol engines uses lock-free single-producer single-consumer ring buffers (`bp_spsc_queue.h`). Memory barriers (`__dmb()`) ensure sequential consistency without disabling interrupts or acquiring spinlocks.

### 2.3 Atomic Snapshot Publication (Seqlock)
Power monitor readings (pin voltages, Vreg, Vout, current sense, fuse status) and system configuration updates are published to an atomic snapshot structure (`bp_ui_snapshot_t`). Readers verify the 32-bit sequence counter before and after copying to ensure torn reads are never consumed.

---

## 3. Transactional Mode Lifecycle

Mode changes are transactional:
1. **Core 0 Request**: Core 0 verifies parameters, halts any active protocol jobs, and dispatches an exit message to Core 1.
2. **Core 1 Teardown**: Core 1 safely releases all claimed PIO state machines, DMA channels, and IRQs back to the resource tracker (`bp_res`).
3. **Core 1 Init**: Core 1 initializes the requested mode hardware.
4. **Rollback on Error**: If the new mode fails to initialize, Core 1 rolls back to HiZ mode, ensuring the board never hangs in an intermediate state.
