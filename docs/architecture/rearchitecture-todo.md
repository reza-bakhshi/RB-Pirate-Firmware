# RB-Pirate Rearchitecture TODO

> Actionable checklist for [rearchitecture-plan.md](rearchitecture-plan.md).  
> Priority: **P0** = freeze/crash now, **P1** = required for the new architecture, **P2** = hardening/cleanup.  
> Status: `[ ]` todo, `[~]` in progress, `[x]` done.

---

## Phase 0 — Baseline and instrumentation

- [x] **ARCH-001 (P0)** Create branch `refactor/kernel` and tag the current build as `pre-rearch-baseline`.
- [x] **ARCH-002 (P0)** Fix `tests/test_spsc_queue.sh` to use `cd "$(dirname "$0")/.."` instead of `/home/ian/bp5fw`.
- [x] **ARCH-003 (P0)** Record baseline: `arm-none-eabi-size`, linker memory report, stack sizes, RAM/flash headroom.
- [x] **ARCH-004 (P0)** Add `bp_health` counters: `tx_dropped`, `tx_overflow`, `icm_timeouts`, `watchdog_resets`, `stack_high_water[2]`, `mode_switches`, `fault_count`.
- [x] **ARCH-005 (P0)** Add a `health` terminal command that prints the counters and resource table.
- [x] **ARCH-006 (P0)** Write reproduction scripts: host pauses output during `txtest`; USB yank during dump; SD insert/remove; 1000 mode switches; SUMP/BPIO stress.
- [x] **ARCH-007 (P1)** Add a simple serial test runner under `tools/hil/` (extend `tools/serial`).
- [x] **ARCH-008 (P1)** Document the baseline failure modes and how to reproduce each one.

**Exit:** every reported freeze has a repeatable test or a clear counter.

---

## Phase 1 — Stop the bleeding

### Terminal output

- [x] **ARCH-010 (P0)** Change `tx_fifo_put()` to non-blocking `spsc_queue_try_add()`; never spin in the producer.
- [x] **ARCH-011 (P0)** Add `tx_dropped`/`tx_overflow` counters and a one-time `[output dropped: N bytes]` marker.
- [x] **ARCH-012 (P0)** Change `tx_fifo_write()` to non-blocking chunking with a bounded retry.
- [x] **ARCH-013 (P0)** Add `tx_fifo_wait_drain(uint32_t timeout_ms)` returning `bool`; replace all unbounded uses.
- [x] **ARCH-014 (P0)** Change `printf` `_putchar()` to use the non-blocking path.
- [x] **ARCH-015 (P0)** Fix `tx_fifo_service()` ordering so disconnect/flush is handled even when `tud_cdc_n_write_available() < 64`.
- [x] **ARCH-016 (P0)** Make `txtest` respect a timeout and report dropped bytes instead of hanging.
- [x] **ARCH-017 (P1)** Add a `tx stats` command for queue level, high-water and drop count.

### Core 1 output and storage

- [x] **ARCH-020 (P0)** Remove all `printf` calls from `storage_detect()`, `storage_mount()`, `storage_unmount()` when called on Core 1.
- [x] **ARCH-021 (P0)** Move `storage_detect()` out of the Core 1 tick into a Core 0 storage service.
- [x] **ARCH-022 (P0)** Remove `printf` from `msc_disk.c` read/write error paths; post a log/event instead.
- [x] **ARCH-023 (P0)** Add a `bp_log_post()` fixed-record queue for Core 1.
- [x] **ARCH-024 (P0)** Audit Core 1 files for any remaining `printf`/`hard_assert`/`panic` calls.
- [x] **ARCH-025 (P1)** Add a `grep`-based CI guard: no `printf(` in Core 1-owned files.

### Inter-core

- [x] **ARCH-030 (P0)** Add a timeout to `icm_core0_send_message_synchronous()`; return `bool`/status.
- [x] **ARCH-031 (P0)** Make callers handle timeout without freezing (`ui_process_syntax`, toolbar update, RGB update).
- [x] **ARCH-032 (P0)** Add a fallback path when Core 1 does not acknowledge: log, disable the dependent feature, continue.
- [x] **ARCH-033 (P1)** Replace the FIFO handshake with the typed event queue in Phase 2.
- [x] **ARCH-034 (P1)** Add `icm_timeouts` counter and a `health` readout.

