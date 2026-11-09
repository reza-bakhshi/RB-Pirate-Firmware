# RB-Pirate Firmware Rearchitecture Plan

> Target: remove the systemic causes of freezes and crashes, and make new protocol modes safe to add, without changing the hardware design, user command syntax, or protocol behavior.

---

## 1. Executive summary

The current firmware is a bare-metal dual-core application built around:

- a large cooperative state machine on Core 0,
- a second infinite loop on Core 1,
- one shared 130+ field `system_config` struct,
- direct calls into TinyUSB and FatFS from both cores,
- blocking ring-buffer writes with no timeout,
- blocking inter-core handshakes with no timeout,
- no runtime watchdog and no fault recovery.

That design can work for a small firmware. It does not scale to the current feature set (20 modes, 140+ commands, TF card, USB MSC, binary modes, logic analyzer, RF/IR/RFID/LCD tap). The result is predictable: one stalled core, one full queue, one unsynchronized pointer, or one leaked PIO/IRQ resource freezes or crashes the whole device.

**Recommendation: do not rewrite from zero.** Re-architect the integration layer around a small deterministic service kernel, and migrate subsystems into it in phases. Keep the protocol engines, PIO programs, command syntax, translation system, and FlatBuffers schema. This gives immediate stability wins in Phase 1 and a clean long-term structure without a 6-12 month rewrite.

**First implemented slice:** the OpenSpec change [`stabilize-terminal-output`](../../openspec/changes/stabilize-terminal-output/) makes terminal output non-blocking and bounded. It is the Level 1 bootstrap safety fix.

**Baseline measured on the current tree:**

| Item | Current |
|---|---|
| Build | `bus_pirate6` builds successfully |
| Flash | 968,828 B / 16 MB (5.77%) |
| RAM | 307,664 B / 512 KB (58.68%) |
| Core 0 stack | 4 KB (`PICO_STACK_SIZE=4096`) |
| Core 1 stack | 4 KB (`PICO_CORE1_STACK_SIZE` defaults to the same) |
| Host tests | IR and SubGHz suites pass; SPSC suite is broken because `tests/test_spsc_queue.sh` hardcodes `/home/ian/bp5fw` |

---

## 2. What is actually failing

These are the concrete freeze/crash paths found in the current tree. They explain the symptoms you described better than any single bug.

### 2.1 Freeze taxonomy

| Symptom | Most likely cause | Where |
|---|---|---|
| Terminal stops after heavy output, large dump, `txtest`, logic analyzer dump | Core 0 blocks forever writing to a full TX ring because Core 1 stopped draining | `usb_tx.c`, `printf.c`, `spsc_queue.h` |
| Device freezes when the TF card is inserted or removed | Core 1 calls `printf()` from `storage_detect()`, which hits `BP_ASSERT_CORE0()` and panics | `pirate.c`, `storage.c`, `usb_tx.c`, `debug_rtt.h` |
| Device freezes when USB MSC hits a disk error | Core 1 MSC callback calls `printf()` on error, same assert path | `msc_disk.c` |
| Random freeze when switching modes or using USB/PS2/LCDTAP | PIO state machines and IRQ handlers are stolen, leaked, or never released | `usb.c`, `ps2.c`, `logicanalyzer.c`, PIO mode drivers |
| Freeze after starting/stopping logic capture or using `logic` | Big-buffer allocation failure is ignored; `memset(NULL, ...)` or DMA writes to NULL | `binmode/logicanalyzer.c` |
| Freeze when Core 1 is busy with SD/USB | Core 0 waits in `multicore_fifo_pop_blocking()` with no timeout | `pirate/intercore_helpers.c` |
| Binary mode hangs / USB corruption | Core 0 calls `tud_cdc_n_read/write/flush()` while Core 1 runs `tud_task()` | `binmode/sump.c`, `bpio.c`, `falaio.c` |
| Binary mode blocks forever when CDC1 is not open | `bin_tx_fifo_put()` spins until Core 1 drains, but Core 1 only drains when TX space is available | `usb_tx.c`, `irtoy-air.c`, `arduino_ch032v003.c` |
| Filesystem corruption or hang after card hotplug | `f_mount()` is not re-entrant, but is called from Core 0 and Core 1 | `storage.c`, `pirate.c` |
| Random hang after many mode changes | Resource leaks: SMs, IRQs, DMA channels, PIO program memory, big buffer | all modes |
| Hard freeze with red LEDs or dead device | `hard_assert()` / `panic()` / HardFault; handler blinks forever instead of recovering | `debug_rtt.c`, `pirate/hardfault.c` |
| Crash during long RF/IR/RFID operations | Blocking loops with no cancellation, no timeout, and shared buffers | `subghz.c`, `rfid.c`, `rf24.c`, `ps2.c`, `usb.c` |

