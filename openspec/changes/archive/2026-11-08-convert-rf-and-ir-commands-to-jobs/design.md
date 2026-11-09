# Design: Convert RF and IR Commands to Jobs

## 1. Architecture Overview

Under the Level 1 and Level 2 multicore split architecture:
- **Core 0 (Control Plane)**: Owns the terminal, VT100 parser, linenoise shell prompt, configuration, storage/FatFS, ADC/AMUX, PSU, buttons, RGB status, and watchdog supervision. Core 0 must never execute blocking protocol loops.
- **Core 1 (Data Plane)**: Owns hardware protocols, PIO engines, protocol DMA channels, interrupts, and high-speed captures. Core 1 runs the cooperative `bp_service(1)` loop, servicing active protocol jobs via `bp_job_service()`.

Step C1 converts long-running RF, IR, and sniffer capture commands from synchronous blocking loops on Core 0 into asynchronous, sliced jobs dispatched to Core 1.

```
+-------------------------------------------------------------------+
| Core 0: Control Plane / Terminal                                  |
|                                                                   |
| [CLI Command] ---> bp_job_dispatch() ---> Enqueue to Job Pool     |
|       |                                            |              |
|       v                                            v              |
| Non-blocking Terminal / Shell Prompt         Core 1 Data Plane    |
| (Pumps bp_service(0), drains output rings)         |              |
|       ^                                            v              |
|       | <--- Core 1 Text / Progress Ring <--- bp_job_service()    |
|       |                                    (Executes slice_fn)    |
| [User Ctrl+C] ---> bp_job_cancel(active_id) ------> Cancellation  |
+-------------------------------------------------------------------+
```

## 2. Component Design

### 2.1 Job Dispatching and Automatic Teardown (`src/kernel/bp_job.h`, `bp_job.c`)

1. **`bp_job_dispatch()`**:
   ```c
   bp_status_t bp_job_dispatch(const char* name, uint8_t proto, uint32_t timeout_ms,
                               bp_job_slice_fn_t slice_fn, void* ctx,
                               uint32_t* out_job_id);
   ```
   Provides a simplified dispatch interface that automatically computes current timestamp `now_ms` and submits the job descriptor to the runtime without requiring callers to query hardware timers.

2. **Transactional Resource Teardown**:
   In `bp_job_service()`, when a job transitions to any terminal state (`BP_JOB_STATE_CANCELLED`, `BP_JOB_STATE_TIMEOUT`, `BP_JOB_STATE_DONE`, `BP_JOB_STATE_ERROR`), the runtime immediately invokes:
   ```c
   bp_res_release_all(BP_RES_OWNER_JOB);
   ```
   This guarantees that any PIO state machines, DMA channels, hardware IRQ lines, or memory buffers leased by the job are cleanly reclaimed even if the slice callback was abruptly aborted.

### 2.2 Sub-GHz (CC1101) Sliced Job Architecture (ARCH-305)

The 7 long-running Sub-GHz operations (`scan`, `sweep`, `receive`, `record`, `waterfall`, `jam`, `bruteforce`) are converted to sliced state machines:
- **`scan` / `sweep` / `waterfall`**: Each slice checks the next frequency step or row. RSSI is measured, peak is updated, and progress percentage is streamed via `bp_job_emit_progress()`.
- **`receive` / `record`**: Slices poll the GDO0 pulse capture buffer. Completed frames or pulse counts are written to buffers or streamed.
- **`jam` / `bruteforce`**: Slices transmit bounded bursts or next key code candidates.
- **Cancellation Check**: At each slice boundary, `bp_job_check_cancelled()` is evaluated. If set, radio hardware is immediately transitioned to IDLE/RX safe state and returns `BP_STATUS_CANCELLED`.

### 2.3 RFID (PN532) Sliced Job Architecture (ARCH-306)

- **`scanner` (Inventory)**: Each slice polls for contactless targets (ISO14443A/B, Felica), logs unique UIDs, and emits progress.
- **`read` / `write`**: Slices operate across block ranges (0..63 for Classic 1K, 0..44 for Ultralight). Between block reads/writes, cancellation tokens are checked.

### 2.4 RF24 (nRF24L01) Sliced Job Architecture (ARCH-307)

- **`scan` / `sweep` / `waterfall`**: Slices iterate through RF channels 0..125, reading carrier detect / RPD registers.
- **`receive` / `jam`**: Slices poll RX FIFO or emit test carrier bursts with cooperative slice boundaries.

### 2.5 Infrared Sliced Job Architecture

- **`ir_record` / `ir_replay` / `ir_send` / `ir_jam`**: Slices capture incoming pulses from PIO or emit modulated bursts. Hardware modulation carrier timers are leased through the resource manager.

### 2.6 Sniffer Loops and FALA Integration (ARCH-308, ARCH-309, ARCH-313)

- **PS/2 Sniffer (`src/mode/ps2.c`)**: Claims PIO state machines (`kbd_sm`, `kbd_h2d_sm`, `kbd_idle_sm`, `kbd_inh_sm`) and PIO IRQs via `bp_res_claim_unused_pio_sm()` and `bp_res_acquire_irq()` under `BP_RES_OWNER_JOB`. Packet decoding runs cooperatively in job slices.
- **USB Sniffer (`src/mode/usb.c`)**: Claims PIO0/PIO1 state machines and IRQ vectors via `bp_res_*`. Packet collection and keylogger decoding execute in cooperative slices.
- **FALA Integration (`src/binmode/fala.c`, `logicanalyzer.c`)**:
  Logic analyzer DMA channels are acquired via `bp_res_claim_unused_dma_ch(BP_RES_OWNER_FALA, "fala_dma", ...)` and teardown uses `bp_res_release_all(BP_RES_OWNER_FALA)`.

## 3. Latency and Memory Bounds

- **Cancellation Latency**: Slices are bounded to <= 10 ms execution time. Thus, upon receiving Ctrl+C (`0x03`), the job aborts within < 20 ms (well within the strict < 100 ms limit).
- **Static Memory**:
  - Zero dynamic allocations (`malloc`, `free`, `new`, `delete`).
  - All job contexts and buffers are statically allocated in `.bss` / `.data`.
  - Static RAM ceiling remains <= 380 KB (tested against 512 KB SRAM).
  - Fixed stack budgets (4096 B for Core 0 in SCRATCH_Y, 4096 B for Core 1 in SCRATCH_X) are preserved.
