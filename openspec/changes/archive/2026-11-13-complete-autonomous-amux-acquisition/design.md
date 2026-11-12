# Design: Complete autonomous AMUX acquisition

## Context and current evidence

The current AMUX backend leases DMA channels but selects channels, waits and reads ADC conversions synchronously. Host buffer tests do not demonstrate autonomous acquisition. Long sweeps occupy CTRL and can disturb manual reads or live protocol pins.

Scope: Core 0 owns ADC, AMUX pins, DMA, scheduling and publication. Core 1 consumes immutable telemetry snapshots only. Complete real acquisition, arbitration and fault observability; preserve the synchronous fallback until the asynchronous backend is measured.

## Core ownership, state and failure behavior

Use a board-specific selector schedule that supports the noncontiguous GPIO38..41 mux wiring; a simple contiguous GPIO shift is not valid. Compare DMA/GPIO aliases, PIO and bounded alarm/IRQ sequencing and document the selected design against pinned SDK/upstream sources. Charge clearing, switch settling and conversion readiness must occur in that order; a DMA lease alone is not an implementation. Establish settling time from board/device data and scope captures before enabling the backend by default.

Model STOPPED, STARTING, SAMPLING, PAUSING, MANUAL and FAULT states. Reentrant same-core CPU ownership must not allow a manual read to reprogram an active DMA conversion. Pause and drain autonomous sampling before manual acquisition; resume only after release and a fresh full frame. Do not reclaim another owner's ADC after a timeout. Publish generation, monotonic capture timestamp, raw values and validity only after a complete frame; use the existing snapshot publication contract and memory barriers. If a consumer is lapped, retry within a finite bound or report stale data, never a mixed frame.

Prototype default cadence of 20 complete frames/second; it is a design target to confirm on hardware, not a measured capability. Specify maximum frame age and the PSU supervisor's stale-telemetry response. On FIFO overrun, DMA stall, missing completion, cancellation or resource conflict, stop the pipeline, invalidate affected readings, report counters through Core 0 and keep USB responsive. Do not silently substitute zero current as a valid measurement. Core 1 notification queues are bounded; queue-full handling must retain the latest telemetry and increment drops without blocking.

## Resources and rollback

Lease ADC/peripheral ownership, each mux GPIO, the sampling timer/alarm, DMA channels and IRQ handlers before enabling sampling. Roll back in reverse order on partial claims. No unregistered SDK channel claims.

All start/failure/stop paths need an explicit rollback table before implementation. Document queue/message types, producer/consumer core, capacity, ownership-transfer point and late-reply lifetime whenever a new cross-core request is introduced. Completion callbacks must not access a caller buffer after its documented release point.

## Static memory and lifetime

Use two uint16_t frames of HW_ADC_MUX_COUNT + 1 entries: 4 * (HW_ADC_MUX_COUNT + 1) bytes total (68 bytes for 16 mux channels plus current sense), reusing the existing allocation. Describe static descriptors, alignment and publication metadata byte-for-byte before coding. No unbudgeted third frame or allocation in callbacks. Reader buffers are caller-owned until return.

## Prerequisites and exclusions

DVI, voltage glitching, new ADC hardware and an unapproved RP2040 port.

No capability is complete merely because configuration accepts it. Hardware gates require independent evidence. New engineering choices and measured timing must be reflected in the corresponding delta scenarios before sign-off.

## Shared verification gates

All new runtime code must use static storage or bounded pools; no malloc/calloc/realloc/free/new/delete or heap-backed STL. RP2350 static main RAM must remain <=389120 bytes, including actual linked reservations measured by the existing memory guard. Baseline in the audit: 388504 bytes, leaving 616 bytes under the ceiling and 135784 bytes main-RAM headroom; scratch stacks are separately reserved at 4 KB/core. These are historical measurements, not a budget approval for future growth. Measure actual byte deltas and stack high-water after each task group.

Core 1 must not print, call FatFS or USB. Use typed bounded queues and published snapshots across cores; queue-full, disconnect, SD hotplug, timeout, cancel and watchdog behavior must be exercised where relevant. No busy wait may be unbounded. No hardware resource claims may bypass bp_res. Startup/teardown must restore leases, GPIO functions, buffer directions, IRQ handlers, clocks and PSU protection.

After each implementation task group, run `cmake --build build_rp2350 --target bus_pirate6`, the named focused suite and `bash tests/test_memory_model.sh`; record flash/RAM and warnings. Final gates: `bash tests/run_all.sh`, a separate full host run forcing GCC/G++ to `-O2 -fsanitize=address,undefined -fno-omit-frame-pointer`, zero-warning supported-platform builds and physical acceptance. Provide a reproducible wrapper/preset for optimized sanitizer flags rather than assuming scripts' existing -O1 runs satisfy -O2. Record each skipped case; a hardware skip cannot satisfy release acceptance. A build named build_rp2040 is not an RP2040 gate without verified platform identity.