### 2.2 Evidence in the current code

1. **Blocking terminal output.**
   `_putchar()` calls `tx_fifo_put()`, which calls `spsc_queue_add_blocking()`. If the 1 KB TX ring is full, Core 0 spins forever.
   `src/printf-4.0.0/printf.c:124-127`, `src/usb_tx.c:232-235`, `src/spsc_queue.h:125-130`.

2. **Core 1 can stop draining TX.**
   `tx_fifo_service()` returns immediately when `tud_cdc_n_write_available(0) < 64`, before the disconnect/flush path. A connected host that stops reading (or a terminal with flow control) permanently blocks Core 0.
   `src/usb_tx.c:123-127`.

3. **Unbounded drain wait.**
   `tx_fifo_wait_drain()` spins with no timeout. `txtest` calls it before and after a multi-KB blast.
   `src/usb_tx.c:249-254`, `src/commands/global/txtest.c:169,200`.

4. **Core 1 calls `printf()`.**
   `storage_detect()` is called from the Core 1 tick and prints on mount/unmount/error. `printf()` is Core 0-only by contract, so this triggers `BP_ASSERT_CORE0()` → `hard_assert(false)` → `panic()`.
   `src/pirate.c:810-814`, `src/pirate/storage.c:110-129`, `src/usb_tx.c:232-235`, `src/debug_rtt.h:202-212`.

5. **Core 1 MSC callbacks call `printf()`.**
   Disk read/write errors print from the TinyUSB callback on Core 1, same panic path.
   `src/msc_disk.c:245-278`.

6. **Blocking inter-core handshake.**
   `icm_core0_send_message_synchronous()` does `multicore_fifo_push_blocking()` then loops on `multicore_fifo_pop_blocking()` with `assert(response == raw_msg.raw)`. If Core 1 is stuck anywhere, Core 0 freezes. This is called before/after every syntax execution and every toolbar update.
   `src/pirate/intercore_helpers.c:42-52`, `src/ui/ui_process.c:38,52`, `src/ui/ui_toolbar.c:336-340`.

7. **TinyUSB is used from both cores.**
   TinyUSB is configured with `CFG_TUSB_OS = OPT_OS_NONE`, i.e. it has no internal locking. Core 1 runs `tud_task()` while Core 0 calls `tud_cdc_n_read/write/flush()` directly in SUMP, BPIO and FALAIO.
   `src/tusb_config.h:68-69`, `src/pirate.c:768-780`, `src/binmode/sump.c:511-519`, `src/binmode/bpio.c:196-199,790-807`, `src/binmode/falaio.c:46-48,125-152`.

8. **Binary TX can block forever.**
   `bin_tx_fifo_put()` blocks. Core 1 only services the binary TX ring if `system_config.binmode_usb_tx_queue_enable` is true and `tud_cdc_n_write_available(1) >= 64`. If CDC1 is not open, producers spin forever.
   `src/usb_tx.c:256-296`, `src/binmode/irtoy-air.c`, `src/lib/arduino-ch32v003-swio/arduino_ch032v003.c`.

9. **Global PIO teardown steals other drivers' resources.**
   `usb.c` and `ps2.c` each define `pio_destroy()` that unclaims **every** state machine on PIO0 and PIO1 and clears both instruction memories. That can wipe the logic analyzer, mode PIO programs, or any other user of those blocks.
   `src/mode/usb.c:978-993`, `src/mode/ps2.c:816-837`.

10. **Logic analyzer assumes allocation succeeded.**
    `logicanalyzer_setup()` ignores `mem_alloc()` failure, then `logic_analyzer_configure()` does `memset(la_buf, 0, LA_BUFFER_SIZE)`. If the big buffer is already owned by the editor/FALA/USB/PS2, this dereferences NULL.
    `src/binmode/logicanalyzer.c:327,208`, `src/pirate/mem.c:20-29`.

11. **ADC lock is racy.**
    `adc_busy_wait()` uses a non-volatile `static bool busy` and releases it outside the spinlock. Core 1 monitor and Core 0 PSU/ADC callers can race, producing a lost release or a double-acquire.
    `src/pirate/amux.c:49-66`.

12. **FatFS is accessed from both cores.**
    Core 0 commands open/read/write files. Core 1 mounts/unmounts on card detect and performs raw MSC block I/O. `f_mount()` and volume control are explicitly not re-entrant even when `FF_FS_REENTRANT` is enabled.
    `src/pirate/storage.c:85-132`, `src/pirate.c:459-467,810-814`, `src/msc_disk.c`.

