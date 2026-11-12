# Qualify DVI and dedicated crowbar board capabilities

## Why

RB-Pirate defines GPIO16..19 as PSU controls and has no supported DVI connector map or dedicated crowbar output. Earlier code confused current-limit override with a crowbar and advertised incomplete HSTX timing. Software guards now reject those physical capabilities.

## What Changes

Hardware feasibility and capability gating for optional board revisions/adapters, followed only after qualification by separate physical-output implementations. Core 0 owns PSU safety and capability status; Core 1 owns bounded logic/protocol work; video DMA/IRQ ownership must be explicitly selected.

Priority: **P1**. This is planned work, not an implementation or hardware verification claim. Existing source changes and passing host tests are recorded separately in [audit evidence](../../../docs/audits/2026-11-12-safety-hardening.md).

## Capabilities

### New Capabilities

- `board-expansion`: the observable acceptance requirements in this change.

### Modified Capabilities

Existing capabilities remain unchanged until implementation; this change adds their qualification gates.

## Impact

Provide a complete GPIO/function conflict matrix for PSU, storage, debug, HSTX lanes/clock, buffers and trigger/output pins. DVI needs leased serializer/clock/DMA/IRQs and pins. Crowbar needs exclusive dedicated output/trigger resources plus an independently bounded hardware pulse mechanism.

No device is energized or flashed by this proposal. The hardware inventory is a documented inventory, not proof of a connected fixture. The change must fill its [acceptance checklist](../../checklists/qualify-dvi-and-crowbar-board-capabilities.md) before archival. Dependencies and sequencing are in [PRODUCTION_READINESS.md](../../PRODUCTION_READINESS.md).

## Out of scope

Driving PSU override as a crowbar, automatically remapping live PSU/debug pins, declaring external hardware present from MCU type, or treating host text rendering as DVI evidence.
