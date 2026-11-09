# Proposal: Fix ADC and PSU Ownership

## Why
Currently, the ADC and analog multiplexer (AMUX) are accessed concurrently by both Core 0 (control plane / system monitor / PSU polling / commands) and Core 1 (BPIO / protocol routines) via a crude spinlock in `adc_busy_wait()`. This design has major failure modes:
1. `adc_busy_wait()` is not reentrant and does not track core ownership; nested calls on the same core cause hard deadlocks.
2. The spinlock spins without backoff or yielding, contending on the system bus.
3. Reading pin voltages (`amux_sweep()`) on Core 1 during active protocols (such as SPI) loads the bus pins for milliseconds and corrupts in-flight data.
4. Readers of ADC voltages and PSU current read unsynchronized global arrays, risking data tearing.
5. PSU overcurrent and undervoltage polling must have a single authoritative control-plane owner on Core 0 that can trip the PSU and safely abort running protocol jobs.

## What Changes
- Implement reentrant, owner-aware ADC locking (`adc_busy_wait()`) with core ID tracking, nesting counter, and bounded wait to prevent lockups.
- Restrict all periodic AMUX sweeping and ADC monitoring exclusively to Core 0 (control plane).
- Publish atomic voltage, current, and PSU status snapshots via the lockless seqlock `bp_ui_snapshot_t`.
- Update `system_monitor`, UI toolbars, and binary modes (such as BPIO) to read atomic snapshot values without touching hardware ADC or disturbing live bus pins.
- Provide a request/response mechanism for on-demand protocol ADC measurements.
- Ensure Core 0 PSU fuse polling immediately disables the PSU and notifies active protocol jobs to abort on fault.
- Provide host unit test suites under AddressSanitizer and UndefinedBehaviorSanitizer validating ADC lock reentrancy, snapshot seqlock consistency, and PSU fault response.

## Capabilities
### New Capabilities
- `power-monitor`: Defines single-core control-plane ownership of ADC/AMUX sampling, atomic monitor snapshots, and PSU fault handling.

### Modified Capabilities
None.

## Impact
- Eliminates multi-core spinlock contention and deadlocks on ADC.
- Ensures active protocol pins on Core 1 are not loaded by concurrent AMUX sweeping.
- PSU overcurrent trip triggers immediate power cutoff and orderly job cancellation.
- Static RAM footprint remains well within the 380 KB limit.
