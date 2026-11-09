/**
 * @file bp_storage.h
 * @brief Control Core (Core 0) storage service, mount state machine, and storage RPC.
 *
 * Owns FatFS filesystem access, card mount/unmount lifecycle, card detect debouncing,
 * storage error telemetry, and cross-core storage RPC/streaming handoff.
 */

#ifndef BP_STORAGE_H
#define BP_STORAGE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "kernel/bp_status.h"
#include "pirate/storage.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Storage mount state machine states.
 */
typedef enum bp_storage_state {
    BP_STORAGE_STATE_UNMOUNTED = 0, ///< Filesystem unmounted
    BP_STORAGE_STATE_MOUNTING,      ///< Mount operation in progress
    BP_STORAGE_STATE_MOUNTED,       ///< Filesystem mounted and accessible
    BP_STORAGE_STATE_UNMOUNTING,    ///< Unmount operation in progress
    BP_STORAGE_STATE_ERROR,         ///< Mount failed or medium error
} bp_storage_state_t;

/**
 * @brief Storage operational statistics and telemetry.
 */
typedef struct bp_storage_stats {
    uint32_t mount_attempts;         ///< Total mount attempts
    uint32_t mount_successes;        ///< Successful mounts
    uint32_t mount_errors;           ///< Mount failures
    uint32_t unmount_count;          ///< Clean unmounts
    uint32_t read_errors;            ///< Low-level or FatFS read errors
    uint32_t write_errors;           ///< Low-level or FatFS write errors
    uint32_t rpc_requests;           ///< Storage RPC requests handled
    uint32_t rpc_timeouts;           ///< Storage RPC timeouts
    uint32_t rpc_drops;              ///< Storage RPC queue full drops
    uint32_t stream_chunks_written;  ///< Capture stream chunks written to disk
    uint32_t stream_chunks_dropped;  ///< Capture stream chunks dropped (queue full)
    uint32_t open_file_count;        ///< Currently open file handles
    bp_storage_state_t state;        ///< Current mount state
    uint8_t last_error_code;         ///< Last FatFS FRESULT error code
    uint8_t fat_type;                ///< FAT filesystem type (12, 16, 32, exFAT)
    float capacity_gb;               ///< Total storage capacity in gigabytes
    bool card_detected;              ///< Debounced physical card presence
} bp_storage_stats_t;

/**
 * @brief Storage RPC operation types.
 */
typedef enum bp_storage_rpc_type {
    BP_STORAGE_RPC_NONE = 0,
    BP_STORAGE_RPC_READ,             ///< Read from file
    BP_STORAGE_RPC_WRITE,            ///< Write to file
    BP_STORAGE_RPC_APPEND,           ///< Append to file
    BP_STORAGE_RPC_STAT,             ///< Check if file exists / get size
} bp_storage_rpc_type_t;

#define BP_STORAGE_RPC_PATH_MAX 64

/**
 * @brief Storage RPC request packet.
 */
typedef struct bp_storage_rpc_req {
    uint32_t request_id;
    bp_storage_rpc_type_t type;
    char path[BP_STORAGE_RPC_PATH_MAX];
    uint32_t offset;
    uint32_t size;
    uint8_t *data;
} bp_storage_rpc_req_t;

/**
 * @brief Storage RPC response packet.
 */
typedef struct bp_storage_rpc_resp {
    uint32_t request_id;
    bp_status_t status;
    uint32_t bytes_transferred;
} bp_storage_rpc_resp_t;

/**
 * @brief Capture stream chunk structure.
 */
typedef struct bp_storage_stream_chunk {
    const uint8_t *buffer;
    uint32_t size;
} bp_storage_stream_chunk_t;

/* --- Core 0 Lifecycle & State Machine --- */

/**
 * @brief Initialize storage service and hardware CS pins.
 */
bp_status_t bp_storage_init(void);

/**
 * @brief Mount filesystem on Core 0.
 * @return BP_STATUS_OK on success, BP_STATUS_ERR_IO on failure.
 */