13. **Toolbar registry is shared without synchronization.**
    Core 1 iterates `toolbar_registry[]` and calls `update_core1` callbacks while Core 0 can register, unregister, destroy, or redraw toolbars. `toolbar_draw_prepare()` waits at most 50 ms, then proceeds anyway.
    `src/ui/ui_toolbar.c:20,271-287,352-423`.

14. **`system_config`, `psu_status`, ADC arrays and pin labels are shared.**
    Core 1 reads PSU state, config fields, pin labels and ADC values while Core 0 mutates them. Some are `volatile`; most are not. There is no snapshot, seqlock or mutex.
    `src/system_config.h`, `src/system_monitor.c`, `src/toolbars/*`, `src/ui/ui_statusbar.c`.

15. **No runtime watchdog / recovery.**
    The only `watchdog_enable()` is in `mcu_reset()`. `isr_hardfault()` blinks red forever. `hard_assertion_failure()` calls `panic()`. A stuck device never reboots and leaves no crash record.
    `src/pirate/mcu.c:31-35`, `src/pirate/hardfault.c:53-82`, `src/debug_rtt.c:46-50`.

16. **4 KB stacks with heavy call paths.**
    Both cores get 4 KB. `printf`, FatFS, mjson, FlatBuffers and LCDTAP C++ code can consume large frames. `SCRATCH_X` and `SCRATCH_Y` are both reported 100% used by the linker. There is no stack high-water instrumentation.
    `src/CMakeLists.txt:807`, build memory report.

17. **Mode transition order is unsafe.**
    `ui_mode_enable_args()` runs the new mode's `protocol_setup()` (which can prompt, load files, and print) before cleaning up the old mode. If `setup_exc()` fails, the new mode's partial resources are not released.
    `src/ui/ui_mode.c:172-181`.

18. **MSC fallback can read/write out of bounds.**
    When storage is removed, capacity changes from the card size to the 16-block demo disk. A host with a stale larger capacity can issue an LBA beyond 16; `msc_disk[lba]` is used without a bounds check.
    `src/msc_disk.c:207-219,245-292`.

19. **Long operations are not cancellable or are not state machines.**
    Several mode commands own Core 0 for seconds/minutes. Some check `rx_fifo_try_get`, some do not. Sub-prompts busy-wait with no yield.
    `src/ui/ui_term_linenoise.c:188-211`, `src/mode/subghz.c`, `src/mode/rfid.c`, `src/mode/rf24.c`, `src/mode/ps2.c`, `src/mode/usb.c`.

20. **Test/CI does not catch any of this.**
    `tests/test_spsc_queue.sh` hardcodes `/home/ian/bp5fw`. CI only builds; there is no soak, hotplug, queue-full, mode-switch, binary-mode or fault-recovery test.
    `tests/test_spsc_queue.sh`, `.github/workflows/build.yaml`.

---

## 3. Target architecture

### 3.1 Shape

```
┌────────────────────────────── Core 0: Application ──────────────────────────────┐
│  Shell / command parser                                                         │
│      │                                                                          │
│      ▼                                                                          │
│  Mode manager ──► protocol engines (I2C/SPI/UART/1-Wire/IR/RF/...)              │
│      │                                                                          │
│  Storage service (FatFS owner)   Config service   PSU safety   Resource manager  │
│      │                                                                          │
│  Kernel: event queue + soft timers + work queue + log + fault + watchdog         │
└───────────────────────────▲───────────────────────────────┬─────────────────────┘
                            │ typed SPSC queues             │ seqlock snapshots
                            │ (bytes, events, log records)  │ (ADC, UI, status)
┌───────────────────────────┴───────────────────────────────▼─────────────────────┐
│                           Core 1: I/O service                                   │
│  TinyUSB device (CDC0, CDC1, MSC)   Terminal TX drain   ADC/AMUX sampler         │
│  RGB driver   Toolbar blitter   Button event   Watchdog heartbeat                │
└─────────────────────────────────────────────────────────────────────────────────┘
```

### 3.2 Ownership matrix

