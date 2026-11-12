# Firmware audit and hardening, 2026-11-12

## Assessment

Later implementation updates: [ADC/SHA/SD batch](2026-11-12-follow-up.md) and [glitch/display/SWD/storage batch](2026-11-12-safety-hardening.md) supersede the original finding statuses below.

This repository is not ready for an exhaustive production-readiness sign-off. This audit pass fixes reproducible command buffer, profiler, calibration, and SD error-path defects, but identifies substantial outstanding hardware and concurrency work. Host tests exercise simulated hardware and cannot certify electrical timing or inter-core behavior. The user authorized review and changes in this repository; no devices were flashed, external services modified, or commits created.

The requested two-platform premise does not match the implementation: top-level CMake forces `rp2350` and `rb-pirate`. Both existing build directories cache `PICO_PLATFORM=rp2350-arm-s`. Building a directory named `build_rp2040` does **not** verify RP2040 compatibility. Restoring it requires board selection, accelerator alternatives, and a separate RAM budget; this patch does not represent that work as complete.

## Fixed findings: evidence → finding → remediation

| ID | Severity | Evidence and trigger | Impact | Remediation and verification |
|---|---|---|---|---|
| F01 | P0 | `cmsis_dap_process_command`, Info strings and UART buffer-size response, capacity of four bytes | Writes beyond caller response capacity | Check each response size before copying; boundary tests preserve sentinel bytes at every insufficient capacity |
| F02 | P1 | DAP transfer/block loops execute reads after response capacity is exhausted | Reads with target side effects are counted but data is discarded | Stop before the next read, report error and completed count; maximal block-count regression with four-byte response |
| F03 | P1 | SWJ Sequence count zero becomes 64 | Incomplete full-length sequence | Decode as 256 using 16-bit loop/count; reject short payload and accept full payload in host tests |
| F04 | P1 | `w.profile` computes elapsed time and duration multiplication in 32 bits | Duration and timestamps wrap after 4294.967296 seconds; long requested durations can end early | Shared clock helper extends sampled timer deltas into 64 bits; duration comparison multiplies in 64 bits; twelve-hour integration/rollover regression |
| F05 | P1 | Profiler cancellation enters a character test with a zero character | Job cancellation does not terminate profiling | Check cancellation independently before checking typed input; source review/build verification |
| F06 | P1 | Calibration JSON numbers cast from double to 16-bit values before validation | Invalid conversion and out-of-range coefficients can be accepted after narrowing | Require all eight integer coefficients, finite/range checks before casts; malformed, huge, wrapping, fractional and missing-field regressions |
| F07 | P1 | `psu_cal_save` ignores sync and close status | Reports successful persistence despite storage failure | Require successful write, full byte count, sync and close; injected short-write/sync/close regressions |
| F08 | P1 | `tf_card.c` write path takes SPI ownership then returns if `_select()` fails | SPI ownership remains held after card failure | Deselect and release ownership before returning; firmware build and source-path review (no card-failure HIL available) |
| F09 | P2 | `rand -c 0` reports an error without setting command result | Automation can interpret invalid input as success | Set `res->error`; build/source verification |
| F10 | P2 | Profiler copies the `snprintf` required length without testing truncation | A future enlarged row could cause a stack over-read | Copy only a positive length smaller than the source row buffer; build/source verification |

No runtime allocations or global buffers were added. The profile clock occupies local storage. Existing CRLF line endings in `w_psu.c` were retained; use `git -c core.whitespace=cr-at-eol diff --check` for whitespace verification.

Calibration compatibility: files must now contain all eight fields written by `psu_cal_save`; incomplete hand-written files fall back to nominal defaults. Save success does not provide power-loss atomicity: the file is still overwritten in place. The loader does not authenticate JSON or persist its in-memory checksum, so plausible numeric corruption is not detected cryptographically.

## Findings and remediation order

Follow-up implementation: O01 and O03 are fixed with bounded, status-returning ADC ownership and serialized sampling/copying. O09 timer rollover comparisons are fixed; sector arithmetic/capacity validation remains open. O12 host SHA syntax and padding are fixed with independent known-answer coverage; command-level I/O and accelerator-busy coverage remain open. See [follow-up validation](2026-11-12-follow-up.md).

