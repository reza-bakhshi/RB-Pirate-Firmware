# Tasks: Move Storage and File Systems to Control Core

- [x] **1. Core 0 Storage Service Implementation** <!-- id: 1.1 -->
  - [x] Implement `src/services/bp_storage.h` and `src/services/bp_storage.c` with Core 0 mount state machine (`UNMOUNTED`, `MOUNTING`, `MOUNTED`, `UNMOUNTING`, `ERROR`).
  - [x] Add open file reference counting and guarded unmount logic (`BP_STATUS_BUSY`).
  - [x] Implement debounced card detection on Core 0, fed by notifications from Core 1.
  - [x] Implement storage telemetry counters (mount attempts, errors, unmounts, read/write errors).

- [x] **2. Core 1 Isolation & Block Device Serialization** <!-- id: 1.2 -->
  - [x] Add `blockdev_lock` mutual exclusion in `src/fatfs/diskio.c` across `disk_read`, `disk_write`, `disk_ioctl`, `disk_status`, and `disk_initialize`.
  - [x] Remove `storage_detect()` and any `f_*` calls from `src/pirate.c` Core 1 loop.
  - [x] Notify card detect changes from Core 1 via non-blocking status notification.

- [x] **3. Storage RPC & Streaming Capture Handoff** <!-- id: 1.3 -->
  - [x] Implement lock-free SPSC Storage RPC queues (`bp_storage_rpc_req_t` and `bp_storage_rpc_resp_t`) for Core 1 protocol jobs.
  - [x] Implement synchronous RPC helper `bp_storage_rpc_call()` with timeout and watchdog feeds.
  - [x] Implement streaming write buffer handoff API (`bp_storage_stream_*`) for capture engines.

- [x] **4. Atomic Configuration Persistence & Fallback** <!-- id: 1.4 -->
  - [x] Implement atomic config write (`bpconfig.tmp` -> sync -> rename `bpconfig.bp`).
  - [x] Implement corruption detection and fallback to safe factory defaults.
  - [x] Refactor `src/pirate/storage.c` and `src/pirate/storage.h` to delegate to `bp_storage`.

- [x] **5. CLI Storage Command & Service Kernel Integration** <!-- id: 1.5 -->
  - [x] Implement CLI command `storage` (`src/commands/global/cmd_storage.h`, `src/commands/global/cmd_storage.c`).
  - [x] Register `storage` command in `src/commands.c`.
  - [x] Hook `bp_storage_service()` into `bp_service(0)` in `src/kernel/bp_kernel.c`.
  - [x] Update `src/CMakeLists.txt` with new source files.

- [x] **6. Automated Verification & Archival** <!-- id: 1.6 -->
  - [x] Create unit test suite `tests/test_storage_service.c` and runner `tests/test_storage_service.sh` with AddressSanitizer and UBSan.
  - [x] Run all host unit test suites and verify 0 failures.
  - [x] Verify static memory model compliance ($\le 380\text{ KB}$, 0 dynamic heap).
  - [x] Compile RP2350 target firmware (`make -C build_rp2350 -j$(nproc)`).
  - [x] Run `openspec validate --all --strict` and archive the change.
