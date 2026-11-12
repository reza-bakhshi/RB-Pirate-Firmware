# Autonomous AMUX acquisition baseline and hardware inventory evidence

Date: 2026-11-12. Scope: Task Group 1 hardware pin inventory, timing analysis, and architecture baseline.

## 1. Hardware Pin & Multiplexer Inventory

From `src/platform/rb-pirate.h`:

| Function | Pin Name | GPIO | Hardware Pin / ADC Channel | Direction / Description |
|---|---|---|---|---|
| AMUX S0 (Bit 0) | `AMUX_S0` | GPIO 40 | Pin 49 | Output, CD4067 channel select bit 0 |
| AMUX S1 (Bit 1) | `AMUX_S1` | GPIO 41 | Pin 52 | Output, CD4067 channel select bit 1 |
| AMUX S2 (Bit 2) | `AMUX_S2` | GPIO 39 | Pin 48 | Output, CD4067 channel select bit 2 |
| AMUX S3 (Bit 3) | `AMUX_S3` | GPIO 38 | Pin 47 | Output, CD4067 channel select bit 3 |
| AMUX Analog Out | `AMUX_OUT` | GPIO 46 | Pin 57 / ADC6 | Input (Analog), voltage divider $\times 2$ |
| Current Sense | `CURRENT_SENSE` | GPIO 47 | Pin 58 / ADC7 | Input (Analog), direct sense $\times 1$ |

### Multiplexer Channel Allocation

| Channel | Identifier | Monitored Signal | Scale Factor |
|---|---|---|---|
| 0 | `HW_ADC_MUX_BPIO0` | Buffered I/O Pin 0 | $\times 2$ |
| 1 | `HW_ADC_MUX_BPIO1` | Buffered I/O Pin 1 | $\times 2$ |
| 2 | `HW_ADC_MUX_BPIO2` | Buffered I/O Pin 2 | $\times 2$ |
| 3 | `HW_ADC_MUX_BPIO3` | Buffered I/O Pin 3 | $\times 2$ |
| 4 | `HW_ADC_MUX_BPIO4` | Buffered I/O Pin 4 | $\times 2$ |
| 5 | `HW_ADC_MUX_BPIO5` | Buffered I/O Pin 5 | $\times 2$ |
| 6 | `HW_ADC_MUX_BPIO6` | Buffered I/O Pin 6 | $\times 2$ |
| 7 | `HW_ADC_MUX_BPIO7` | Buffered I/O Pin 7 | $\times 2$ |
| 8 | `HW_ADC_MUX_VUSB` | USB VBUS 5V Rail | $\times 2$ |
| 9 | `HW_ADC_MUX_VREG_OUT` | Onboard VREG Output | $\times 2$ |
| 10 | `HW_ADC_MUX_VREF_VOUT` | Target Reference VOUT | $\times 2$ |
| 11 | `HW_ADC_MUX_CURRENT_DETECT` | PSU Hardware Overcurrent Latch | $\times 2$ |
| 12 | `HW_ADC_MUX_CARD_DETECT` | MicroSD Card Detect | $\times 2$ |
| 13..14 | Reserved | Unconnected / Test | $\times 2$ |
| 15 | `HW_ADC_MUX_GND` | Ground Reference (Charge Clear) | 0 V |
| 16 | `HW_ADC_CURRENT_SENSE` | Direct Current Sense (ADC7) | $\times 1$ |

**Total Frame Channels**: 17 entries (`HW_ADC_MUX_COUNT + 1`).

## 2. Pin Non-Contiguity and Selector Arithmetic

The 4 selector lines on RB-Pirate are non-contiguous in binary weight:
* $S_0$ = GPIO 40
* $S_1$ = GPIO 41
* $S_2$ = GPIO 39
* $S_3$ = GPIO 38

A simple bitshift on `channel` is electrically invalid. The 64-bit mask for RP2350 GPIO output is:
```c
uint64_t gpio_val = (((uint64_t)(channel & 1) << 40) |
                     ((uint64_t)((channel >> 1) & 1) << 41) |
                     ((uint64_t)((channel >> 2) & 1) << 39) |
                     ((uint64_t)((channel >> 3) & 1) << 38));
uint64_t gpio_mask = ((1ULL << 38) | (1ULL << 39) | (1ULL << 40) | (1ULL << 41));
```

## 3. Physical Timing Diagram & Conversion Sequence

```text
Time (us)  | 0       10           50    52     62          102   104
-----------+--------------------------------------------------------------
Mux Pins   | GND (15) | Ch 0       | Conv | GND (15) | Ch 1      | Conv ...
AMUX_OUT   | Settling to GND (10us)| Settle (40us) | Settle (10)| Settle (40us)
ADC State  | Idle     | Idle       | Conv | Idle     | Idle      | Conv ...
ADC Input  | In 6 (AMUX_OUT)       | In 6 | In 6     | In 6      | In 6
-----------+--------------------------------------------------------------
Final step: Switch ADC to In 7 (CURRENT_SENSE) -> wait 20us -> Conv ADC7
Frame Published: generation++, timestamp = time_us_64(), valid = true
Total Frame Sweep Time: ~900 us.
Cadence Target: 20 Hz (50 ms interval). Duty Cycle: ~1.8%.
```

## 4. State Machine Definition

