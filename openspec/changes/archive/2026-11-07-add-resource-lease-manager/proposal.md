# Proposal: Hardware Resource Lease Manager

## Why

In the existing firmware architecture, hardware peripherals (PIO state machines, DMA channels, IRQ handlers, communication controllers, and the 128 KB big buffer) are claimed via ad-hoc, untracked mechanisms (`pio_claim_unused_sm(..., true)`, `dma_claim_unused_channel(true)`, and `mem_alloc()`). This creates critical stability vulnerabilities:
1. When resources are contested or exhausted, pico-sdk `true` claims call `panic()` or `hard_assert()`, hanging the device instead of reporting a clean error.
2. Modes that exit on errors or switch to other modes can leak PIO state machines, DMA channels, and IRQ handlers.
3. Legacy mode code has historically invoked destructive global resets (such as `pio_clear_instruction_memory()` or `pio_destroy()`) which clobbered state machines and instruction memories belonging to other subsystems (e.g. logic analyzer or status LEDs).
4. The 128 KB big buffer allocator in `src/pirate/mem.c` relies on an unsynchronized single boolean, invokes `printf()` inside allocator logic, and provides no transactional teardown on mode exit.

A unified resource lease manager is required to enforce explicit ownership, non-blocking acquisition, transactional rollback (`bp_res_release_all(owner)`), and complete diagnostic observability.

## What Changes

- **Resource Lease Manager Subsystem (`src/kernel/bp_resource.h`, `src/kernel/bp_resource.c`)**:
  - Define resource classes: `BP_RES_PIO_SM`, `BP_RES_DMA_CH`, `BP_RES_IRQ`, `BP_RES_PERIPH_HW`, and `BP_RES_BIG_BUFFER`.
  - Static resource tracking table covering 12 PIO state machines (PIO0, PIO1, PIO2), 16 DMA channels, hardware IRQ vectors, peripheral blocks, and the 128 KB buffer.
  - Implement non-panicking lease acquisition (`bp_res_acquire`, `bp_res_claim_unused`) returning `bp_status_t` (`BP_STATUS_OK`, `BP_STATUS_ERR_RESOURCE`, `BP_STATUS_ERR_INVALID`).
  - Implement transactional release (`bp_res_release`, `bp_res_release_all(owner_id)`).
- **Big Buffer Lease Arena Integration**:
  - Refactor `src/pirate/mem.c` to use `bp_resource` for big-buffer leasing.
  - Eliminate `printf()` inside `mem_alloc()`, ensuring thread-safety and returning `NULL` cleanly on conflict.
- **Resource Teardown in Subsystems**:
  - Eliminate hardcoded panic asserts in DMA and PIO acquisition (`dma_claim_unused_channel(false)`, `pio_claim_unused_sm(..., false)`).
  - Ensure mode teardowns and error rollbacks invoke `bp_res_release_all()`.
- **Diagnostic CLI Command `res` (`src/commands/global/cmd_res.h/.c`)**:
  - Add interactive `res` command showing the status, owner ID, and lease name of every hardware resource.
- **Automated Verification**:
  - Host unit tests (`tests/test_resource.c`, `tests/test_resource.sh`) testing acquisition, conflict rejection, transactional `release_all()`, big buffer leases, and mode-switch soak simulation with ASan/UBSan.

## Capabilities

### New Capabilities
- `resource-ownership`: Deterministic, non-panicking lease manager for hardware resources (PIO state machines, DMA channels, IRQ vectors, peripherals, big buffer) with transactional teardown and zero dynamic memory allocation.

### Modified Capabilities
<!-- None -->

## Impact

- **Affected Code**:
  - `src/kernel/bp_resource.h`, `src/kernel/bp_resource.c`: Core lease manager.
  - `src/pirate/mem.c`, `src/pirate/mem.h`: Thread-safe big buffer integration.
  - `src/commands/global/cmd_res.h`, `src/commands/global/cmd_res.c`: `res` CLI command.
  - `src/commands.c`, `src/commands.h`: Command table registration.
  - `src/CMakeLists.txt`: Build target inclusion.
- **Memory & Resource Budget**:
  - Zero dynamic heap allocation (`malloc`, `free`, `new`, `delete`).
  - All tracking tables statically allocated in `.bss` / `.data` well within the RP2350 <= 380 KB static RAM budget.