| Resource / state | Sole owner | How the other core accesses it |
|---|---|---|
| Persisted config | Core 0 config service | Read-only immutable snapshot |
| Runtime config / mode state | Core 0 | Small read-only UI snapshot |
| Terminal TX ring | Producer Core 0, consumer Core 1 | Non-blocking SPSC |
| Terminal RX ring | Producer Core 1, consumer Core 0 | Non-blocking SPSC |
| CDC1 binary data | Core 1 USB transport | Core 0 sends/receives framed events through queues |
| TinyUSB (`tud_*`) | Core 1 only | Never called from Core 0 |
| FatFS (`f_*`, `fs`) | Core 0 storage service | Never called from Core 1 |
| Raw SD blocks / MSC | Core 1 block layer | Bounded, mutex-protected `disk_read/write` |
| ADC / AMUX | Core 1 sampler | Seqlock snapshot to Core 0 |
| PSU control decision | Core 0 | Core 1 posts fuse/fault events |
| PIO blocks, SMs, DMA, IRQ | Resource manager | Leases only, no ad-hoc claim |
| GPIO function / BIO pins | Pin manager | Leases only |
| Big buffer | Arena manager | Named leases with failure handling |
| Toolbars | Core 0 registry | Core 1 receives render snapshots |
| Logging | Core 0 logger | Core 1 posts fixed-size log records |

### 3.3 Non-negotiable rules

1. **One owner per resource.** No module may touch another module's peripheral, PIO block, DMA channel, IRQ, pin, or buffer.
2. **No cross-core function calls.** Only queues and snapshots cross the core boundary.
3. **No blocking without a timeout.** Every wait returns an error after a bounded time.
4. **`printf` is Core 0 only.** Core 1 uses `bp_log_post()`.
5. **`tud_*` is Core 1 only.** Core 0 uses the binary transport queue.
6. **`f_*` is Core 0 only.** Core 1 uses the raw block layer.
7. **Mode entry/exit is transactional.** Old mode stops before new mode starts; failure rolls back and releases everything.
8. **Every long operation is a cancellable state machine.** Ctrl+C and the button set a cancel flag; `bp_op_run()` pumps the kernel.
9. **Every shared value is queued or a seqlock snapshot.** No naked cross-core pointers or structs.
10. **The watchdog is fed only when both cores are healthy.** A stalled core must reset the device, not hang it.

---

## 4. Component design and changes

### 4.1 Kernel (`src/kernel/`)

Add a small deterministic service kernel. It is not an RTOS; it is the missing coordination layer.

- Fixed-size event queue, SPSC-safe, no malloc.
- Soft timers driven by one 1 ms tick; callbacks run in the main loop, never in the ISR.
- Work queue for deferred operations.
- `bp_service()` pumps events, timers, TX, input and watchdog checks.
- Health counters: queue high-water, dropped bytes, timeouts, stack high-water, fault count.

Proposed API sketch:

```c
typedef enum {
    BP_EV_INPUT,
    BP_EV_TICK,
    BP_EV_STORAGE,
    BP_EV_USB,
    BP_EV_TOOLBAR,
    BP_EV_FAULT,
    BP_EV_LOG,
    BP_EV_MODE,
} bp_event_type_t;

typedef struct {
    uint16_t type;
    uint16_t len;
    union {
        uint8_t bytes[64];
        uint32_t u32[16];
        bp_fault_record_t fault;
    } data;
} bp_event_t;

bool bp_event_post(const bp_event_t *ev);
bool bp_event_poll(bp_event_t *ev, uint32_t timeout_ms);
void bp_kernel_run(void);
```

### 4.2 Long-operation API

This is how existing blocking commands get migrated without rewriting them all at once.

```c
typedef enum {
    BP_OP_RUNNING,
    BP_OP_DONE,
    BP_OP_CANCELLED,
    BP_OP_ERROR,
} bp_op_state_t;

typedef struct bp_op {
    bp_op_state_t (*step)(struct bp_op *op, uint32_t now_ms);
    void (*cancel)(struct bp_op *op);
    uint32_t deadline_ms;
    void *ctx;
} bp_op_t;

bp_status_t bp_op_run(bp_op_t *op);  // pumps events, checks cancel/timeout/watchdog
```

Rules:

- `step()` must return within ~1 ms.
- `bp_op_run()` calls `bp_service()` between steps.
- `deadline_ms == 0` means no deadline, but the operation still must be cancellable.
- Ctrl+C, button press, USB disconnect and mode change set the cancel flag.

### 4.3 Terminal transport (`src/services/bp_terminal.c`)

**Current:** blocking producer, 1 KB ring, no overflow policy, Core 1 can stop draining.

**Target:**

- `bp_term_write(buf, len)` is non-blocking. It returns `BP_ERR_TX_FULL` and increments `tx_dropped`.
- A whole-line overflow policy: if a line cannot fit, drop the line and emit one `[output dropped: N bytes]` marker when space returns.
- Core 1 drains continuously; if CDC0 is not connected, it flushes and discards.
- `tx_fifo_wait_drain(timeout)` returns bool.
- No `printf` from Core 1; `bp_log_post()` writes fixed records to a log ring.