* `AMUX_STATE_STOPPED`: Subsystem uninitialized or resources released.
* `AMUX_STATE_IDLE`: Awaiting next scheduled 50 ms tick; ADC released for Core 0 tasks.
* `AMUX_STATE_SAMPLING`: Active sweep in progress (channels 0..15 + current sense).
* `AMUX_STATE_PAUSING`: Drain in-flight conversion for high-priority manual scalar read.
* `AMUX_STATE_MANUAL`: Direct manual conversion (`amux_read`, `amux_read_bio`) holding ADC lock.
* `AMUX_STATE_FAULT`: Stalled conversion, overrun, or lease exhaustion; telemetry marked invalid.

## 5. Memory Budget Accounting

* Storage: 2 ping-pong frames $\times$ 17 entries $\times$ 2 bytes = **68 bytes static RAM**.
* Descriptors / metadata: 32 bytes static RAM.
* Total memory footprint: **100 bytes static RAM** (already accounted for in the 388,504-byte baseline).
* Heap allocation: **Strictly 0 bytes**.

## 6. Implementation & Verification Evidence (Groups 2..7)

### Group 2: Latency & Settling Baseline
* Calculated sweep duration: $\sim 900\,\mu\text{s}$ per frame (16 channels $\times$ 50 $\mu$s settle/convert + GND discharge + current sense).
* Target cadence: 20 Hz (every 50 ms), resulting in $\le 2\%$ CPU core occupancy for acquisition.
* Physical scope / logic analyzer captures remain marked **BLOCKED** pending a physically connected test fixture.

### Group 3: Transactional Resource Leasing & Rollback
* Implemented `BP_RES_PERIPH_ADC` in `src/kernel/bp_resource.h`.
* `amux_dma_init()` transactionally acquires:
  1. `BP_RES_PERIPH_ADC`
  2. Data DMA channel (`amux_adc`)
  3. Control DMA channel (`amux_ctrl`)
* Rollback verified in `tests/test_dma_pipeline.c`: intentionally exhausting DMA channels causes `amux_dma_init()` to fail and roll back the ADC peripheral lease cleanly with zero resource leaks.

### Group 4: Frame Publication & Coherency
* Implemented `amux_frame_t` and `amux_get_frame()` in `src/pirate/amux.c`.
* Frame publication increments a monotonic generation counter and stamps the frame with `timestamp_us`.
* Readers extract complete frames using seqlock-style verification (bounded 3 retries); torn or mixed channel frames are never returned.
* Implemented startup priming in `amux_dma_init()`: initial sweep is primed immediately on initialization so coherent telemetry is published without startup stall.

### Group 5: Manual Read Pause Arbitration
* `amux_read()` and `amux_read_current()` set `AMUX_STATE_PAUSING` before locking the ADC, transition to `AMUX_STATE_MANUAL` during the conversion, and safely restore `AMUX_STATE_IDLE` on release.
* Verified non-blocking arbitration with active background sampling in `tests/test_dma_pipeline.c` and `tests/test_power_monitor.c`.

### Group 6: Stale Telemetry, Overrun & Fault Injection Trip
* Defined `AMUX_MAX_FRAME_AGE_US` (250 ms) and pipeline fault codes (`AMUX_FAULT_OVERRUN`, `AMUX_FAULT_STALL`, `AMUX_FAULT_TIMEOUT`, `AMUX_FAULT_RESOURCE`).
* Implemented `amux_is_telemetry_fresh()`, `amux_inject_fault()`, and `amux_clear_fault()`.
* When in `AMUX_STATE_FAULT` or telemetry is stale (>250 ms), `amux_get_frame()` and `amux_dma_get_coherent()` return false, and background sweeps refuse to run until explicitly cleared.
* Integrated stale telemetry supervisor in `psu_poll_fuse_vout_error()` in `src/pirate/psu.c`: trips `psu_disable()`, sets `error_stale_telemetry = true`, sets `error_pending = true`, and cancels any active protocol job.
* Integrated UI error notification in `psucmd_show_clear_error()` in `src/commands/global/w_psu.c`.
* Verified with unit tests and AddressSanitizer/UBSan in `tests/test_power_monitor.c`:
  - Simulated 300 ms time stall trips supervisor within documented timeout.
  - Fault injection (`amux_inject_fault(AMUX_FAULT_STALL)`) immediately invalidates frame, aborts PSU regulator, and cancels active jobs.
  - Recovery via `amux_clear_fault()` and subsequent sweep restores healthy publication.
  - Snapshot lapping and rapid producer generation increments verified with seqlock extraction.

### Group 7: Behavior Under Concurrent Subsystem Load
* Executed unified test runner `tests/run_all.sh` across all 39 test suites:
  - Total Suites: 39 (37 passed, 2 physical HIL skipped due to disconnected target fixtures, 0 failures).
  - Validated concurrent USB transport (`test_bin_transport.sh`), MSC block storage (`test_msc_storage.sh`), DMA pipeline (`test_dma_pipeline.sh`), and protocol workloads (`test_can_glitch.sh`, `test_subghz.sh`, etc.).
* Physical 30-minute scope acquisition traces remain **BLOCKED pending hardware fixture**.

### Build & Memory Gates
* Build: `cmake --build build_rp2350 --target bus_pirate6` passes with 0 warnings.
* Static RAM: 388,612 bytes ($\le$ 389,120 byte ceiling, 508 bytes headroom). Zero dynamic heap allocations across runtime firmware.
* Build identity: `supported_contract_passed: true` (RP2350 ARMv8-M.mainline Cortex-M33, rb-pirate board).
* Unified Test Suite: 37 passed, 2 hardware skips, 0 failures. Sanitizers (ASan + UBSan): all clean.