bp_status_t bp_storage_mount(void);

/**
 * @brief Unmount filesystem on Core 0.
 * @return BP_STATUS_OK on success, BP_STATUS_ERR_BUSY if files open.
 */
bp_status_t bp_storage_unmount(void);

/**
 * @brief Format storage media on Core 0.
 * @return BP_STATUS_OK on success, error status code otherwise.
 */
bp_status_t bp_storage_format(void);

/**
 * @brief Print human-readable FatFS error message.
 * @param res FatFS result code (FRESULT)
 */
void bp_storage_file_error(uint8_t res);

/**
 * @brief Core 0 cooperative service function.
 * Called in bp_service(0) to process RPC requests, debounced card detect, and streaming writes.
 */
void bp_storage_service(void);

/**
 * @brief Get current storage state machine state.
 */
bp_storage_state_t bp_storage_get_state(void);

/**
 * @brief Get storage telemetry and performance counters.
 */
void bp_storage_get_stats(bp_storage_stats_t *out_stats);

/**
 * @brief Notify card detection state from Core 1 or AMUX sweep.
 * Thread-safe: sets internal raw presence flag debounced on Core 0.
 */
void bp_storage_notify_card_detect(bool inserted);

/**
 * @brief Record a block I/O read or write error in storage telemetry.
 * Thread-safe: can be called from interrupt or Core 1.
 * @param is_write true for write error, false for read error.
 */
void bp_storage_record_io_error(bool is_write);

/* --- Open File Reference Counting --- */

/**
 * @brief Increment open file counter.
 */
void bp_storage_file_opened(void);

/**
 * @brief Decrement open file counter.
 */
void bp_storage_file_closed(void);

/**
 * @brief Get active open file handle count.
 */
uint32_t bp_storage_get_open_count(void);

/* --- Core 1 Storage RPC --- */

/**
 * @brief Execute a storage RPC call from Core 1.
 * Blocks Core 1 with watchdog heartbeat until Core 0 fulfills request or timeout.
 * @param req         Pointer to request struct
 * @param resp        Pointer to response struct to receive results
 * @param timeout_ms  Maximum wait time in milliseconds
 * @return BP_STATUS_OK on success, BP_STATUS_ERR_TIMEOUT on timeout, BP_STATUS_ERR_RESOURCE if queue full.
 */
bp_status_t bp_storage_rpc_call(const bp_storage_rpc_req_t *req, bp_storage_rpc_resp_t *resp, uint32_t timeout_ms);

/* --- Streaming Capture Buffer Handoff --- */

/**
 * @brief Open capture stream output file on Core 0.
 */
bp_status_t bp_storage_stream_open(const char *filepath);

/**
 * @brief Commit a captured data chunk from Core 1 into the stream queue.
 * Lock-free, safe to call from Core 1 protocol loops.
 */
bp_status_t bp_storage_stream_write(const uint8_t *buffer, uint32_t size);

/**
 * @brief Close and flush capture stream file on Core 0.
 */
bp_status_t bp_storage_stream_close(void);

/* --- Configuration Persistence & Fallback --- */

/**
 * @brief Atomically save system configuration to flash.
 */
bp_status_t bp_storage_save_config(void);

/**
 * @brief Load system configuration from flash with fallback on corruption.
 */
bp_status_t bp_storage_load_config(void);

/**
 * @brief Save mode configuration file.
 */
bp_status_t bp_storage_save_mode(const char *filename, const mode_config_t *config_t, uint8_t count);

/**
 * @brief Load mode configuration file.
 */
bp_status_t bp_storage_load_mode(const char *filename, const mode_config_t *config_t, uint8_t count);

/* --- Block Device Lock --- */

/**
 * @brief Acquire block device serialization lock.
 */
void blockdev_lock_acquire(void);

/**
 * @brief Release block device serialization lock.
 */
void blockdev_lock_release(void);

#ifdef __cplusplus
}
#endif

#endif // BP_STORAGE_H