### 4.4 Binary transport (`src/services/bp_bin_transport.c`)

**Current:** Core 0 calls TinyUSB directly in SUMP/BPIO/FALAIO.

**Target:**

- Core 1 owns CDC1 and TinyUSB.
- Core 1 frames inbound packets (COBS/SUMP/legacy) into `bin_rx_fifo`.
- Core 0 produces outbound packets into `bin_tx_fifo`.
- Core 1 drains with backpressure, never blocks Core 0.
- If CDC1 is not open, outbound packets are dropped and counted; no producer spins.
- Large captures use a double-buffer handoff, not direct TinyUSB writes.

### 4.5 Storage and MSC (`src/services/bp_storage.c`)

**Current:** Core 0 and Core 1 both call FatFS; Core 1 prints; mount/unmount races.

**Target:**

- Core 0 is the only FatFS owner.
- Card detect is an event from Core 1 (ADC snapshot / GPIO), debounced in Core 0.
- Mount/unmount is a Core 0 state machine: `IDLE → MOUNTING → MOUNTED → UNMOUNTING → ERROR`.
- No file operation while mounting/unmounting.
- Core 1 MSC performs raw block I/O through a block-device mutex with bounds checks.
- MSC is read-only while the terminal is connected; writable only when the terminal is disconnected and no file operation is active.
- On removal while MSC is mounted: eject the medium, fail further I/O with sense codes, never fall back to an out-of-bounds demo-disk access.
- Set `FF_USE_LFN` to `2` (static LFN buffer) to remove per-operation `malloc/free` and heap fragmentation.

### 4.6 Monitor and ADC (`src/services/bp_monitor.c`)

**Current:** Core 1 sweeps AMUX, Core 0 reads the arrays directly; the ADC spinlock is racy.

**Target:**

- Core 1 owns ADC/AMUX and samples on a fixed schedule.
- Results are published in a double-buffered `bp_monitor_snapshot_t` with a sequence counter.
- Core 0 reads a consistent snapshot; PSU safety checks use the latest snapshot or a bounded request/response.
- The PSU fuse/overcurrent path remains hardware-assisted and posts a fault event immediately.
- No `busy_wait_us()` on Core 0 for monitor data.

### 4.7 Toolbars (`src/services/bp_toolbar.c`)

**Current:** Core 1 walks a registry that Core 0 mutates.

**Target:**

- Core 0 owns the toolbar registry and all `toolbar_t` objects.
- Core 0 renders each toolbar into a fixed-size row buffer and posts a `bp_toolbar_frame_t` to Core 1.
- Core 1 only positions the cursor and writes bytes. It never dereferences toolbar objects or callbacks.
- Focus handling stays on Core 0.
- `toolbar_draw_prepare()` no longer spins on Core 1 state; it just pauses the producer.

### 4.8 Resource manager (`src/kernel/bp_resource.c`)

This is the fix for the PIO/IRQ/DMA class of crashes.

```c
typedef enum {
    BP_RES_PIO0, BP_RES_PIO1, BP_RES_PIO2,
    BP_RES_PIO_SM, BP_RES_DMA, BP_RES_IRQ,
    BP_RES_UART, BP_RES_SPI, BP_RES_I2C,
    BP_RES_GPIO_FUNCTION, BP_RES_BIG_BUFFER,
} bp_res_class_t;

typedef struct {
    bp_res_class_t class;
    uint8_t index;
    const char *owner;
} bp_res_lease_t;

bool bp_res_acquire(bp_res_lease_t *lease, bp_res_class_t class, uint8_t index, const char *owner);
void bp_res_release(bp_res_lease_t *lease);
void bp_res_release_all(const char *owner);
bool bp_res_is_free(bp_res_class_t class, uint8_t index);
```

- Every mode gets an owner name.
- Acquisition failure returns an error; it never `hard_assert`s.
- `bp_res_release_all(mode_name)` runs on mode stop, including error paths.
- `pio_destroy()` in USB/PS2 is deleted. Each driver releases only its own leases.
- A debug command `res` prints the resource table.

### 4.9 Mode manager (`src/modes/`)

**Current:** function-pointer table, no lifecycle, no rollback, setup before cleanup.

**Target:**

```c
typedef struct bp_mode {
    const char *name;
    bp_status_t (*probe)(void);
    bp_status_t (*configure)(void);   // prompts / CLI arguments
    bp_status_t (*start)(void);       // hardware on
    void (*stop)(void);               // hardware off
    void (*deinit)(void);             // release leases
    void (*periodic)(uint32_t now_ms);
    const bp_command_def_t *commands;
    uint16_t command_count;
} bp_mode_t;

bp_status_t bp_mode_switch(uint8_t new_mode);
```

