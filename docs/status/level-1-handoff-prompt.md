# Level 1 Handoff Prompt

Copy everything between `BEGIN PROMPT` and `END PROMPT` into a new AI chat.

---

BEGIN PROMPT

You are continuing the RB-Pirate firmware project. Work in:

```text
/home/rez0x61/Projects/electronic/buspirate-firmware/BusPirate5-firmware
```

Git branch: `refactor/kernel`

## Mission

Start **Level 1 — Core runtime and multicore control/data split**.

Level 1 is the first execution level. It is a milestone made of several OpenSpec changes, not a
single task. Feature parity is now **Level 4** and must not run before the core split.

Level 1 order:

1. A1 `stabilize-terminal-output` — bootstrap safety.
2. A3 `add-watchdog-and-crash-recovery` — bootstrap safety.
3. B0 `adopt-static-memory-model` — no-heap policy, pools, arenas.
4. B1 `add-service-kernel` — the core runtime.
5. B2 `add-protocol-job-runtime`.
6. B3 `add-resource-lease-manager`.
7. B4 `route-terminal-io-through-control-core`.
8. B5 `move-storage-to-control-core`.
9. B6 `move-binary-modes-to-transport-queues`.
10. B7 `split-control-and-protocol-cores`.
11. B8 `split-config-and-ui-snapshot`.

Do not start Level 2 (reliability), Level 3 (command conversion) or Level 4 (feature parity) until
Level 1 is complete.

## Read these files first

1. `openspec/ROADMAP.md` — especially Level order, Phase A and Phase B.
2. `docs/architecture/multicore-process-model.md` — the actor/service model.
3. `docs/architecture/memory-model.md` — no-heap rule, pools, arenas, migration list.
3. `docs/architecture/rearchitecture-plan.md`.
4. `docs/status/current-state-2026-11-04.md`.
5. `openspec/config.yaml`.
6. `openspec/FEATURES.md` — for context only; feature parity is Level 4.
7. `openspec/HARDWARE.md`.
8. `openspec/changes/stabilize-terminal-output/` — A1, ready to implement.
9. `openspec/changes/audit-ported-feature-parity/` — Level 4; do not execute now.
10. `openspec/specs/resource-lifecycle/spec.md` — archived F2 contract.

Read the relevant source files before changing anything.

## Current state

- Branch `refactor/kernel`; working tree clean.
- F2 `fix-ported-feature-lifecycles` is archived; `resource-lifecycle` spec exists.
- Feature specs and checklists exist for all ported features, but feature parity is Level 4.
- Build passes:
  - `cmake --build build_rp2350 --parallel --target bus_pirate6`
  - FLASH ~970 KB, RAM ~308 KB.
- Host tests pass:
  - `tests/test_ir_protocols.sh`
  - `tests/test_subghz.sh`
  - `tests/test_spsc_queue.sh`
- `openspec validate --all --strict` passes.

## Runtime model to implement

Read `docs/architecture/multicore-process-model.md` in full. In short:

- Core 0 = CTRL: USB/terminal, shell, mode manager, job dispatcher, storage, config, monitor/PSU,
  watchdog.
- Core 1 = PROTO: protocol engine, capture/data plane, binary engines, PIO/DMA IRQs.
- Each subsystem is a **service/actor** with a state struct, mailbox and bounded `step()`.
- No cross-core function calls. Only messages, SPSC rings, buffer-pool tokens and seqlock snapshots.
- No blocking waits without timeout. Long operations are state machines.
- Cooperative scheduler; no FreeRTOS initially.

## First actions, in order

### 1. Verify baseline

```bash
git status --short
git branch --show-current
cmake --build build_rp2350 --parallel --target bus_pirate6
(cd tests && ./test_ir_protocols.sh && ./test_subghz.sh && ./test_spsc_queue.sh)
npx -y @fission-ai/openspec@latest validate --all --strict
```

Stop and report if the tree is dirty or the build/tests fail.

### 2. Start A1 — non-blocking terminal output

The A1 change already exists:

```text
openspec/changes/stabilize-terminal-output/
```

Read its proposal, spec, design and 23 tasks. Implement it before the kernel work because B4 depends
on a non-blocking terminal producer.

Acceptance:

- `tx_fifo_put()` never spins.
- `tx_fifo_wait_drain(timeout)` returns a status.
- USB disconnect/backpressure cannot freeze Core 0.
- `health` exposes dropped bytes/overflow/high-water.
- Build, host tests, and the paused-host script pass.
- Update `openspec/FEATURES.md` life-cycle notes if relevant.
- Commit and archive A1.

### 3. Start A3 — watchdog and crash recovery

