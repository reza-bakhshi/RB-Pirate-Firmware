# Proposal

## Why

RB-Pirate combines code from Bus Pirate upstream, ESP32-Bit-Pirate, Flipper Zero and several
vendor drivers. Some features are complete, some are partial ports, some are only host-tested, and
some are advertised but not working on hardware.

Before the architecture work starts, we need an evidence-based feature inventory. Otherwise a
rearchitecture can hide or preserve broken ports, and "all features work" becomes an untestable
claim.

This change is an audit only. It does not change firmware behavior.

## What Changes

- Pin the exact upstream commit for every source used by the fork.
- Build a feature matrix with implementation location, status, known gaps and verification method.
- Run a static lifecycle audit for every feature:
  - PIO programs, state machines, DMA channels and IRQ handlers
  - clock changes
  - pin mux and debug/terminal UART conflicts
  - PSU and storage ownership
  - buffer allocation and failure handling
- Run the existing host tests and record what they actually cover.
- Compare key ported files against the pinned upstream versions.
- Define a hardware verification checklist for each feature.
- Record every defect in `openspec/FEATURES.md`.
- Create targeted fix-change proposals (F2-F10) from the findings.

## Capabilities

### New Capabilities

None. This is a no-behavior-change audit; `skip_specs: true` is set in `.openspec.yaml`.

### Modified Capabilities

None.

## Impact

- `openspec/FEATURES.md` — source pins, feature matrix, defects, verification status.
- `openspec/ROADMAP.md` — add the Phase F feature-parity track before A1.
- New planned changes: `fix-ported-feature-lifecycles`, `fix-infrared-port-parity`,
  `fix-subghz-port-parity`, `fix-rfid-port-parity`, `fix-rf24-port-parity`,
  `fix-lcdtap-port-parity`, `fix-cell-port-parity`, `fix-ps2-and-usb-sniffer-parity`,
  `add-feature-conformance-tests`.
- No firmware source files are changed by this audit.
