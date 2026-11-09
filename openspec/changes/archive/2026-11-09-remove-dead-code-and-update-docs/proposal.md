# Proposal

## Why
With all five architectural levels (Bootstrap, Dual-Core Separation, Protocol Jobs, Feature Parity, Hardening) implemented, legacy dead code and obsolete shims should be cleaned up. Furthermore, the architecture documentation must accurately reflect the final split (Core 0 Control / Core 1 Protocol), the cooperative service kernel, storage RPC, memory model, and upstream divergence. Finally, the program roadmap and feature parity matrix must be marked 100% complete (28/28 steps).

## What Changes
- Update architecture documentation in `docs/architecture/`:
  - `docs/architecture/dual_core_guide.md`
  - `docs/architecture/usb_communication_guide.md`
  - `docs/architecture/kernel-guide.md`
  - `docs/architecture/mode-lifecycle-guide.md`
  - `docs/architecture/storage-guide.md`
  - `docs/architecture/upstream-divergence.md`
- Document Core 0 / Core 1 ownership invariants in `CLAUDE.md`.
- Update `docs/architecture/rearchitecture-todo.md` completing all items ARCH-001 through ARCH-709.
- Mark `openspec/ROADMAP.md` and `openspec/FEATURES.md` 100% complete (28/28 steps, Level 5 complete).

## Capabilities

### New Capabilities
*(None; `skip_specs: true`)*

### Modified Capabilities
*(None)*

## Impact
- Documentation files under `docs/architecture/` and repo root.
- OpenSpec program tracking files.
