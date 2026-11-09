# Design: Hardware Resource Lease Manager

## Context

On RP2350B (`bus_pirate6`), hardware resources are shared across the application layer, protocol modes, background capture engines (logic analyzer, FALA), and system peripherals (RGB status LED, debug UART, storage SPI/SDIO). Previously, components acquired PIO state machines, DMA channels, and IRQ handlers by calling SDK functions with `required = true` (which panic on failure) or by writing directly to hardware registers. Additionally, mode exits frequently leaked resources or executed destructive global clears (`pio_destroy()`) that interfered with other subsystems.

Step B3 establishes the `resource-ownership` subsystem to mediate access to all on-chip hardware blocks through explicit leases with deterministic error checking, transactional rollback, and zero dynamic heap memory allocation.

## Goals / Non-Goals

**Goals:**
- Provide a static, thread-safe resource lease registry for RP2350 hardware resources:
  - 12 PIO State Machines (PIO0 SM0–3, PIO1 SM0–3, PIO2 SM0–3).
  - 16 DMA Channels (0–15).
  - Hardware IRQ lines (PIO, DMA, UART, SPI, I2C, etc.).
  - Hardware controllers (UART0/1, SPI0/1, I2C0/1).
  - 128 KB Big Buffer arena.
- Support non-panicking lease acquisition (`bp_res_acquire`, `bp_res_claim_unused`) returning `bp_status_t` (`BP_STATUS_OK`, `BP_STATUS_ERR_RESOURCE`, `BP_STATUS_ERR_INVALID`).
- Support transactional teardown (`bp_res_release_all(owner_id)`) to release all leases held by a given owner on mode exit or setup failure.
- Convert `src/pirate/mem.c` to thread-safe named leases and remove `printf()` from allocator failure paths.
- Add an interactive diagnostic command `res` to inspect lease tables.
- Pass unit tests validating lease acquisition, conflict rejection, and transactional rollback across 1000 simulated mode transitions without memory leaks.

**Non-Goals:**
- Full mode lifecycle migration (deferred to B7 `split-control-and-protocol-cores`).
- Pin multiplexing dynamic matrix (pin manager integration expands in B7/B8).
- Modifying PIO program bytecode or low-level protocol timing.

## Decisions

### 1. Statically Budgeted Resource Registry
- **Decision**: Store all resource states in fixed-size arrays in `.bss`, with zero dynamic heap allocation:
  ```c
  typedef enum bp_res_class {
      BP_RES_CLASS_PIO_SM = 0,
      BP_RES_CLASS_DMA_CH,
      BP_RES_CLASS_IRQ,
      BP_RES_CLASS_PERIPH,
      BP_RES_CLASS_BIG_BUFFER,
      BP_RES_CLASS_COUNT,
  } bp_res_class_t;

  typedef struct bp_res_entry {
      uint32_t owner_id;
      char     owner_name[16];
      bool     acquired;
  } bp_res_entry_t;
  ```
- **Rationale**: Eliminates dynamic memory allocation, conforms to the static memory model (ARCH-060, capability `memory-model`), and ensures predictable lookup times without pointer chasing.

### 2. Transactional Owner-Based Cleanup (`bp_res_release_all`)
- **Decision**: Every lease records an `owner_id` (e.g. `BP_RES_OWNER_MODE_BASE + mode_id`). When a mode exits or fails during configuration, `bp_res_release_all(owner_id)` iterates through all resource classes and safely releases any leases held by that owner, unclaiming state machines and disabling IRQs.
- **Rationale**: Eliminates the class of resource-leak bugs where an error exit path forgets to clean up a partially allocated DMA channel or PIO state machine.

### 3. Non-Panicking Claims Over Pico-SDK Hardware Blocks
- **Decision**: Wrap pico-sdk `pio_claim_unused_sm(pio, false)` and `dma_claim_unused_channel(false)`. If hardware resources are exhausted, return `BP_STATUS_ERR_RESOURCE` instead of calling `panic()`.
- **Rationale**: Firmware must remain resilient and report human-readable errors to the terminal instead of triggering hardware watchdog resets.

### 4. Big Buffer Integration Without `printf()`
- **Decision**: `mem_alloc()` and `mem_free()` in `src/pirate/mem.c` delegate to `bp_res_acquire_buffer()` and `bp_res_release_buffer()`. Allocator failure returns `NULL` silently and updates the error counter instead of printing to the console, preventing cross-core I/O deadlocks.

### 5. Diagnostic CLI Command `res`
- **Decision**: Register `res` command (`src/commands/global/cmd_res.c`) displaying a tabular overview of all PIO, DMA, IRQ, and Buffer allocations.
- **Rationale**: Provides developers and automated verification tools with real-time inspection of hardware resource ownership.

## Risks / Trade-offs

- **[Risk] Legacy Unregistered Claims**: If a legacy library (e.g., third-party picorvd or arduino-ch32) claims an SM directly via `pio_claim_sm_mask` without the lease manager, the resource manager might attempt to lease that same SM.
  - *Mitigation*: The lease manager checks both its internal registry AND pico-sdk hardware claim status (`pio_sm_is_claimed()`, `dma_channel_is_claimed()`) during acquisition.
- **[Risk] Inter-Core Concurrency**: Leases might be requested or released concurrently across cores.
  - *Mitigation*: Resource registry mutations are guarded with spinlocks / critical sections (`save_and_disable_interrupts()`).

## Migration Plan

1. Implement `src/kernel/bp_resource.h` and `src/kernel/bp_resource.c`.
2. Refactor `src/pirate/mem.c` to use `bp_resource` for big-buffer management.
3. Integrate `bp_res_init()` into system startup (`src/pirate.c`).
4. Implement host unit tests `tests/test_resource.c` and `tests/test_resource.sh`.
5. Add `res` command in `src/commands/global/cmd_res.h/.c` and register in `src/commands.c`.
6. Verify static RAM consumption and build RP2350 target (`bus_pirate6.uf2`).
