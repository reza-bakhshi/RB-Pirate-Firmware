# Design: Split Control and Protocol Cores

## Context & Problem Statement

In the Bus Pirate 5 firmware baseline, mode switching and protocol execution suffer from three architectural shortcomings:
1. **Lack of Lifecycle Transactionality**: The legacy mode manager calls the new mode's interactive setup prompts (`protocol_setup()`) while the previous mode is still running. If the user cancels the prompt or an error occurs, the hardware is left half-configured. Furthermore, if `protocol_setup_exc()` fails, no rollback to HiZ occurs, leaving pins driven and peripherals asserted.
2. **Resource Leakage Across Mode Changes**: Because modes claimed hardware directly from the Pico SDK (`pio_claim_unused_sm`, `dma_claim_unused_channel`) without owner tracking, switching between modes repeatedly exhausted state machines and DMA channels.
3. **Core Ownership Inversion**: Protocol execution ran synchronously on Core 0 mixed with user input handling and syntax parsing, while Core 1 ran TinyUSB tasks and display updates. When long protocol captures or syntax executions stalled, the entire system became unresponsive.

Step B7 resolves these issues by introducing:
- A formal `bp_mode_t` lifecycle interface and compatibility adapter.
- A transactional `bp_mode_switch()` coordinator with atomic rollback to HiZ on setup failure.
- Mandatory lease release per mode (`BP_RES_OWNER_MODE_BASE + mode`).
- A protocol engine state tracker with heartbeat and crash recovery for the Core 1 data plane.

---

## Architecture & Components

```
┌─────────────────────────────────────────────────────────────────────────┐
│                           Core 0 — CTRL                                 │
│  Shell / Syntax Parser / UI Prompt                                      │
│      │                                                                  │
│      ▼                                                                  │
│  bp_mode_switch(target_mode)                                            │
│      │                                                                  │
│      ├─ 1. Stop current mode hardware (stop())                          │
│      ├─ 2. Release all mode leases (bp_res_release_all(OWNER_MODE + id))│
│      ├─ 3. Float all BIO pins to safe HiZ state (bio_init())            │
│      ├─ 4. Configure new mode (configure())                             │
│      ├─ 5. Start new mode hardware (start())                            │
│      │     └─ If failed: Rollback to HiZ, release partial leases        │
│      └─ 6. Post BP_EV_MODE_SWITCH to kernel event queue                │
└─────────────────────────────────┬───────────────────────────────────────┘
                                  │ IPC Event Queue (bp_event_t)
                                  ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                           Core 1 — PROTO                                │
│  Protocol Data Plane & Hardware Engines (PIO, DMA, IRQs)                │
│  bp_protocol_service():                                                 │
│      ├─ Process mode switch event (sync data plane state)               │
│      ├─ Execute active protocol job slices                              │
│      ├─ Record protocol heartbeat timestamp                             │
│      └─ Monitor protocol state (IDLE / ACTIVE / FAULT)                  │
└─────────────────────────────────────────────────────────────────────────┘
```

### 1. Unified Mode Descriptor (`bp_mode_t`)

```c
typedef struct bp_mode {
    uint8_t mode_id;
    const char *name;
    bp_status_t (*probe)(void);
    bp_status_t (*configure)(void);   // prompts / CLI arguments
    bp_status_t (*start)(void);       // hardware initialization
    void (*stop)(void);               // hardware teardown
    void (*deinit)(void);             // lease release
    void (*periodic)(uint32_t now_ms);// async data polling
    const struct bp_command_def *commands;
    uint16_t command_count;
} bp_mode_t;
```

### 2. Compatibility Adapter

To ensure 100% backward compatibility with existing modes in `src/mode/`:
- `modes[MAXPROTO]` structures are wrapped by `bp_mode_compat_adapter`:
  - `configure()` maps to `modes[i].protocol_setup()`.
  - `start()` maps to `modes[i].protocol_setup_exc()`.
  - `stop()` maps to `modes[i].protocol_cleanup()`.
  - `periodic()` maps to `modes[i].protocol_periodic()`.
  - Mode commands and prompt names are preserved identically.

### 3. Transactional Switch & Rollback State Machine

The mode switch sequence enforces:
1. **Pre-flight Stop**: Old mode is halted. Its DMA and PIO interrupts are disabled.
2. **Resource Reclamation**: `bp_res_release_all(BP_RES_OWNER_MODE_BASE + current_mode)` reclaims every SM, DMA channel, and IRQ registered to the old mode.
3. **Pin Safety**: `bio_init()` sets all 8 buffered I/O pins to input/floating state.
4. **Configuration**: Target mode parameters are evaluated. If interactive prompts are cancelled by the user, mode switches back to HiZ.
5. **Hardware Activation**: `start()` is invoked. If it returns failure (`BP_STATUS_HARDWARE_ERROR` or `0`):
   - The partial mode's `stop()` is invoked immediately.
   - Any partial leases are reclaimed.
   - Safe HiZ mode (`modes[HIZ]`) is activated as a guaranteed fallback.
   - An error message is displayed to the user without any kernel panic.

### 4. Protocol Data Plane & Heartbeat (`bp_protocol`)

The protocol data plane manager (`src/kernel/bp_protocol.h`, `src/kernel/bp_protocol.c`):
- Maintains protocol state:
  ```c
  typedef enum bp_proto_state {
      BP_PROTO_STATE_IDLE    = 0,
      BP_PROTO_STATE_ACTIVE  = 1,
      BP_PROTO_STATE_FAULT   = 2,
  } bp_proto_state_t;
  ```
- Core 1 updates `bp_protocol_heartbeat()` during protocol execution.
- If Core 1 protocol hangs during a long operation or DMA transfer, Core 0 supervisory watchdog detects the stall, marks the engine as `BP_PROTO_STATE_FAULT`, cancels the active job, releases contested leases, and restores pin safety.

---

## Memory & Resource Limits

- **Static RAM**: Max 24 registered modes × 48 bytes = 1,152 bytes static RAM.
- **Dynamic Heap**: 0 bytes (`malloc`, `free`, `realloc`, `calloc` are strictly forbidden).
- **Core 0 & Core 1 Stacks**: Preserved at 4096 bytes per core (`SCRATCH_Y` and `SCRATCH_X`).