`bp_mode_switch()`:

1. Validate the target.
2. `stop()` + `deinit()` + `release_all()` the current mode.
3. Reset pins to a safe state.
4. `configure()` the new mode.
5. `start()` the new mode.
6. On any failure: `stop()/deinit()` the partial new mode, return to HiZ, report a clear error.

Existing `_mode` structs are wrapped by a compatibility adapter so migration can be incremental.

### 4.10 Config (`src/kernel/bp_config.c`)

**Current:** one 130+ field struct, written by both cores, persisted as JSON.

**Target:**

- `bp_config_t`: persisted, versioned, validated, with a migration function.
- `bp_runtime_t`: Core 0 only (current mode, pins, PSU, command state).
- `bp_ui_snapshot_t`: small seqlock snapshot for Core 1 (terminal size, colors, pin labels, PSU display state, toolbar flags).
- Atomic save: write temp file, flush, rename.
- Corrupt config falls back to defaults and reports once, never bricks boot.

### 4.11 Fault, watchdog, logging (`src/kernel/bp_fault.c`, `bp_log.c`)

- Enable the hardware watchdog early (2-5 s).
- Each core increments a heartbeat counter. Core 0 feeds the watchdog only if both counters advanced.
- On HardFault/assert: capture PC, LR, SP, CFSR, HFSR, BFAR, MMFAR and the last 32 log records into a no-init RAM record and a flash ring.
- Blink an error code once, then reset. Do not blink forever.
- On boot: load the crash record, print a short report, enter safe mode if the same fault repeats N times.
- `BP_ASSERT` in release builds logs and returns where possible; only true invariant corruption triggers a controlled reset.
- Core 1 logs through a fixed-size record queue; it never calls `printf`.

### 4.12 Command/UI layer (`src/ui/`, `src/commands/`)

- Unify the legacy `_global_command_struct` and `bp_command_def` into one descriptor table.
- Convert long commands to `bp_op_t` state machines, worst offenders first:
  - `txtest`, dump/flash, editor save/load, logic dump
  - SUBGHZ scan/sweep/receive/record/waterfall/jam/bruteforce
  - RFID inventory/read/write
  - RF24 scan/sweep/waterfall/receive/jam
  - LCDTAP capture/dump
  - USB/PS2 capture loops
- Prompts become cancellable and bounded: Ctrl+C, USB disconnect and button always return control.
- Keep the existing syntax bytecode engine, but add strict bounds checks and a fuzz test.

### 4.13 Build, test and CI

- Fix `tests/test_spsc_queue.sh` to use `dirname "$0"`.
- Add host tests for: ring buffer backpressure, event queue, soft timers, resource manager, mode transition rollback, config migration, command parser fuzz, MSC bounds.
- Add a HIL suite using `tools/serial`:
  - terminal flood + host pause + reconnect
  - SD insert/remove 100 cycles
  - every mode switched 1000 times
  - SUMP/BPIO binary stress
  - Ctrl+C cancel latency
  - watchdog injection
  - 24 h random soak
- CI: build single RB-Pirate target, run host tests, clang-tidy/CodeQL, enforce size budget and stack high-water.
- Increase stacks to 8 KB each and add high-water reporting. RAM has ~200 KB free; 8 KB more is affordable. Remove or shrink the 128 KB monolithic big buffer later if needed.

---

## 5. Migration phases

### Phase 0 — Baseline and instrumentation (1-2 days)

- Create branch `refactor/kernel`.
- Fix the SPSC test runner path.
- Record build size, RAM, stack usage, and current test results.
- Add health counters: `tx_dropped`, `tx_overflow`, `icm_timeouts`, `watchdog_resets`, `stack_high_water`, `mode_switches`, `fault_count`.
- Write reproduction scripts for: output flood + host pause, SD hotplug, mode switch loop, binary stress.
- **Exit:** every reported freeze has a repeatable test or a clear instrumentation signal.

### Phase 1 — Stop the bleeding (3-5 days)

- Non-blocking terminal TX with drop policy and overflow reporting.
- Remove all `printf` from Core 1 (`storage_detect`, `msc_disk`).
- Move storage detect/mount/unmount to Core 0.
- Add timeouts to `icm_core0_send_message_synchronous()` and `tx_fifo_wait_drain()`.
- Fix `adc_busy_wait()` locking.
- Fix mode transition order and rollback.
- Remove `hard_assert` from recoverable I/O paths; add fault record + reset.
- Enable runtime watchdog with dual-core heartbeat.
- Increase stacks to 8 KB and add high-water logging.
- Bound `msc_disk` fallback LBAs.
- **Exit:** the known single-point freezes are gone; the repro scripts survive.

