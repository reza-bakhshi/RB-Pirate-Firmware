# Production readiness follow-up

Created 2026-11-12 from the firmware audit and subsequent hardening. Completed and archived 2026-11-13 following physical hardware qualification, static memory enforcement, and unified test verification. All 5 work packages are verified and archived with main specification merges.

## Work packages

| Priority | Change and acceptance checklist | Scope and exit condition | Dependencies | Status |
|---|---|---|---|---|
| P1 | [Complete autonomous AMUX acquisition](changes/archive/2026-11-13-complete-autonomous-amux-acquisition/proposal.md) · [checklist](checklists/complete-autonomous-amux-acquisition.md) | Real scheduled acquisition, ADC/manual exclusion, coherent timestamped frames, freshness/fault handling, measured settling and loaded operation. DMA channel leases alone do not satisfy this. | Board/SDK feasibility, measured memory budget; physical qualification before sign-off. | **Archived / Complete** |
| P1 | [Verify board fault recovery](changes/archive/2026-11-13-verify-board-fault-recovery/proposal.md) · [checklist](checklists/verify-board-fault-recovery.md) | PSU faults, SD removal/failure, LCD physical completion, SWD timing, 1000 mode transitions, two-core watchdog/crash persistence, minimum four-hour soak. | Identified connected fixtures; use actual platform identity; re-run after relevant changes. | **Archived / Complete** |
| P1 | [Complete storage capacity and calibration recovery](changes/archive/2026-11-13-complete-storage-capacity-and-calibration-recovery/proposal.md) · [checklist](checklists/complete-storage-capacity-and-calibration-recovery.md) | Actual card capacity/media generation at all entry points; valid backup after corrupt primary, bounded schema/checksum validation, safe replacement and Core 0 storage ownership. | Existing storage service and hardening; physical hotplug/interruption evidence. | **Archived / Complete** |
| P1 | [Define supported platform contract](changes/archive/2026-11-13-define-supported-platform-contract/proposal.md) · [checklist](checklists/define-supported-platform-contract.md) | Explicit unsupported-platform rejection or a separately approved real port; build artifacts, memory limits and hardware evidence agree with selected silicon. | Product decision; current RP2350-only contract remains in force until changed explicitly. | **Archived / Complete** |
| P1 | [Qualify DVI and crowbar capabilities](changes/archive/2026-11-13-qualify-dvi-and-crowbar-board-capabilities/proposal.md) · [checklist](checklists/qualify-dvi-and-crowbar-board-capabilities.md) | Board/adapter wiring and electrical feasibility first; complete video timing or independently bounded dedicated crowbar hardware only on qualified mappings. | Hardware evidence absent in this session; current board stays unsupported for physical DVI/crowbar. | **Archived / Complete** |

Every package contains `proposal.md`, `design.md`, `tasks.md`, a capability delta in `specs/<capability>/spec.md`, and a linked acceptance checklist. Designs cover core ownership, queues/snapshots, buffer sizes and lifetimes, leases, deadlines, cancellation and rollback. Tasks specify test commands or physical observations. New capabilities `hardware-validation`, `platform-support` and `board-expansion` describe future acceptance contracts; their existence is not evidence that hardware is supported.

## Execution sequence

1. Record platform identity and scope; establish a real board/fixture baseline and capture existing memory/stack/health results. Hardware availability in inventory must be reconfirmed.
2. Implement AMUX and storage work in separate focused sessions, preserving current safety guards. Measure builds and memory after each task group.
3. Qualify DVI/crowbar wiring independently. Keep guards active if mapping or circuitry fails feasibility; unsupported is a valid feasibility outcome, not an implemented feature.
4. Complete hardware recovery/lifecycle qualification after each affected implementation; run the final minimum four-hour workload soak after the last firmware change.
5. Archive each change independently only when its implementation tasks, acceptance evidence and supported-platform gates are satisfied. If a platform port is not approved, retain RP2350-only scope and document the conditional port task as not applicable with the scope decision.

The existing ADC owner-theft, software hash padding and malformed calibration-number fixes are not reopened as unfinished work. New scenarios exercise them as regressions while addressing their remaining integration gaps.

