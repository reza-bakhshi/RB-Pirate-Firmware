# Complete autonomous AMUX acquisition

## Why

The current AMUX backend leases DMA channels but selects channels, waits and reads ADC conversions synchronously. Host buffer tests do not demonstrate autonomous acquisition. Long sweeps occupy CTRL and can disturb manual reads or live protocol pins.

## What Changes

Core 0 owns ADC, AMUX pins, DMA, scheduling and publication. Core 1 consumes immutable telemetry snapshots only. Complete real acquisition, arbitration and fault observability; preserve the synchronous fallback until the asynchronous backend is measured.

Priority: **P1**. This is planned work, not an implementation or hardware verification claim. Existing source changes and passing host tests are recorded separately in [audit evidence](../../../docs/audits/2026-11-12-safety-hardening.md).

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `power-monitor`: complete the remaining acceptance and fault behavior described by its delta requirements.

## Impact

Lease ADC/peripheral ownership, each mux GPIO, the sampling timer/alarm, DMA channels and IRQ handlers before enabling sampling. Roll back in reverse order on partial claims. No unregistered SDK channel claims.

No device is energized or flashed by this proposal. The hardware inventory is a documented inventory, not proof of a connected fixture. The change must fill its [acceptance checklist](../../checklists/complete-autonomous-amux-acquisition.md) before archival. Dependencies and sequencing are in [PRODUCTION_READINESS.md](../../PRODUCTION_READINESS.md).

## Out of scope

DVI, voltage glitching, new ADC hardware and an unapproved RP2040 port.