Create:

```bash
npx -y @fission-ai/openspec@latest new change add-watchdog-and-crash-recovery
```

Scope:

- Target exclusively RP2350B (Cortex-M33). RP2040 is completely out of scope.
- 2–5 s hardware watchdog with `pause_on_debug = true` (`watchdog_enable(timeout_ms, 1)`) and debug bypass (`BP_WATCHDOG_DISABLE`).
- Remove Core 1 `printf` in `storage_detect()` and MSC callbacks (post to event log) to prevent spurious assertion resets.
- Per-core heartbeat; feed only when both advance.
- RP2350 Cortex-M33 HardFault register capture (CFSR, HFSR, BFAR, MMFAR, PC, LR, SP).
- Panic/assert record in `.uninitialized_data` / `.noinit` RAM section with magic word + CRC.
- Boot-time crash report and safe mode after repeated faults.
- 8 KB stacks per core (16 KB total) and high-water reporting.
- `fault` and `health` commands.

Build and test after each group. Commit and archive.

### 4. Start B0 — static memory model

Create:

```bash
npx -y @fission-ai/openspec@latest new change adopt-static-memory-model
```

Scope:

- No `malloc`/`calloc`/`realloc`/`free`/`new`/`delete` in new code.
- Static pools/arenas for services, jobs and protocol objects.
- FatFS `FF_USE_LFN=2`.
- FlatCC static builder/output buffer.
- SubGHz decoder/encoder fixed pool.
- LCDTAP placement new + static framebuffer arena.
- kilo/hx editor arena.
- CI grep guard and `memory` command.

### 5. Start B1 — service kernel

Create:

```bash
npx -y @fission-ai/openspec@latest new change add-service-kernel
```

Scope:

- `src/kernel/bp_event.*` — fixed-size event queue.
- `src/kernel/bp_timer.*` — 1 ms soft timers.
- `src/kernel/bp_work.*` — deferred work.
- `src/kernel/bp_kernel.*` — service scheduler and `bp_service()`.
- `src/kernel/bp_status.h`, `bp_log`, `bp_health`.
- Convert Core 0 and Core 1 main loops to services.
- Cancellation plumbing for Ctrl+C, button and USB disconnect.
- Host tests for queue, timers, cancellation.
- A test long-op that cancels within 100 ms.

Then B2, B3, B4–B8 in order. Each gets its own OpenSpec change, build, tests, validation, commit and
archive.

## Per-change workflow

1. `openspec new change <name>`.
2. Read the relevant source and the roadmap section.
3. Write `proposal.md`, delta spec, `design.md`, `tasks.md`.
4. Implement the smallest safe slice.
5. Build, run host tests, `openspec validate --all --strict`.
6. Update `FEATURES.md`/checklists only where the change touches feature behavior.
7. Commit.
8. Archive when all tasks are complete.

## Hard rules

- Do not start Level 2/3/4 during Level 1.
- Do not use FreeRTOS unless the service model proves insufficient.
- Do not add cross-core function calls.
- Do not move pointers between cores; copy into fixed messages or use buffer-pool tokens.
- Do not add blocking waits without a timeout.
- Do not add `malloc`, `calloc`, `realloc`, `free`, `new` or `delete` in new code. Use static pools/arenas.
- Do not add heap-using STL types (`std::vector`, `std::string`, etc.).
- Do not allocate from an ISR or across cores.
- Do not add `printf` to Core 1 once B4 lands.
- Do not call `tud_*` outside the Core 0 USB service.
- Do not call `f_*` outside the Core 0 storage service.
- Do not add global `pio_clear_instruction_memory()` / `pio_destroy()` to mode code.
- Do not call `psu_enable` / `psu_disable` directly from mode code; use `psucmd_*`.
- Preserve command syntax, translation keys, FlatBuffers schema, PIO timing and the RB-Pirate pin map.
- Every change must be independently reviewable and archivable.

## Level 1 exit criteria

- A1, A3 and B1–B8 are archived.
- Core 0 runs the control plane; Core 1 runs the protocol/data plane.
- No cross-core function calls; only messages, rings, buffer tokens and snapshots.
- Terminal, storage, binary transport, config and monitor have one owner each.
- The service scheduler and resource leases are live.
- Build and host tests pass.
- `openspec validate --all --strict` passes.
- A Level 1 completion report is written under `docs/status/`.

## First response expected from you

1. Confirm branch and clean tree.
2. Report baseline build/test/OpenSpec results.
3. Read the multicore process model.
4. Start A1 and show its task plan before editing code.
5. Implement A1 only, then build/test/commit/archive.

END PROMPT