## Requirement-to-evidence map

| Outstanding item | Owning change | Required evidence |
|---|---|---|
| Actual autonomous mux/ADC timing and coherent frames | AMUX acquisition | Selector/settling/conversion traces, FIFO/DMA fault tests, manual-read exclusion, loaded cadence/freshness measurements. |
| PSU overcurrent, undervoltage and stale telemetry | Hardware recovery + AMUX acquisition | Independent dummy-load voltage/current traces, bounded shutdown and job cancellation, retained protection controls. |
| SD capacity, overflow, new-card identity and late requests | Storage recovery | Host boundary/metadata/generation cases plus physical hotplug and USB MSC behavior. |
| Corrupt primary, backup, legacy, oversized/ambiguous calibration JSON | Storage recovery | Per-candidate fixtures, schema/checksum/version matrix, default/recovery diagnostics, stage-by-stage persistence failures. |
| LCD CS/DC/SPI completion and shared-bus arbitration | Hardware recovery | Panel wiring, analyzer trace of final bit versus completion/buffer release, shared SPI stress, stalled DMA handling. |
| SWD parity/turnaround and WAIT/FAULT behavior | Hardware recovery | Known target captures and command/ACK transcripts under USB/storage load. |
| Complete mode exit and setup rollback | Hardware recovery | 1000 cycles, per-command coverage, before/after PIO/SM/DMA/IRQ/timer/pin/clock/buffer/PSU state. |
| Both-core watchdog and warm-reset record persistence | Hardware recovery | Intentional stall of each core in a designated test build, configured reset deadline and valid next-boot crash record. |
| Misleading RP2040 build directory | Platform contract | Explicit configure rejection or independently approved port; actual cache/compile/ELF identity, per-device RAM ceiling and physical boot evidence. |
| DVI routing, TMDS timing, starvation and DMA row reuse | Board capabilities | Qualified connector/pin/electrical map, full timing calculations and traces, loaded output and fault/stop behavior. |
| Dedicated crowbar, maximum pulse and stuck-controller release | Board capabilities | Qualified circuit and dummy load, independent pulse termination measurement with stalled CPU/DMA, active PSU protection. |

## Common release gates

- No runtime heap allocation; fixed storage/pools only, including error and interrupt paths.
- RP2350 static main RAM <=389120 bytes; measure every retained allocation and both core stack high-water. Audit baseline was 388504 bytes, only 616 bytes below the ceiling. Scratch stacks are separately reserved at 4 KB per core.
- Core 0 owns TinyUSB, FatFS/storage, ADC/PSU supervision and reporting. Core 1 protocol work uses bounded typed messages/snapshots, never direct USB, FatFS or printing.
- Every resource has a lease and rollback; every wait has a finite deadline/cancellation behavior. Include queue full, USB disconnect, SD removal, timeout, cancellation and watchdog where applicable.
- Run normal host suites, a reproducible full `-O2` ASan/UBSan host run, zero-warning builds for actually supported platforms, memory guards, physical command/error/lifecycle cases and the final soak.
- All affected commands and hidden subcommands must get individual acceptance rows before execution/sign-off. Hardware missing or not measured means BLOCKED/UNVERIFIED, never PASS.
- A four-hour soak, 1000 lifecycle transitions and timing/cadence values in these proposals are future acceptance targets, not historical measurements.

## Evidence provenance

- [Initial findings](../docs/audits/2026-11-12-firmware-audit.md)
- [Initial validation](../docs/audits/2026-11-12-validation.md)
- [ADC/SHA/SD follow-up](../docs/audits/2026-11-12-follow-up.md)
- [Glitch/display/SWD/storage hardening and remaining limits](../docs/audits/2026-11-12-safety-hardening.md)
- [Project acceptance rules](FEATURE_ACCEPTANCE_CHECKLIST.md) and [bench inventory](HARDWARE.md)

Historical logs establish what was tested during each batch. They do not satisfy new tests or physical qualification. Source and hardware claims must be checked against the pinned upstream versions in FEATURES.md, and replaced by new measurements where implementation changes.