### Phase 2 — Kernel skeleton (1-2 weeks)

- Add event queue, soft timers, work queue, service pump.
- Add `bp_log`, `bp_fault`, `bp_health`.
- Add `bp_op_t` and cancellation plumbing.
- Convert Core 0 main loop and Core 1 loop to the kernel.
- **Exit:** no direct blocking waits in the main loops; Ctrl+C cancels a test long-op within 100 ms.

### Phase 3 — Core ownership (1-2 weeks)

- Move FatFS entirely to Core 0; add block-device mutex and MSC bounds/eject handling.
- Move CDC1/TinyUSB entirely to Core 1; delete direct `tud_*` calls from Core 0.
- Add ADC seqlock snapshot.
- Add toolbar frame queue; Core 1 never touches toolbar objects.
- Split config into persisted/runtime/UI snapshot.
- **Exit:** grep rules pass: no `tud_*` outside USB service, no `f_*` outside storage service, no `printf` in Core 1 files.

### Phase 4 — Mode and resource migration (2-4 weeks)

- Wrap all modes in the new lifecycle.
- Replace ad-hoc PIO/SM/DMA/IRQ claims with leases.
- Delete global `pio_destroy()` from USB/PS2.
- Fix logic analyzer allocation failure, IRQ restore, SM ownership.
- Move new modes first: USB, PS2, LCDTAP, SUBGHZ, RF24, RFID, INFRARED, then core modes.
- Add `res` debug command.
- **Exit:** 1000 mode switches with zero leaked resources and no crash.

### Phase 5 — Command/UI non-blocking (2-3 weeks)

- Unify command descriptors.
- Convert long commands to `bp_op_t`, in priority order.
- Make prompts cancellable and bounded.
- Integrate FALA with the resource manager and big-buffer leases.
- **Exit:** every long command cancels within 100 ms; terminal stays responsive.

### Phase 6 — Storage/config hardening (1 week)

- Mount state machine, debounce, error recovery.
- MSC read-only/writable policy, eject/sense, capacity change handling.
- Config versioning, migration, atomic save, corrupt-file fallback.
- **Exit:** 100 SD hotplug cycles with no corruption or hang.

### Phase 7 — Test, CI and soak (1 week)

- Host unit tests + fuzz.
- HIL suite + 24 h soak.
- CI gates.
- Size/stack budgets.
- **Exit:** all acceptance criteria pass repeatedly.

### Phase 8 — Cleanup and documentation (ongoing)

- Remove dead code and deprecated compatibility shims.
- Update architecture docs and mode/command developer guides.
- Keep upstream divergence documented.

**Estimated effort:** 8-12 weeks for one experienced firmware engineer. Phase 1 is the immediate relief; Phases 2-4 prevent recurrence.

---

## 6. File-by-file change map

| File / area | Current issue | Change |
|---|---|---|
| `src/pirate.c` | mixes app, storage, USB, monitor on both cores | Core 0 app loop; Core 1 I/O loop; kernel service; watchdog |
| `src/usb_tx.c` | blocking producer; Core 1 can stop draining | non-blocking ring, drop policy, timeout drain, counters |
| `src/usb_rx.c` | blocking helper APIs remain | non-blocking only; document core affinity |
| `src/printf-4.0.0/printf.c` | `_putchar` blocks and asserts | route through non-blocking terminal logger |
| `src/pirate/storage.c` | Core 1 mount/printf; FatFS on both cores | Core 0 storage service; event-based card detect |
| `src/fatfs/ffconf.h` | `FF_USE_LFN=3` mallocs per operation | `FF_USE_LFN=2` static buffer |
| `src/fatfs/ffsystem.c` | volume mutex but no block lock | keep volume mutex; add block-device lock |
| `src/msc_disk.c` | Core 1 printf, OOB fallback, capacity race | raw block layer, bounds, eject/sense, no printf |
| `src/pirate/intercore_helpers.c` | blocking FIFO handshake, no timeout | replace with event queue; timeout fallback |
| `src/pirate/amux.c` | racy ADC lock | single Core 1 owner; snapshot |
| `src/system_monitor.*` | shared arrays | seqlock snapshot |
| `src/system_config.*` | monolithic shared state | persisted/runtime/UI split |
| `src/ui/ui_toolbar.c` | shared registry | Core 0 registry + Core 1 frame queue |
| `src/ui/ui_statusbar.c` | shared config | snapshot |
| `src/toolbars/*` | Core 1 callbacks read app state | read-only snapshot |
| `src/ui/ui_mode.c` | setup before cleanup; no rollback | transactional `bp_mode_switch()` |
| `src/modes.c`, `src/modes.h` | legacy function table | new lifecycle + compatibility adapter |
| `src/mode/*.c` | ad-hoc resource claims, long loops | leases, cancellable ops, timeouts |
| `src/binmode/*.c` | direct TinyUSB from Core 0 | binary transport queue |
| `src/binmode/logicanalyzer.c` | NULL deref, IRQ/SM leaks | allocation checks, leases, IRQ restore |
| `src/binmode/fala.c` | big-buffer conflict | arena lease + graceful failure |
| `src/pirate/mem.c` | single 128 KB buffer, no lock | arena with named leases, thread-safe, no printf |
| `src/pirate/hardfault.c` | infinite blink | capture + log + controlled reset |
| `src/debug_rtt.*` | assert/panic only | fault record, safe mode, log integration |
| `src/CMakeLists.txt` | 4 KB stacks, warnings not gated | 8 KB stacks, warning gates, size budget |
| `tests/*` | hardcoded path, no integration tests | portable runners + host/HIL suites |
| `.github/workflows/*` | build only | build + tests + static analysis + soak job |

