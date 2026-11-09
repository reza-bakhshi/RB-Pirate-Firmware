# Proposal: Split Control and Protocol Cores

## Why

In the baseline firmware architecture, mode switching and protocol executions lack transactional safety, resource boundary enforcement, and cross-core isolation:
1. **Unsafe Mode Transitions**: When switching modes, `protocol_setup()` on the target mode is called before the active mode is cleaned up, leading to resource collisions (claimed PIO state machines, DMA channels, and IRQ vectors) and inconsistent peripheral states if setup is cancelled or fails.
2. **Missing Rollback on Setup Failure**: If a mode's hardware setup fails (`protocol_setup_exc() == 0`), the system leaves the hardware partially configured or corrupted, rather than rolling back to a known-safe HiZ state and releasing all acquired leases.
3. **Core Ownership Ambiguity**: Protocol engines execute directly on Core 0 while TinyUSB tasks execute on Core 1, but Core 0 also performs direct TinyUSB and SPI hardware accesses. To achieve the target architecture (Core 0 as Control/UI/Shell plane and Core 1 as Hardware Data/Protocol plane), protocol mode execution and lifecycle management must be formalized behind a unified mode descriptor interface (`bp_mode_t`), with transactional mode switching (`bp_mode_switch`), protocol heartbeat tracking, and crash recovery.

Step B7 introduces `bp_mode_t`, transactional `bp_mode_switch()` with automatic rollback to HiZ, full peripheral lease tracking per mode (`BP_RES_OWNER_MODE_BASE + mode`), Core 1 protocol heartbeat monitoring, and safe inter-core mode RPC.

## What Changes

- **Mode Lifecycle Interface and Compatibility Adapter (`src/mode/bp_mode.h`, `src/mode/bp_mode.c`)**:
  - Define `bp_mode_t` lifecycle interface (`probe`, `configure`, `start`, `stop`, `deinit`, `periodic`).
  - Implement universal compatibility adapter wrapping all existing `modes[MAXPROTO]` structures.
  - Implement `bp_mode_switch(uint8_t new_mode)`:
    - Stop old mode and release all acquired peripheral leases (`bp_res_release_all(BP_RES_OWNER_MODE_BASE + current_mode)`).
    - Reset all BIO pins to floating HiZ safe state (`bio_init()`).
    - Configure new mode parameters.
    - Start new mode hardware.
    - Automatic rollback to HiZ (`modes[HIZ]`) on any initialization failure without crashing or leaking resources.
  - Enforce cleanup-before-setup globally across the codebase (updating `ui_mode.c`, `binmodes.c`, `bpio.c`, and `binio_helpers.c`).
- **Core 1 Protocol Data Plane & Heartbeat (`src/kernel/bp_protocol.h`, `src/kernel/bp_protocol.c`)**:
  - Track protocol engine execution state (`BP_PROTO_STATE_IDLE`, `BP_PROTO_STATE_ACTIVE`, `BP_PROTO_STATE_FAULT`).
  - Provide protocol engine heartbeat recording and watchdog supervisor tracking.
  - Handle protocol crash/stall detection and recovery: if Core 1 protocol hangs, release leases, reset pins, and recover to HiZ without freezing the terminal.
- **Inter-Core Mode RPC**:
  - Post `BP_EV_MODE_SWITCH` kernel events across cores so Core 1 data plane and toolbars coordinate cleanly on mode transitions.
- **Verification and Test Suite (`tests/test_mode.c`, `tests/test_mode.sh`)**:
  - Host test suite verifying `bp_mode_t` registration, `bp_mode_switch()` transitions, transactional rollback on hardware error, and a 1000-iteration soak test asserting zero leaked resource leases under ASan & UBSan.

## Capabilities

### New Capabilities
- `core-separation`: Formalizes control-plane / protocol-data-plane separation, protocol heartbeat tracking, and fault recovery.

### Modified Capabilities
- `base-modes`: Requires strict cleanup-before-setup sequencing, transactional mode switching via `bp_mode_switch()`, automatic rollback to HiZ on setup failure, and complete lease release.

## Impact

- `src/mode/bp_mode.h`, `src/mode/bp_mode.c`: New mode lifecycle manager and adapter.
- `src/kernel/bp_protocol.h`, `src/kernel/bp_protocol.c`: Protocol data plane tracker and heartbeat.
- `src/ui/ui_mode.c`: Adopt `bp_mode_switch()`.
- `src/binmode/binmodes.c`, `src/binmode/bpio.c`, `src/binmode/binio_helpers.c`: Fix cleanup-before-setup ordering.
- `tests/test_mode.c`, `tests/test_mode.sh`: Automated verification.
