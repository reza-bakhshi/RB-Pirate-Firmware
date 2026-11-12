# Complete storage capacity and calibration recovery

## Why

Bounds checks reject arithmetic overflow but do not establish requests lie within the actual inserted card. Calibration recovers a missing primary, but a present corrupt primary still prevents valid backup recovery; plausible coefficient corruption lacks a persisted integrity check.

## What Changes

Core 0 storage service owns card capacity/media generation, FatFS handles, calibration parsing and durable replacement. Core 1 uses typed storage requests and receives bounded completion statuses.

Priority: **P1**. This is planned work, not an implementation or hardware verification claim. Existing source changes and passing host tests are recorded separately in [audit evidence](../../../docs/audits/2026-11-12-safety-hardening.md).

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `storage`: complete the remaining acceptance and fault behavior described by its delta requirements.

## Impact

Use existing storage SPI/DMA leases and block-device serialization. File handles and media-generation tokens remain owned by Core 0. Timed-out requests must not retain pointers to released worker buffers.

No device is energized or flashed by this proposal. The hardware inventory is a documented inventory, not proof of a connected fixture. The change must fill its [acceptance checklist](../../checklists/complete-storage-capacity-and-calibration-recovery.md) before archival. Dependencies and sequencing are in [PRODUCTION_READINESS.md](../../PRODUCTION_READINESS.md).

## Out of scope

FAT journaling guarantees, cryptographic authenticity without a key, repairing arbitrary filesystem damage, or treating SD-internal write caches as durable without evidence.