### ADC and monitor

- [x] **ARCH-040 (P0)** Fix `adc_busy_wait()`: `volatile` state, release inside the spinlock, owner tracking.
- [x] **ARCH-041 (P0)** Ensure no nested ADC acquisition on the same core.
- [x] **ARCH-042 (P1)** Move ADC/AMUX ownership to Core 1 and publish a seqlock snapshot.
- [x] **ARCH-043 (P1)** Update `system_monitor` to read the snapshot instead of shared arrays.

### Mode lifecycle

- [x] **ARCH-050 (P0)** Change `ui_mode_enable_args()` to stop/cleanup the old mode **before** configuring the new one.
- [x] **ARCH-051 (P0)** Add rollback: if `protocol_setup_exc()` fails, call the new mode's cleanup and return to HiZ.
- [x] **ARCH-052 (P0)** Ensure `setup()` prompts cannot leave the old mode half-configured.
- [x] **ARCH-053 (P1)** Add a `bp_mode_switch()` wrapper and migrate callers.

### Faults and watchdog

- [x] **ARCH-060 (P0)** Enable a 2-5 s hardware watchdog.
- [x] **ARCH-061 (P0)** Add per-core heartbeat counters; feed only when both advance.
- [x] **ARCH-062 (P0)** Capture HardFault registers and the last log records into no-init RAM.
- [x] **ARCH-063 (P0)** On HardFault: blink an error code, then reset (no infinite blink).
- [x] **ARCH-064 (P0)** On panic/assert: record file/line/reason, then controlled reset.
- [x] **ARCH-065 (P0)** Print the previous crash record on boot; enter safe mode after repeated faults.
- [x] **ARCH-066 (P1)** Add a `fault` terminal command to dump the record.
- [x] **ARCH-067 (P1)** Add a safe mode that disables storage, binary modes and RF/IR power.

### Stack and memory

- [x] **ARCH-070 (P0)** Increase Core 0 and Core 1 stacks to 8 KB.
- [x] **ARCH-071 (P0)** Add stack painting/high-water measurement and print it in `health`.
- [x] **ARCH-072 (P0)** Verify `SCRATCH_X`/`SCRATCH_Y` usage after the change.
- [x] **ARCH-073 (P1)** Set `FF_USE_LFN` to `2` (static LFN buffer) to remove per-operation malloc.
- [x] **ARCH-074 (P1)** Add a heap high-water/fragmentation counter.

### MSC bounds

- [x] **ARCH-080 (P0)** Bounds-check `msc_disk[lba]` fallback accesses against `MSC_DEMO_DISK_BLOCK_NUM`.
- [x] **ARCH-081 (P0)** Handle capacity change on card removal: eject medium / fail I/O with sense code.
- [x] **ARCH-082 (P1)** Add a block-device mutex around raw `disk_read/write/ioctl`.
- [x] **ARCH-083 (P1)** Make MSC read-only while the terminal is connected; writable only when safe.

**Exit:** output flood, USB yank, SD hotplug and fault-injection scripts survive; no known single-point freeze remains.

---

## Phase 2 — Kernel skeleton

- [x] **ARCH-100 (P1)** Add `src/kernel/bp_event.h/.c`: fixed-size event queue, SPSC-safe, no malloc.
- [x] **ARCH-101 (P1)** Add `src/kernel/bp_timer.h/.c`: 1 ms soft timers, callbacks in the main loop.
- [x] **ARCH-102 (P1)** Add `src/kernel/bp_work.h/.c`: deferred work queue.
- [x] **ARCH-103 (P1)** Add `src/kernel/bp_kernel.h/.c`: `bp_service()` and `bp_kernel_run()`.
- [x] **ARCH-104 (P1)** Add `src/kernel/bp_status.h`: unified error codes.
- [x] **ARCH-105 (P1)** Add `src/kernel/bp_log.h/.c`: leveled log ring, Core 1-safe producer.
- [x] **ARCH-106 (P1)** Add `src/kernel/bp_health.h/.c`: counters, high-water, resource summary.
- [x] **ARCH-107 (P1)** Add `src/kernel/bp_op.h/.c`: cancellable long-operation API.
- [x] **ARCH-108 (P1)** Convert Core 0 main loop to `bp_kernel_run()`.
- [x] **ARCH-109 (P1)** Convert Core 1 loop to kernel services + heartbeat.
- [x] **ARCH-110 (P1)** Add Ctrl+C/button/USB-disconnect cancellation plumbing.
- [x] **ARCH-111 (P1)** Add a test long-op command and verify cancellation latency < 100 ms.
- [x] **ARCH-112 (P1)** Add host unit tests for event queue, timers, work queue, cancellation.
- [x] **ARCH-113 (P2)** Add `kernel stats` command.

