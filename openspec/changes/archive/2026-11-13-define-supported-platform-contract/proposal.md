# Define and enforce the supported platform contract

## Why

Top-level CMake forces RP2350 and both existing build directories cache rp2350-arm-s. A directory named build_rp2040 is therefore not RP2040 evidence, while the project OpenSpec context explicitly excludes BP5/RP2040.

## What Changes

Build configuration and product capability reporting. Record a product decision: enforce the current RP2350-only scope, or approve a separately scoped RP2040 port before changing the support claim. Until that decision, RP2350-only remains the effective contract.

Priority: **P1**. This is planned work, not an implementation or hardware verification claim. Existing source changes and passing host tests are recorded separately in [audit evidence](../../../docs/audits/2026-11-12-safety-hardening.md).

## Capabilities

### New Capabilities

- `platform-support`: the observable acceptance requirements in this change.

### Modified Capabilities

Existing capabilities remain unchanged until implementation; this change adds their qualification gates.

## Impact

Document platform-specific available PIO, DMA, IRQ, GPIO, timers and accelerator counts. Resource limits and pin mapping must derive from the selected hardware, not an RP2350 assumption.

No device is energized or flashed by this proposal. The hardware inventory is a documented inventory, not proof of a connected fixture. The change must fill its [acceptance checklist](../../checklists/define-supported-platform-contract.md) before archival. Dependencies and sequencing are in [PRODUCTION_READINESS.md](../../PRODUCTION_READINESS.md).

## Out of scope

Silently approving an RP2040 port, claiming RP2040 compatibility from directory names, or requiring the RP2350 RAM budget to fit RP2040.
