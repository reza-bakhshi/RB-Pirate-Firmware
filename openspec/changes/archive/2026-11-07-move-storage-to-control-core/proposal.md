# Proposal: Move Storage and File Systems to Control Core

## Why

Currently, storage operations and card detection lack strict core boundaries. While most file commands run on Core 0, `storage_detect()` is periodically called by Core 1 inside `core1_infinite_loop()`, where it directly invokes `storage_mount()` and `f_mount()` or `storage_unmount()`. Concurrently, TinyUSB MSC running on Core 1 executes raw block I/O (`disk_read`, `disk_write`) without synchronization against FatFS operations on Core 0. Furthermore, protocol drivers and capture engines running on Core 1 lack a clean, thread-safe mechanism to write capture data or read configuration scripts without directly invoking FatFS or risking race conditions.

Step B5 moves complete ownership of the FAT filesystem, mount state management, card detection debouncing, and configuration persistence to Core 0 (`bp_storage`). Raw block device operations are guarded by `blockdev_lock`. Core 1 is completely isolated from FatFS (`f_*`) and interacts with storage exclusively via asynchronous streaming buffers and synchronous RPC messages.

## What Changes

- **Core 0 Storage Service (`src/services/bp_storage.h`, `src/services/bp_storage.c`)**:
  - Implement a dedicated mount state machine (`UNMOUNTED`, `MOUNTING`, `MOUNTED`, `UNMOUNTING`, `ERROR`) running on Core 0.
  - Guard mount and unmount transitions against open files and active USB MSC transactions.
  - Service debounced card detection on Core 0, receiving raw presence notifications from Core 1 without Core 1 touching FatFS.
  - Track storage operational telemetry: mount attempts, mount failures, unmount counts, read/write errors, active open files.
  - Implement a `storage` CLI command displaying mount status, filesystem type, capacity, error counters, and recovery guidance.
- **Block Device Concurrency Lock (`blockdev_lock`)**:
  - Add mutual exclusion around low-level disk access (`disk_read`, `disk_write`, `disk_ioctl`) in `src/fatfs/diskio.c` to serialize concurrent block access between Core 0 (FatFS) and Core 1 (TinyUSB MSC).
- **Core 1 Isolation from FatFS**:
  - Remove `storage_detect()`, `storage_mount()`, `storage_unmount()`, and all `f_*` calls from Core 1 code (`src/pirate.c`).
  - Provide a lock-free SPSC Storage RPC queue (`bp_storage_rpc_req_t` / `bp_storage_rpc_resp_t`) for Core 1 protocol jobs needing file read/write operations.
  - Implement a streaming write handoff buffer (`bp_storage_stream_*`) allowing high-speed capture engines on Core 1 to pass captured data blocks to Core 0 for background disk writing.
- **Atomic Configuration Persistence and Corruption Recovery**:
  - Implement atomic configuration writes via a temporary swap file (`bpconfig.tmp` -> sync -> rename to `bpconfig.bp`) to prevent corruption during sudden power loss.
  - Add corruption fallback to restore default configuration settings safely if configuration files are unreadable or malformed.
- **Service Kernel Integration**:
  - Register `bp_storage_service()` in the Core 0 cooperative kernel dispatch (`bp_service(0)`).
- **Automated Verification**:
  - Host unit tests (`tests/test_storage_service.c`, `tests/test_storage_service.sh`) verifying mount state transitions, debouncing, RPC requests, streaming writes, error handling, and atomic config recovery under ASan/UBSan.

## Capabilities

### New Capabilities
- `storage`: Core 0 filesystem ownership, mount state machine, block device serialization, storage RPC, streaming capture handoff, and atomic configuration persistence.

### Modified Capabilities
<!-- None -->

## Impact

- **Affected Code**:
  - `src/services/bp_storage.h`, `src/services/bp_storage.c`: New storage service subsystem.
  - `src/fatfs/diskio.c`: Add `blockdev_lock` around physical disk I/O.
  - `src/pirate/storage.h`, `src/pirate/storage.c`: Refactor to delegate to `bp_storage`.
  - `src/pirate.c`: Remove Core 1 `storage_detect()` call; notify debounced detection.
  - `src/kernel/bp_kernel.c`: Hook `bp_storage_service()` into Core 0 service loop.
  - `src/commands/global/cmd_storage.h`, `src/commands/global/cmd_storage.c`: Add `storage` status command.
  - `src/commands.c`: Register `storage` command.
  - `src/CMakeLists.txt`: Build target updates.
- **Memory & Resource Budget**:
  - Zero dynamic heap allocation (`malloc`, `free`, `new`, `delete`).
  - Static memory within RP2350 <= 380 KB static RAM budget.
