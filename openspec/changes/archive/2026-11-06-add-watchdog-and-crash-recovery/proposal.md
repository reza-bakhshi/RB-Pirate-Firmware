# Proposal

## Why

A stalled core, an unhandled exception, or an assertion failure currently leaves the device permanently frozen or blinking in an infinite loop, requiring manual power cycling and losing all diagnostic context. As we transition to a multi-core service architecture, the system needs deterministic crash recovery that resets the device, saves hardware fault registers to persistent RAM, and reports the root cause on reboot.

## What Changes

- **RP2350 Hardware Watchdog**: Configure the RP2350 hardware watchdog (2–5 second timeout) fed only when both Core 0 and Core 1 advance their heartbeats, with `pause_on_debug = true` so hardware debuggers (SWD/GDB) do not trigger spurious resets.
- **Persistent Crash Record**: Retain a `bp_fault_record_t` structure in uninitialized RAM (`.uninitialized_data` / `.noinit`) across warm resets, validated by a magic word and CRC32.
- **Cortex-M33 HardFault Handler**: Replace the infinite blink loop with register capture (`CFSR`, `HFSR`, `BFAR`, `MMFAR`, `PC`, `LR`, `SP`) into the crash record followed by a controlled software reset.
- **Assert & Panic Interception**: Record source file, line number, and message in the crash record before resetting.
- **Boot Crash Report & Safe Mode**: Check and display the crash record on startup; enter safe mode (disabling autoscripts and default peripheral power) after repeated reboot cycles.
- **Core 1 Printf Hazard Elimination**: Remove `printf()` calls from Core 1 storage hotplug (`storage_detect()`) and MSC error callbacks (`msc_disk.c`), routing diagnostic events safely so routine operations do not panic or reset.
- **Diagnostic Commands**: Add `fault` command to view or clear crash history, and extend `health` with watchdog and heartbeat counters.
- **Stack Margin**: Increase stack sizes to 8 KB per core (`PICO_STACK_SIZE=8192`, `PICO_CORE1_STACK_SIZE=8192`) with stack painting to monitor high-water usage.

## Capabilities

### New Capabilities
- `fault-recovery`: Hardware watchdog supervision, dual-core heartbeat monitoring, HardFault register capture, crash record persistence across reset in uninitialized RAM, and post-reset crash diagnosis on the RP2350.

### Modified Capabilities
<!-- None -->

## Impact

- **Affected Systems**:
  - `src/pirate/hardfault.c`: Register capture and reset sequence.
  - `src/pirate.c`: Watchdog feeding loop and boot-time crash check.
  - `src/pirate/storage.c` & `src/msc_disk.c`: Core 1 `printf` removal.
  - `src/commands/global/`: Addition of `fault` command and update to `health`.
  - Linker script and build configuration: 8 KB stacks and `.uninitialized_data` section preservation.
- **API Additions**:
  - `bp_watchdog_init()`, `bp_watchdog_feed(core_id)`.
  - `bp_crash_record_t`, `bp_crash_get_last()`, `bp_crash_clear()`.
