# Verify board fault recovery and resource lifecycle

## Why

Two physical HIL suites were skipped during host verification. Passing fake peripherals cannot establish PSU trip behavior, SD removal recovery, LCD shifter completion, SWD edge timing or mode-switch resource restoration.

## What Changes

Core 0 owns USB, PSU supervision, storage and test reporting. Core 1 owns protocol jobs and worker cancellation. Add reproducible board procedures and harness cases with independently observed outputs.

Priority: **P1**. This is planned work, not an implementation or hardware verification claim. Existing source changes and passing host tests are recorded separately in [audit evidence](../../../docs/audits/2026-11-12-safety-hardening.md).

## Capabilities

### New Capabilities

- `hardware-validation`: the observable acceptance requirements in this change.

### Modified Capabilities

Existing capabilities remain unchanged until implementation; this change adds their qualification gates.

## Impact

Record PIO/SM, DMA, IRQ, timers, GPIO/buffer direction, clocks, SPI ownership and large-buffer ownership before and after every case. Fixtures reserve only their documented pins; unrelated debug UART pins must be excluded.

No device is energized or flashed by this proposal. The hardware inventory is a documented inventory, not proof of a connected fixture. The change must fill its [acceptance checklist](../../checklists/verify-board-fault-recovery.md) before archival. Dependencies and sequencing are in [PRODUCTION_READINESS.md](../../PRODUCTION_READINESS.md).

## Out of scope

Claiming a test passed from source review, automatically energizing unidentified loads, or developing an unrelated feature.
