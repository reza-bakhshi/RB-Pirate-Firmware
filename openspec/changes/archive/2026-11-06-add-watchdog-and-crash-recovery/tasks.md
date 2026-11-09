# Tasks

## 1. Safety Prerequisites (Core 1 Printf Elimination)

- [x] 1.1 Replace `printf` in `src/pirate/storage.c` (`storage_detect`) with status flags and log events, and verify Core 1 no longer calls `printf` on SD card insertion or removal.
- [x] 1.2 Replace `printf` in `src/msc_disk.c` error callbacks with status counters and log events, and verify MSC read/write errors do not call `printf`.
- [x] 1.3 Add a grep-based build check confirming no `printf` calls remain in Core 1 storage or MSC execution paths.

## 2. Crash Record Structure & Persistent RAM

- [x] 2.1 Define `bp_crash_record_t` in `src/pirate/crash_record.h` and allocate it in section `.uninitialized_data` with magic number `0x42504352` and CRC32; verify the linker places it in RAM outside `.bss`.
- [x] 2.2 Implement CRC32 generation and validation functions in `src/pirate/crash_record.c` with host unit tests in `tests/test_crash_record.c`; verify `tests/test_crash_record.sh` passes.
- [x] 2.3 Implement assert and panic hooks in `src/debug_rtt.c` / `src/pirate/crash_record.c` to record file, line, and message into `bp_crash_record` before reset.

## 3. Cortex-M33 HardFault Handler

- [x] 3.1 Implement assembly trampoline in `src/pirate/hardfault.c` to detect MSP/PSP and pass stacked registers (`R0-R3`, `R12`, `LR`, `PC`, `xPSR`) and SCB registers (`CFSR`, `HFSR`, `MMFAR`, `BFAR`).
- [x] 3.2 Update `bp_hardfault_handler_c` in `src/pirate/hardfault.c` to populate `bp_crash_record` and trigger `watchdog_reboot()`.
- [x] 3.3 Add a fault-injection command option `fault --crash` (guarded by a debug build flag) to verify register capture and reboot behavior.

## 4. Hardware Watchdog & Heartbeat Supervisor

- [x] 4.1 Retain 4 KB dedicated stack sizes per core in `SCRATCH_X` / `SCRATCH_Y` to avoid bus contention with DMA/PIO and add stack watermark painting (`0x55AA55AA`).
- [x] 4.2 Implement dual-core heartbeat counters (`bp_heartbeat[0]`, `bp_heartbeat[1]`) and supervisor logic in `src/pirate/watchdog.c/.h`.
- [x] 4.3 Configure the RP2350 hardware watchdog (3000 ms timeout) with `pause_on_debug = true` (`watchdog_enable(3000, 1)`) and compile-time bypass `BP_WATCHDOG_DISABLE`.
- [x] 4.4 Add heartbeat update calls to Core 0 and Core 1 main loops in `src/pirate.c`; verify the running system feeds the watchdog reliably without resets.

## 5. Boot Report, Safe Mode & Diagnostic Commands

- [x] 5.1 Implement boot-time crash check in `src/pirate.c` to inspect `bp_crash_record` and print a diagnostic summary over the terminal.
- [x] 5.2 Implement consecutive crash tracking in persistent RAM; verify that 3 crashes without 30 seconds of clean uptime activate safe mode.
- [x] 5.3 Implement `fault` CLI command in `src/commands/global/fault.c` and register it in `src/commands.c` to display crash details and support clearing the record.
- [x] 5.4 Extend `health` command in `src/commands/global/health.c` to display watchdog status, heartbeat counters, and stack high-water usage.

## 6. Verification & Build

- [x] 6.1 Build target `bus_pirate6` on `build_rp2350` and record flash and RAM usage changes.
- [x] 6.2 Run the full host test suite (`test_terminal_tx.sh`, `test_spsc_queue.sh`, `test_ir_protocols.sh`, `test_subghz.sh`, and `test_crash_record.sh`); verify all pass.
- [x] 6.3 Run `npx @fission-ai/openspec validate --all --strict`; verify all checks pass.

## Workflow follow-up

- Review implementation with developer.
- Archive this change with `openspec archive add-watchdog-and-crash-recovery` once all tasks and verification pass.
- Proceed to B0 (`adopt-static-memory-model`).