**Exit:** no direct blocking waits in main loops; cancel works; host tests pass.

---

## Phase 3 — Core ownership

### Terminal and binary transport

- [x] **ARCH-120 (P1)** Add `src/services/bp_terminal.h/.c`: TX/RX rings, non-blocking writes, drop policy.
- [x] **ARCH-121 (P1)** Move `usb_tx.c`/`usb_rx.c` behind the terminal service API.
- [x] **ARCH-122 (P1)** Add `src/services/bp_bin_transport.h/.c`: CDC1 framing and queues.
- [x] **ARCH-123 (P1)** Remove all `tud_cdc_n_read/write/flush` calls from `src/binmode/*.c`.
- [x] **ARCH-124 (P1)** Convert SUMP to queue-based transport.
- [x] **ARCH-125 (P1)** Convert BPIO to queue-based transport.
- [x] **ARCH-126 (P1)** Convert FALAIO to queue-based transport.
- [x] **ARCH-127 (P1)** Convert legacy4third and IRToy/CH32V003 to the same transport.
- [x] **ARCH-128 (P1)** Add backpressure policy for CDC1 (drop/count, never block Core 0).
- [x] **ARCH-129 (P2)** Benchmark binary throughput before/after; size queues accordingly.

### Storage and MSC

- [x] **ARCH-130 (P1)** Add `src/services/bp_storage.h/.c`: Core 0 mount state machine.
- [x] **ARCH-131 (P1)** Remove every `f_*`/FatFS call from Core 1-owned code.
- [x] **ARCH-132 (P1)** Add `blockdev_lock` around raw SD access.
- [x] **ARCH-133 (P1)** Make card detect an event from Core 1, debounced on Core 0.
- [x] **ARCH-134 (P1)** Guard mount/unmount against open files and active MSC.
- [x] **ARCH-135 (P1)** Add error counters and a `storage` status command.
- [x] **ARCH-136 (P2)** Add filesystem repair/format fallback guidance on mount error.

### Monitor and toolbars

- [x] **ARCH-140 (P1)** Add `bp_monitor_snapshot_t` with sequence counter; Core 1 writes, Core 0 reads.
- [x] **ARCH-141 (P1)** Move `amux_sweep()` callers to the snapshot API.
- [x] **ARCH-142 (P1)** Add `bp_toolbar_frame_t` and a Core 0 → Core 1 frame queue.
- [x] **ARCH-143 (P1)** Make Core 1 toolbar rendering operate only on the frame, never on `toolbar_registry`.
- [x] **ARCH-144 (P1)** Remove `toolbar_draw_prepare()` spin; replace with producer pause.
- [x] **ARCH-145 (P1)** Convert `sys_stats`, `logic_bar`, `pin_watcher` to snapshot reads.
- [x] **ARCH-146 (P2)** Add a toolbar stress test (activate/teardown while capturing).

### Config

- [x] **ARCH-150 (P1)** Split `system_config` into `bp_config_t`, `bp_runtime_t`, `bp_ui_snapshot_t`.
- [x] **ARCH-151 (P1)** Add config version + migration chain.
- [x] **ARCH-152 (P1)** Add validation and corrupt-file fallback.
- [x] **ARCH-153 (P1)** Add atomic save (temp + flush + rename).
- [x] **ARCH-154 (P1)** Make Core 1 read only the UI snapshot.
- [x] **ARCH-155 (P2)** Add `config export/import` for debugging.

