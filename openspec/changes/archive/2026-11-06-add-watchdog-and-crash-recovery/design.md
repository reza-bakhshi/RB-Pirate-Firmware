# Design

## Context

The RB-Pirate runs on a dual-core RP2350 (ARM Cortex-M33). Currently, any unhandled hardware exception (`hardfault.c`) or runtime panic enters an infinite blink loop that leaves the board completely unresponsive and erases volatile execution state. Furthermore, Core 1 periodically calls `storage_detect()` and TinyUSB MSC callbacks which invoke `printf()`, triggering `BP_ASSERT_CORE0()` and panicking.

To make the device self-healing and provide crash diagnostics, this change introduces hardware watchdog supervision, crash dump retention in persistent RAM, safe-mode boot gating, and removes Core 1 `printf` hazards.

## Goals / Non-Goals

**Goals:**
- Provide a 3000 ms hardware watchdog timer that triggers a clean reset if either core halts.
- Prevent SWD debugger lockups by pausing the watchdog timer when CPUs are halted (`pause_on_debug = true`).
- Capture ARM Cortex-M33 fault registers (`CFSR`, `HFSR`, `BFAR`, `MMFAR`, `PC`, `LR`, `SP`) and assertion metadata into a persistent crash record in uninitialized RAM.
- Display last-crash diagnostics on boot and provide CLI commands (`fault`, `health`) to inspect or clear the record.
- Enable automatic safe mode if the device crashes repeatedly within a short window.
- Replace `printf()` calls on Core 1 in `storage.c` and `msc_disk.c` with safe diagnostic logging.
- Increase Core 0 and Core 1 stack sizes to 8 KB each with watermark painting.

**Non-Goals:**
- Independent single-core restart (restarting only Core 1 is out of scope until the full core split in B7).
- Persistent flash-backed crash logging (crash dumps are kept in SRAM across warm resets).
- Preemptive RTOS integration (the watchdog supervises the existing cooperative architecture).

## Decisions

### 1. Dual-Core Heartbeat Scheme with Core 0 Supervisor
- **Approach**: Core 0 and Core 1 each increment an atomic uint32 heartbeat counter (`bp_heartbeat[0]` and `bp_heartbeat[1]`) at every pass through their main loops. Core 0 acts as the watchdog supervisor: every ~500 ms, it verifies that *both* counters have advanced since the last check before calling `watchdog_update()`.
- **Alternative considered**: Having both cores write to the hardware watchdog independently.
  * *Rejected*: Independent feeding creates a race condition where Core 0 could stay alive and feed the hardware even if Core 1 is permanently frozen (or vice versa).

### 2. Cortex-M33 HardFault Trampoline & Register Extraction
- **Approach**: The HardFault vector routes to a brief assembly trampoline that determines whether `MSP` or `PSP` was active when the fault occurred, loads the stack pointer into `r0`, and branches to a C handler `bp_hardfault_handler_c(uint32_t *sp)`. The C handler reads the stacked registers (`R0-R3`, `R12`, `LR`, `PC`, `xPSR`) and the System Control Block registers (`CFSR`, `HFSR`, `MMFAR`, `BFAR`), populates `bp_crash_record`, and triggers `watchdog_reboot()`.
- **Alternative considered**: Plain C handler without assembly trampoline.
  * *Rejected*: Standard C function prologues push registers and corrupt the stack frame offset, making stacked `PC` and `LR` unreliable.

### 3. Crash Record Persistence in `.uninitialized_data`
- **Approach**: The crash structure `bp_crash_record_t` is allocated with `__attribute__((section(".uninitialized_data")))`. A 32-bit magic word (`0x42504352` / "BPCR") and a CRC32 checksum validate the contents across reboot. On cold power-on, the CRC will not match, initializing a clean state. On warm reset after a crash, the record is recognized, displayed, and retained until cleared.
- **Alternative considered**: Writing crash dumps to SPI flash.
  * *Rejected*: Erasing and writing SPI flash from inside a HardFault handler is hazardous and can fail if the fault was caused by a bus conflict or SPI controller error.

### 4. Safe Mode Threshold
- **Approach**: A persistent boot counter tracks consecutive crashes. If 3 consecutive crashes occur without at least 30 seconds of clean runtime, the firmware boots into "Safe Mode" (`system_config.safe_mode = true`). In Safe Mode:
  - Startup scripts (`config.bp`) are skipped.
  - Programmable power supply (VOUT) and pull-ups remain disabled.
  - Mode defaults to HiZ.
- **Alternative considered**: Prompting the user over USB before continuing boot.
  * *Rejected*: Headless operations or automated test setups would hang waiting for user input.

### 5. Elimination of Core 1 Printf Hazards
- **Approach**: Replace `printf()` calls in `src/pirate/storage.c` (`storage_detect`) and `src/msc_disk.c` with a compact lock-free logging ring or status flags consumed by Core 0. Core 1 updates hotplug and disk status via shared atomic variables; Core 0 formats and outputs any user-facing notifications.
- **Rationale**: `printf` calls `tx_fifo_put()` which contains `BP_ASSERT_CORE0()`. Running it on Core 1 panics immediately.

### 6. Stack Sizing & Painting
- **Approach**: Set `PICO_STACK_SIZE=8192` and `PICO_CORE1_STACK_SIZE=8192` in CMake. At early boot (before multi-core start), fill the unused portions of both stacks with `0x55AA55AA`. Diagnostic functions measure how many marker words were overwritten to report peak stack utilization.

## Risks / Trade-offs

- **[Risk] SWD breakpoints trigger watchdog resets during development**
  $\to$ **Mitigation**: Initialize with `watchdog_enable(3000, 1)` (`pause_on_debug = true`) and add `BP_WATCHDOG_DISABLE` build flag for development builds.
- **[Risk] Long disk/flash operations (e.g., formatting SD card) stall the loop**
  $\to$ **Mitigation**: Place bounded `watchdog_update()` calls inside long-running file and formatting loops.
- **[Risk] Fault handler encounters another fault (double fault)**
  $\to$ **Mitigation**: Keep the fault handler strictly static with no dynamic calls or printf; populate the struct and immediately trigger software reset.