| ID | Severity | Evidence | Required next work |
|---|---|---|---|
| O01 | P0 | `amux.c:adc_busy_wait` overwrites owner/depth after one million spins while another core may still own ADC | Replace forced ownership with a status-returning bounded acquisition; propagate failure to every caller and add stalled-owner cross-core tests |
| O02 | P1 | `amux_dma_init` leases two channels but `amux_dma_sweep_start` loops over `adc_read`/delays without configuring DMA | Implement actual mux sequencing and DMA chain or expose this accurately as synchronous sampling; validate hardware settling and cancellation |
| O03 | P1 | `amux_dma_get_coherent` copies one indexed ping-pong buffer without a publication sequence or reader acknowledgement | Prevent reuse during a reader copy, or publish into the established snapshot path; stress producer lapping the consumer |
| O04 | P1 | `hstx_dvi.c` feeds active pixel rows without horizontal/vertical blanking or sync control symbols; service depends on polling, restart only changes read address | Implement full frame timing, HSTX pin mapping, clock derivation, DMA transfer-count rearm and IRQ chaining before claiming 640×480@60 Hz |
| O05 | P1 | HSTX claims raw DMA outside `bp_res` and returns init success even if no channel is available | Lease DMA/peripheral/pins transactionally and fail initialization on unavailable resources |
| O06 | P1 | `cmd_glitch.c` directly changes BIO/PSU control GPIOs without leasing or restoring their prior mode; accepts multi-second interrupt-disabled delays and widths | Add bounded pulse/delay/count limits, typed worker execution, ownership rollback and explicit edge selection |
| O07 | P1 | Power glitch maps to `CURRENT_EN_OVERRIDE`, also used by PSU to override current limiting | Verify the board circuit before advertising this as a crowbar; implement an independent hardware-enforced pulse limit and restore PSU protection on all exits |
| O08 | P1 | `s_dap.retry_count` is configured, but firmware `swd_transfer` calls hardware once | Implement bounded WAIT retries and protocol-specific FAULT/data-phase recovery; host mock currently always ACKs and does not test physical parity/turnaround behavior |
| O09 | P1 | SD waits compare `_millis() < start + timeout`; non-block cards multiply LBA by 512 without a conversion bound | Use wrap-safe subtraction and validate sector arithmetic/count/capacity; inject rollover and out-of-range requests |
| O10 | P1 | `bp_lcd_wait_idle` calls blocking DMA wait without deadline; window commands use shared SPI directly | Add bounded completion/error signaling and verify SPI ownership, chip-select/DC ordering and shared-bus arbitration on hardware |
| O11 | P1 | Calibration writer truncates destination before durable replacement | Write/sync a temporary file and replace through the storage service with an explicit power-failure recovery contract |
| O12 | P2 | `cmd_sha256.c` host-only constant is spelled `0bef9a3f7`, and unified tests have no SHA command suite | Repair host test path and add known-answer padding/block-boundary, I/O failure and accelerator-busy tests before claiming hash coverage |
| O13 | P1 | Top-level platform is hard-coded and both build caches are RP2350 | Restore real platform selection and hardware fallbacks, or formally change the supported-platform contract |

These are source-supported findings, not claims of observed physical damage. In particular O01 establishes unsafe ownership transfer; an actual inter-core collision was not reproduced on a board.

## Review coverage and limits

| Area | Reviewed evidence | What remains unverified |
|---|---|---|
| A: services/faults | Terminal has dedicated SPSC queues and bounded 256-byte draining; storage RPC has timeout; watchdog feeds only when both heartbeats advance; crash record is in `.uninitialized_data` in `pirate/crash_record.c`, not `kernel/bp_health.c` | Full call-graph proof of Core 1 restrictions, queue lifetime after RPC timeout, ISR/core races, fault register capture and warm-reset retention on device |
| B: acceleration/CAN/glitch | SHA reads bounded 512-byte blocks, finishes accelerator even after read error; entropy delegates to SDK `pico/rand`; CAN leases four PIO1 SMs and filters numeric IDs; glitch checks pin ranges and supports keyboard/button trigger cancellation | SHA hardware state reset/long-file service responsiveness, entropy source failure behavior, standard-vs-extended filter semantics, CAN bus-off recovery and physical transmission counters |
| C: pipelines/storage | AMUX/LCD resource paths and SD multiblock paths inspected; SD byte-sized DMA supports unaligned byte buffers | Autonomous AMUX requirement is unmet; actual bus timing, SD capacity limits, cross-core publication and LCD completion require additional work |
| D: PSU | Calibration range/checksum checks, persistence, wizard calculations and profiler arithmetic inspected | Wizard is zero-offset plus single loaded-voltage measurement, rather than two independent nonzero points; it measures through existing calibration. Accuracy and behavior across repeated calibration require raw-ADC tests |
| E: DAP/video/USB | DAP packet bounds, parity expression, turnaround, retry path, HSTX service; USB descriptors contain BOS/MS OS 2.0 WinUSB association and compile-time length check | SWD hardware branch bypassed by host mock; Windows/macOS/Linux enumeration and physical DVI timing not tested |
| F: modes | Central transactional mode switch/rollback and representative I2C/SPI/UART/1-Wire/RF24/RFID/LCDTAP setup/cleanup paths; unified mode, PS/2, RF, CELL tests | No exhaustive per-driver/per-argument reachability proof or repeated physical mode-switch/leak test; syntax/source scanning alone cannot establish ownership correctness |

Heap-symbol tests cover selected object families, not every linked library or reachable allocation path. The memory ceiling leaves only 616 bytes below the RP2350 static budget; preserve that constraint when addressing open items. Scratch stacks are separately reserved at 4 KB per core and require hardware high-water measurements.

## Verification

Baseline: 36 suites, 34 passed, two physical HIL suites skipped, zero failures. Both requested incremental builds succeeded, but both compile RP2350. This is not a clean rebuild warning audit.

Final verification results are recorded in the accompanying validation file. Logs are local under `/tmp/bp-audit-*.log`. To reproduce normal checks:

```sh
bash tests/run_all.sh
cmake --build build_rp2350 --target bus_pirate6
cmake --build build_rp2040 --target bus_pirate6
bash tests/test_memory_model.sh
```

For the additional optimized sanitizer run, temporary compiler wrappers invoke `/usr/bin/gcc` and `/usr/bin/g++` with original arguments followed by `-O2 -fsanitize=address,undefined -fno-omit-frame-pointer`. Prepend their directory to PATH when invoking `tests/run_all.sh`. This tests host compilations, not instrumented ARM firmware or physical devices.

## Feature priorities

First complete DMA AMUX and DVI requirements already advertised in OpenSpec, then add CAN bus-off observability/recovery and deterministic glitch safety. SWO trace or richer waveform rendering should follow these correctness repairs. The delivered feature expansion is reliable multi-hour profiling with monotonic 64-bit CSV timestamps and a directly tested reusable elapsed-time helper.