**Exit:** `tud_*` only in USB service, `f_*` only in storage service, no `printf` on Core 1, no shared naked struct access.

---

## Phase 4 — Mode and resource migration

### Resource manager

- [x] **ARCH-200 (P1)** Add `src/kernel/bp_resource.h/.c` with leases for PIO/SM/DMA/IRQ/UART/SPI/I2C/GPIO/big buffer.
- [x] **ARCH-201 (P1)** Replace `pio_claim_unused_sm(..., true)` with lease acquisition + error return.
- [x] **ARCH-202 (P1)** Delete `pio_destroy()` from `usb.c` and `ps2.c`; release only owned leases.
- [x] **ARCH-203 (P1)** Add `bp_res_release_all(owner)` on every mode stop/error path.
- [x] **ARCH-204 (P1)** Add a `res` command to print the resource table.
- [x] **ARCH-205 (P1)** Add a mode-switch soak test that asserts zero leaked resources.

### Big buffer

- [x] **ARCH-210 (P1)** Replace the single `allocated` bool with a named lease arena.
- [x] **ARCH-211 (P1)** Make `mem_alloc()` thread-safe and remove its `printf`.
- [x] **ARCH-212 (P1)** Check every `mem_alloc()` return; fail gracefully.
- [x] **ARCH-213 (P1)** Fix `logicanalyzer_setup()` NULL handling.
- [x] **ARCH-214 (P1)** Fix `logicanalyzer_cleanup()` SM/IRQ/program release.
- [x] **ARCH-215 (P1)** Fix FALA big-buffer conflict and re-registration.
- [x] **ARCH-216 (P2)** Evaluate shrinking the 128 KB buffer or splitting it into pools.

### Mode adapters

- [x] **ARCH-220 (P1)** Add `bp_mode_t` lifecycle and compatibility adapter for `_mode`.
- [x] **ARCH-221 (P1)** Migrate USB mode.
- [x] **ARCH-222 (P1)** Migrate PS2 mode.
- [x] **ARCH-223 (P1)** Migrate LCDTAP mode.
- [x] **ARCH-224 (P1)** Migrate SUBGHZ mode.
- [x] **ARCH-225 (P1)** Migrate RF24 mode.
- [x] **ARCH-226 (P1)** Migrate RFID mode.
- [x] **ARCH-227 (P1)** Migrate INFRARED mode.
- [x] **ARCH-228 (P1)** Migrate JTAG/I2S/USBPD (if enabled).
- [x] **ARCH-229 (P1)** Migrate core modes: HiZ, 1-Wire, UART, HDUART, I2C, SPI, 2-Wire, 3-Wire, DIO, LED.
- [x] **ARCH-230 (P1)** Remove `protocol_setup()` before `protocol_cleanup()` ordering globally.
- [x] **ARCH-231 (P2)** Add per-mode self-test hooks to `probe()`.

### IRQ/DMA

- [x] **ARCH-240 (P1)** Inventory all IRQ handlers and priorities.
- [x] **ARCH-241 (P1)** Ensure every handler is removed/restored on mode exit.
- [x] **ARCH-242 (P1)** Ensure DMA channels are claimed/released by lease, not hardcoded.
- [x] **ARCH-243 (P1)** Add a debug command to dump IRQ/DMA/PIO ownership.
- [x] **ARCH-244 (P2)** Add an interrupt-latency test for protocol modes.

**Exit:** 1000 mode switches across all modes with zero leaked resources and no crash.

---

## Phase 5 — Command and UI non-blocking

