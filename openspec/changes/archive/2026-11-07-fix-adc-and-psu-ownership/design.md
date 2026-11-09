# Design: Fix ADC and PSU Ownership

## Architectural Overview
The analog multiplexer (AMUX) connects all 8 BIO pins, Vout/Vref, Vreg, and current sense to the single ADC peripheral.
Under the multicore rearchitecture:
- **Core 0 (Control Plane)** exclusively owns periodic analog sweeping (`amux_sweep()`), system monitoring (`monitor()`), and PSU overcurrent protection.
- **Core 1 (Data Plane)** never performs unsolicited AMUX sweeping. Instead, Core 1 and asynchronous binary protocols (e.g. BPIO) read atomic, consistent voltage and current measurements from the lockless seqlock snapshot `bp_ui_snapshot_t`.

```
                  Core 0 (Control Plane)
  +-------------------------------------------------------+
  |  System Monitor Tick                                  |
  |        |                                              |
  |        v                                              |
  |  amux_sweep() [Single owner]                          |
  |        |                                              |
  |        v                                              |
  |  bp_ui_snapshot_publish()                             |
  |  (seqlock seq++)                                      |
  |        |                                              |
  |        +---------------------+                        |
  |                              |                        |
  |  PSU Poll Fuse & Vout        |                        |
  |  [Overcurrent / Trip]        |                        |
  |        |                     |                        |
  |        v (On Fault)          |                        |
  |  psu_disable()               |                        |
  |  bp_job_cancel()             |                        |
  +------------------------------|------------------------+
                                 | Shared RAM (Atomic Seqlock)
                                 v
  +-------------------------------------------------------+
  |  bp_ui_snapshot_read()                                |
  |        |                                              |
  |        +---> UI Toolbars / Status Bar                 |
  |        |                                              |
  |        +---> Core 1 BPIO ADC Status Requests          |
  |        |                                              |
  |        +---> Protocol Jobs (Non-intrusive voltage)    |
  +-------------------------------------------------------+
                  Core 1 (Data Plane)
```

## Reentrant ADC Arbitration
`adc_busy_wait()` is refactored to support reentrant locking with owner tracking:
- `s_adc_owner_core`: records core ID (`0` or `1`, or `-1` when unowned).
- `s_adc_nest_count`: tracks nesting depth on the owning core.
- If the calling core already owns the lock, it increments `s_adc_nest_count` and returns immediately without spinning.
- If owned by another core, bounded spinning with backoff (`tight_loop_contents()`) prevents bus contention and deadlock.
- Interrupt-safe spinlock operations (`spin_lock_blocking` / `spin_unlock`) prevent ISR deadlocks.

## Seqlock Snapshot Updates
`bp_ui_snapshot_t` is updated to include:
- `pin_voltages[HW_PINS]`: measured millivolts for all pins.
- `vreg_mv`, `vout_mv`: power supply voltages.
- `current_ma`: measured current in tenths of mA.
- `psu_enabled`, `psu_fuse_ok`, `psu_error_overcurrent`, `psu_error_undervoltage`: authoritative PSU status.

When `monitor()` runs on Core 0:
1. `amux_sweep()` captures hardware channels.
2. `bp_ui_snapshot_publish_power(...)` writes the snapshot with full memory barriers.
3. Readers use `bp_ui_snapshot_read(...)` to obtain consistent values without taking locks.

## PSU Protection and Fault Coordination
In `psu_poll_fuse_vout_error()` on Core 0:
1. Checks fuse and voltage boundaries.
2. If overcurrent or undervoltage occurs:
   - Immediately invokes `psu_disable()`.
   - Flags fault in `psu_status` and snapshot.
   - If a protocol job is running on Core 1 (`bp_job_is_active()`), triggers `bp_job_cancel(0)` to float pins and abort the operation.
