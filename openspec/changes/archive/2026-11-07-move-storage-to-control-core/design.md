# Design: Move Storage and File Systems to Control Core

## Architectural Overview

In the RB-Pirate dual-core architecture, Core 0 (CTRL) owns all file systems, configuration persistence, mount lifecycle, and user interactions. Core 1 (PROTO) owns real-time protocol engines, capture hardware, and DMA/PIO channels.

Direct filesystem access (`f_*`) across cores creates critical failure points:
1. FatFS is not re-entrant across preemptive multi-core execution without complex global locks.
2. In the baseline code, `core1_infinite_loop()` was calling `storage_detect()`, which invoked `storage_mount()` / `f_mount()` and `storage_unmount()` on Core 1 while Core 0 was running interactive shell commands.
3. TinyUSB MSC running on Core 1 performs raw SCSI block operations (`disk_read`, `disk_write`) concurrently with Core 0 filesystem operations on the shared SPI bus.

Step B5 refactors the storage subsystem to establish single-core ownership on Core 0:

```
+-------------------------------------------------------------------------------+
|                                CORE 0 (CTRL)                                  |
|                                                                               |
|  +--------------------+    +--------------------+    +---------------------+  |
|  | Terminal / Commands|    | Config Persistence |    | Mount State Machine |  |
|  +---------+----------+    +---------+----------+    +----------+----------+  |
|            |                         |                          |             |
|            v                         v                          v             |
|       +----+-------------------------+--------------------------+----+        |
|       |                     bp_storage Service                       |        |
|       |        - Debounced Card Detect                               |        |
|       |        - Open File Reference Counting                        |        |
|       |        - Mount / Unmount / Format / FatFS API Wrapper        |        |
|       |        - Telemetry Counters (mounts, errors, IO counts)      |        |
|       +------------------------------+-------------------------------+        |
|                                      |                                        |
|                                      v                                        |
|                           +--------------------+                              |
|                           |   FatFS (f_*)      |                              |
|                           +----------+---------+                              |
|                                      |                                        |
|                                      v                                        |
|                           +--------------------+                              |
|                           |   blockdev_lock    |<-------+                     |
|                           +----------+---------+        |                     |
|                                      |                  |                     |
+--------------------------------------|------------------|---------------------+
                                       |                  |
+--------------------------------------|------------------|---------------------+
|                                      |                  |                     |
|                                      |        +---------+-----------+         |
|                                      |        | TinyUSB MSC (SCSI)  |         |
|                                      |        +---------------------+         |
|                                      v                                        |
|                           +--------------------+                              |
|                           | Physical Disk I/O  |                              |
|                           | (SPI Flash / SD)   |                              |
|                           +--------------------+                              |
|                                                                               |
|  +-------------------------+            +----------------------------------+  |
|  | Protocol Engine / Dump  |            | Capture Engine (Logic Analyzer)  |  |
|  +------------+------------+            +-----------------+----------------+  |
|               |                                           |                   |
|               v (Storage RPC Queue)                       v (Stream Chunks)   |
|   s_storage_req_q / resp_q                     s_storage_stream_q             |
|                                                                               |
|                                CORE 1 (PROTO)                                 |
+-------------------------------------------------------------------------------+
```

## Detailed Subsystems

### 1. Mount State Machine (`bp_storage.h`, `bp_storage.c`)

The state machine manages the lifecycle of the filesystem:
```c
typedef enum bp_storage_state {
    BP_STORAGE_STATE_UNMOUNTED = 0,
    BP_STORAGE_STATE_MOUNTING,
    BP_STORAGE_STATE_MOUNTED,
    BP_STORAGE_STATE_UNMOUNTING,
    BP_STORAGE_STATE_ERROR,
} bp_storage_state_t;
```
- **State transitions**:
  - `UNMOUNTED` -> `MOUNTING` -> `MOUNTED` (or `ERROR` on mount failure).
  - `MOUNTED` -> `UNMOUNTING` -> `UNMOUNTED`.
- **Guarded unmount**:
  - Checks `bp_storage_get_open_count()`. If > 0, returns `BP_STATUS_ERR_BUSY`.
  - Checks if MSC is actively transferring blocks (`s_msc_active_timestamp`).
- **Debounced Card Detection**:
  - Core 1 samples the analog MUX (`hw_adc_raw[HW_ADC_MUX_CARD_DETECT]`) and posts `bp_storage_notify_card_detect(detected)`.
  - Core 0 runs debounce logic in `bp_storage_service()`. A state change must remain stable for >= 5 consecutive checks before initiating mount or unmount.

### 2. Block Device Concurrency Serialization (`blockdev_lock`)

In `src/fatfs/diskio.c`, low-level calls (`disk_read`, `disk_write`, `disk_ioctl`, `disk_status`, `disk_initialize`) wrap their operations with:
```c
static mutex_t s_blockdev_mutex;

void blockdev_lock_init(void);
void blockdev_lock_acquire(void);
void blockdev_lock_release(void);
```
On RP2350 target builds, `s_blockdev_mutex` uses the SDK's hardware-backed mutex. On host test builds, standard pthread or stub mutex primitives are used. This guarantees that concurrent access from Core 0 FatFS and Core 1 TinyUSB MSC cannot interleave SPI transactions or corrupt sector transfers.

### 3. Storage RPC for Protocol Jobs

Core 1 protocol jobs interact with files via lock-free SPSC queues:
```c
typedef enum bp_storage_rpc_type {
    BP_STORAGE_RPC_READ = 1,
    BP_STORAGE_RPC_WRITE,
    BP_STORAGE_RPC_STAT,
} bp_storage_rpc_type_t;

typedef struct bp_storage_rpc_req {
    uint32_t request_id;
    bp_storage_rpc_type_t type;
    char path[64];
    uint32_t offset;
    uint32_t size;
    uint8_t *data;
} bp_storage_rpc_req_t;

typedef struct bp_storage_rpc_resp {
    uint32_t request_id;
    bp_status_t status;
    uint32_t bytes_transferred;
} bp_storage_rpc_resp_t;
```
Core 1 calls `bp_storage_rpc_call(&req, &resp, timeout_ms)`, which queues the request, yields via `bp_watchdog_heartbeat(1)`, and waits for Core 0 to service the request and return the response.

### 4. Streaming Capture Buffer Handoff

For high-speed logic captures:
```c
typedef struct bp_storage_chunk {
    const uint8_t *buffer;
    uint32_t size;
} bp_storage_chunk_t;
```
Core 1 pushes full blocks into a static ring (`s_stream_q`). Core 0 drains this ring in `bp_storage_service()` and executes sequential `f_write()` calls without stalling Core 1 capture timing.

### 5. Atomic Configuration Persistence

`bp_storage_save_config()` writes configuration JSON to `bpconfig.tmp`. Once closed, it flushes and executes an atomic replace:
```c
f_unlink(CONFIG_FILENAME);
f_rename(CONFIG_TEMP_FILENAME, CONFIG_FILENAME);
```
On startup, `bp_storage_load_config()` checks the file integrity. If parsing fails, it safely falls back to defaults (`system_config_reset()`) without assertion or panic.

### 6. Storage Telemetry and Status Command

Tracked metrics:
- Mount attempts (`mount_attempts`)
- Mount successes (`mount_successes`)
- Mount failures (`mount_errors`)
- Unmount count (`unmount_count`)
- Read errors (`read_errors`)
- Write errors (`write_errors`)
- RPC requests handled (`rpc_requests`)
- Active open file handles (`open_file_count`)

The CLI command `storage` provides real-time visibility into these metrics and actionable recovery guidance when in error state.