- [x] **ARCH-300 (P1)** Unify `_global_command_struct` and `bp_command_def` into one table.
- [x] **ARCH-301 (P1)** Add a command descriptor migration shim; remove duplicates.
- [x] **ARCH-302 (P1)** Convert `txtest` to `bp_op_t`.
- [x] **ARCH-303 (P1)** Convert dump/flash read/write commands.
- [x] **ARCH-304 (P1)** Convert logic analyzer dump/export.
- [x] **ARCH-305 (P1)** Convert SUBGHZ scan/sweep/receive/record/waterfall/jam/bruteforce.
- [x] **ARCH-306 (P1)** Convert RFID inventory/read/write.
- [x] **ARCH-307 (P1)** Convert RF24 scan/sweep/waterfall/receive/jam.
- [x] **ARCH-308 (P1)** Convert LCDTAP capture/dump.
- [x] **ARCH-309 (P1)** Convert USB/PS2 capture loops.
- [x] **ARCH-310 (P1)** Convert editor save/load and EEPROM GUI.
- [x] **ARCH-311 (P1)** Make all prompts cancellable and bounded.
- [x] **ARCH-312 (P1)** Replace busy-wait prompt loops with kernel wait + timeout.
- [x] **ARCH-313 (P1)** Integrate FALA with the resource manager and op cancellation.
- [x] **ARCH-314 (P1)** Add a Ctrl+C latency test across all long commands.
- [x] **ARCH-315 (P1)** Harden syntax compiler bounds checks and add a fuzz test.
- [x] **ARCH-316 (P2)** Add command-level timeouts and progress reporting.

**Exit:** every long command cancels within 100 ms; terminal stays responsive.

---

## Phase 6 — Storage and config hardening

- [x] **ARCH-400 (P1)** Storage mount/unmount state machine with debounce and retry.
- [x] **ARCH-401 (P1)** MSC eject/sense handling for removal during transfer.
- [x] **ARCH-402 (P1)** Capacity-change handling and bounds checks on every LBA.
- [x] **ARCH-403 (P1)** Read-only terminal-connected policy enforced at the block layer.
- [x] **ARCH-404 (P1)** Config atomic save and corrupt-file fallback.
- [x] **ARCH-405 (P1)** Config migration test from old files.
- [x] **ARCH-406 (P1)** SD hotplug stress: 100 cycles, verify FAT integrity.
- [x] **ARCH-407 (P2)** Add a storage error log accessible after reboot.

**Exit:** 100 hotplug cycles, no corruption, no hang.

---

## Phase 7 — Test, CI and soak

- [x] **ARCH-500 (P1)** Fix all test runner paths; make them run from any directory.
- [x] **ARCH-501 (P1)** Add host tests for terminal ring backpressure.
- [x] **ARCH-502 (P1)** Add host tests for event queue/timers/cancellation.
- [x] **ARCH-503 (P1)** Add host tests for resource manager and mode rollback.
- [x] **ARCH-504 (P1)** Add host tests for config migration.
- [x] **ARCH-505 (P1)** Add parser fuzz tests (syntax, commands, args).
- [x] **ARCH-506 (P1)** Add HIL smoke suite: boot, mode list, self-test.
- [x] **ARCH-507 (P1)** Add HIL stress suite: flood, yank, hotplug, mode loop, binary stress.
- [x] **ARCH-508 (P1)** Add fault-injection tests (hang Core 0, hang Core 1, queue full, storage fail).
- [x] **ARCH-509 (P1)** Add 24 h soak job and artifact/counter collection.
- [x] **ARCH-510 (P1)** CI: build RB-Pirate target, run host tests, static analysis.
- [x] **ARCH-511 (P1)** CI: enforce flash/RAM/stack budgets.
- [x] **ARCH-512 (P2)** Add CodeQL/clang-tidy baseline and fix high-value findings.
- [x] **ARCH-513 (P2)** Add coverage reporting for host tests.

**Exit:** all acceptance criteria in the plan pass repeatedly.

---

## Phase 8 — Cleanup and documentation

- [x] **ARCH-600 (P2)** Remove dead code and deprecated compatibility shims.
- [x] **ARCH-601 (P2)** Update `docs/architecture/dual_core_guide.md`.
- [x] **ARCH-602 (P2)** Update `docs/architecture/usb_communication_guide.md`.
- [x] **ARCH-603 (P2)** Write `docs/architecture/kernel-guide.md`.
- [x] **ARCH-604 (P2)** Write `docs/architecture/mode-lifecycle-guide.md`.
- [x] **ARCH-605 (P2)** Write `docs/architecture/storage-guide.md`.
- [x] **ARCH-606 (P2)** Document the Core 0/Core 1 ownership rules in `CLAUDE.md`/`AGENTS.md`.
- [x] **ARCH-607 (P2)** Document upstream divergence for future merges.
- [x] **ARCH-608 (P2)** Final 24 h soak and release candidate validation.