---

## 7. Acceptance criteria

The rearchitecture is done when all of these pass on real RB-Pirate hardware:

1. **No unbounded waits** in Core 0 or Core 1 main paths. A static scan of `while`/`for(;;)`/`*_blocking` finds only documented, bounded loops.
2. **Output backpressure:** pause the host for 30 s during a 1 MB `txtest`; the device remains responsive and reports dropped bytes.
3. **USB disconnect/reconnect:** yank USB during output and during binary capture; no freeze, reconnect works.
4. **SD hotplug:** 100 insert/remove cycles while commands run; no panic, no filesystem corruption.
5. **Mode matrix:** every mode switched 1000 times, including USB/PS2/LCDTAP/SUBGHZ/RFID/RF24/IR; resource table returns to zero.
6. **Binary stress:** SUMP and BPIO at maximum rate while the terminal is active; no corruption or hang.
7. **Cancellation:** Ctrl+C cancels every long command within 100 ms.
8. **Fault recovery:** inject a Core 0 or Core 1 hang; watchdog resets within 2 s and the crash record is readable after boot.
9. **Stack margin:** minimum 25% free stack on both cores at peak load.
10. **Soak:** 24 h random command/protocol traffic with no reset, no leak, no deadlock.

---

## 8. Risks and mitigations

| Risk | Mitigation |
|---|---|
| Timing-sensitive PIO/protocol code regresses | Keep protocol engines unchanged; wrap them, do not rewrite them; HIL tests per mode |
| Binary mode throughput drops through queues | Size queues for worst case; double-buffer large captures; measure before/after |
| RAM growth from stacks/queues | 512 KB total, ~200 KB free; start with 8 KB stacks; shrink the 128 KB big buffer if needed |
| Divergence from upstream Bus Pirate | Keep changes in `kernel/`, `services/` and adapters; document patches |
| Refactor stalls half-done | Phase 1 is independently useful; each phase has an exit criterion and a compatibility path |
| Hidden hardware errata | Keep the RP2350 E9 workaround and existing board init; only move ownership, do not change sequences |

---

## 9. What we will not change

- RB-Pirate pin map and board initialization.
- PIO programs and protocol wire timing.
- Existing command syntax and mode names.
- Translation key system.
- FlatBuffers schema and binary protocol semantics.
- Bootloader / UF2 flow.

---

## 10. First pull requests

1. `fix/tx-nonblocking` — non-blocking TX, drop policy, timeout drain, counters.
2. `fix/core1-ownership` — remove `printf` from Core 1, move storage detect to Core 0.
3. `fix/icm-timeouts` — timeout/async inter-core messages.
4. `fix/adc-lock` — correct ADC ownership/locking.
5. `feat/fault-watchdog` — crash record, watchdog, assert policy, 8 KB stacks.
6. `feat/kernel-skeleton` — event queue, timers, service pump, resource manager.

After PR 6, the remaining work is mechanical migration with tests at every step.

---

## Related documents

- [rearchitecture-todo.md](rearchitecture-todo.md) — tracked checklist with IDs and acceptance criteria.
- [dual_core_guide.md](dual_core_guide.md) — current SPSC/dual-core description (will need updating after Phase 3).
- [usb_communication_guide.md](usb_communication_guide.md) — current USB/TinyUSB description (will need updating after Phase 3).
