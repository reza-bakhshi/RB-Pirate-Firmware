# Proposal: Convert RF and IR Commands to Jobs

## Why

In the baseline firmware architecture, RF and IR operations (Sub-GHz CC1101, RFID PN532, RF24 nRF24L01, Infrared) as well as PS/2 and USB sniffer capture loops execute synchronous, blocking loops directly on Core 0. During frequency scans, signal sweeps, packet captures, brute-force transmissions, and sniffer monitoring:
1. **Core 0 UI & Terminal Lockup**: The linenoise shell prompt and status bar freeze completely, preventing user interaction or concurrent command inspection.
2. **Ad-Hoc Cancellation**: Cancellation relies on intermittent checks of `rx_fifo_try_get(&c)` inside tight loops, leading to unpredictable cancellation latency and terminal input buffer pollution.
3. **Resource Leak Hazards**: PIO state machines, DMA channels, and IRQ handlers claimed during sniffer and RF captures are not bound to transactional resource ownership, risking permanent hardware resource leaks if an operation aborts unexpectedly.
4. **Watchdog Risks**: Blocking loops on Core 0 risk tripping the hardware watchdog if an external bus device stalls or holds clock lines low.

Step C1 (covering ARCH-305, ARCH-306, ARCH-307, ARCH-308, ARCH-309, and ARCH-313) converts these long-running operations into asynchronous, cancellable jobs dispatched to the Core 1 data plane via `bp_job_dispatch()`. Core 0 remains non-blocking and interactive, Ctrl+C cancellation halts jobs within 100 ms, and all leased peripherals are released cleanly via `bp_res_release_all(BP_RES_OWNER_JOB)`.

## What Changes

- **Job Dispatching API (`src/kernel/bp_job.h`, `src/kernel/bp_job.c`)**:
  - Implement `bp_job_dispatch()` to automatically timestamp and submit jobs from Core 0 to Core 1.
  - Wire automatic transactional cleanup in `bp_job_service()`: call `bp_res_release_all(BP_RES_OWNER_JOB)` on job completion, cancellation, timeout, or error.
- **Sub-GHz (CC1101) Job Conversion (ARCH-305)**:
  - Convert `scan`, `sweep`, `receive`, `record`, `waterfall`, `jam`, and `bruteforce` into cooperative job slice functions.
  - Check `bp_job_check_cancelled()` at every frequency channel and sample slice.
- **RFID (PN532) Job Conversion (ARCH-306)**:
  - Convert tag `scanner` (inventory), `read`, and `write` loops into cancellable job slices.
- **RF24 (nRF24L01) Job Conversion (ARCH-307)**:
  - Convert `scan`, `sweep`, `waterfall`, `receive`, and `jam` loops into cooperative job slices with slice-boundary cancellation checks.
- **Infrared Job Conversion**:
  - Convert `record`, `send`, `replay`, and `jam` into cooperative jobs executed on Core 1.
- **PS/2 and USB Sniffer Capture Conversion (ARCH-308, ARCH-309)**:
  - Convert PS/2 sniffer and USB low-speed keylogger loops into cancellable jobs.
  - Lease all PIO state machines, DMA channels, and IRQ handlers through `bp_res_*` under `BP_RES_OWNER_JOB`.
- **FALA Resource Integration (ARCH-313)**:
  - Integrate Fast Logic Analyzer DMA channel acquisition and teardown with `bp_res_*` under `BP_RES_OWNER_FALA`.
- **Terminal Service and Ctrl+C Latency**:
  - Maintain non-blocking terminal I/O on Core 0 while jobs execute on Core 1.
  - Guarantee job abortion and peripheral teardown within 100 ms of Ctrl+C.
- **Verification and Test Suite (`tests/test_rf_ir_jobs.sh`)**:
  - Dedicated host test suite verifying job dispatch, progress streaming, timeout enforcement, Ctrl+C cancellation, and zero leaked resource leases under ASan and UBSan.

## Capabilities

### New Capabilities
None.

### Modified Capabilities
- `protocol-jobs`: Adds requirements for long-running RF/IR/sniffer job conversion, automatic `BP_RES_OWNER_JOB` cleanup on teardown, sub-100ms cancellation latency, and FALA resource integration.

## Impact

- Affects `src/kernel/bp_job.h`, `src/kernel/bp_job.c`.
- Affects `src/mode/subghz.c`, `src/mode/rfid.c`, `src/mode/rf24.c`, `src/mode/infrared.c`, `src/mode/ps2.c`, `src/mode/usb.c`.
- Affects `src/binmode/fala.c`, `src/binmode/logicanalyzer.c`.
- Eliminates blocking loops on Core 0, preserving UI responsiveness and deterministic real-time deadlines.
- Strictly adheres to the static memory model (RAM <= 380 KB, zero dynamic allocation, 4096-byte fixed stacks).