---

## Cross-cutting audit checklist

Use this before declaring any phase complete:

- [ ] No `while(1)`/`for(;;)` without a documented exit or cancel path.
- [ ] No `*_blocking()` without a timeout variant in production code.
- [ ] No `printf` in Core 1-owned code.
- [ ] No `tud_*` outside the USB service.
- [ ] No `f_*`/FatFS outside the storage service.
- [ ] No `pio_claim_unused_sm(..., true)` / `dma_claim_unused_channel(true)` in mode code.
- [ ] No `hard_assert` on recoverable I/O or allocation failure.
- [ ] Every `mem_alloc` checked.
- [ ] Every mode stop/error path releases all leases.
- [ ] Every shared cross-core value is queued or a seqlock snapshot.
- [ ] Stack high-water > 25% free on both cores at peak load.
- [ ] Watchdog resets a stalled core within 2 s and preserves a crash record.

---

## Correctness backlog found during the audit

These are smaller than the architectural issues but should be fixed while touching the relevant files.

- [x] **ARCH-700 (P2)** `system_monitor.c`: `current_sense` is a `bool` but is compared to a 32-bit current value; separate the enable flag from the measurement.
- [x] **ARCH-701 (P2)** `storage.c`: `getint()` checks the `found` pointer instead of `*found`; fix the return logic.
- [x] **ARCH-702 (P2)** `pirate.c`: make `system_tick_enable()` idempotent; it currently adds a repeating timer each call.
- [x] **ARCH-703 (P2)** `syntax_compile.c`: reject `:0` repeats and check `out_cnt` before every array write, not after.
- [x] **ARCH-704 (P2)** `mcu.c`: remove the strict-aliasing violation in `mcu_get_unique_id()`.
- [x] **ARCH-705 (P2)** `commands/spi/sniff.c`, `lib/pico_ir_nec/nec_transmit.c`, `lib/picorvd/picoswio.c`: replace `hard_assert(success)` on PIO claim with a returned error.
- [x] **ARCH-706 (P2)** `pirate/storage.c`: bounds-check `fresult_msg[res]`.
- [x] **ARCH-707 (P2)** `ui/ui_term_linenoise.c`: replace the busy-wait sub-prompt loop with a kernel wait/timeout.
- [x] **ARCH-708 (P2)** Enable and gate `-Wall -Wextra` (and optionally `-Werror` for new files) in CMake.
- [x] **ARCH-709 (P2)** Audit all `snprintf`/`strcpy`/`strcat` call sites in non-library code with a static check.

---

## Suggested PR sequence

| PR | Scope | Items |
|---|---|---|
| 1 | Non-blocking terminal TX | ARCH-010 … ARCH-017 |
| 2 | Core 1 ownership / no printf | ARCH-020 … ARCH-025 |
| 3 | Inter-core timeouts | ARCH-030 … ARCH-034 |
| 4 | ADC lock + monitor ownership prep | ARCH-040 … ARCH-043 |
| 5 | Mode transition rollback | ARCH-050 … ARCH-053 |
| 6 | Fault record + watchdog + stacks | ARCH-060 … ARCH-074 |
| 7 | MSC bounds + block lock | ARCH-080 … ARCH-083 |
| 8 | Kernel skeleton | ARCH-100 … ARCH-113 |
| 9 | Terminal/binary transport services | ARCH-120 … ARCH-129 |
| 10 | Storage service + config split | ARCH-130 … ARCH-155 |
| 11 | Resource manager + big buffer | ARCH-200 … ARCH-216 |
| 12+ | Mode migrations | ARCH-220 … ARCH-244 |
| 13+ | Long-command conversion | ARCH-300 … ARCH-316 |
| 14+ | Hardening, CI, soak | ARCH-400 … ARCH-513 |

---

## Definition of done

The rearchitecture is complete when:

1. The plan's acceptance criteria all pass on real hardware.
2. The resource table is empty after every mode exit.
3. `health` shows zero unhandled timeouts and zero unexpected resets after a 24 h soak.
4. New modes can be added without touching Core 1, TinyUSB, FatFS or the kernel internals.
5. A crash always produces a readable record and a controlled recovery.
