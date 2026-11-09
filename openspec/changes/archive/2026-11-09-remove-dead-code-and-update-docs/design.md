# Design

## Context
See `proposal.md`. The 28-step rearchitecture of RB-Pirate transformed a monolithic firmware codebase with cross-core races, dynamic allocation, and unmanaged peripherals into a deterministic dual-core cooperative system. The final step consolidates architectural documentation so current and future maintainers have clear references.

## Goals / Non-Goals

**Goals:**
- Provide complete, accurate architecture guides covering dual-core RPC, USB buffering, kernel scheduler, mode lifecycles, and storage subsystem.
- Mark all 28 rearchitecture steps complete in `openspec/ROADMAP.md` and `openspec/FEATURES.md`.

**Non-Goals:**
- Introducing new runtime features or modifying wire protocols.

## Decisions

### Decision: Dedicated subsystem architecture guides
- Rather than a single unwieldy document, maintain targeted guides for `dual_core_guide.md`, `usb_communication_guide.md`, `kernel-guide.md`, `mode-lifecycle-guide.md`, and `storage-guide.md`.
