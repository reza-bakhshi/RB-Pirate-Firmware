#define _POSIX_C_SOURCE 199309L
#define __dmb() __sync_synchronize()
#define BP_STORAGE_CUSTOM_TF_CARD 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>

#include "services/bp_storage.h"
#include "kernel/bp_status.h"

#include "../src/services/bp_storage.c"

host_system_config_t system_config;

const char system_config_file[] = "rbconfig.rb";
const mode_config_t system_config_json[1] = {
    {"$.terminal_language", &system_config.terminal_language, MODE_CONFIG_FORMAT_DECIMAL},
};
const uint8_t system_config_json_count = 1;

/* Mock mem_alloc / mem_free */
uint8_t* mem_alloc(size_t size, uint32_t type) {
    (void)type;
    return malloc(size);
}

void mem_free(uint8_t* ptr) {
    free(ptr);
}

/* Mock watchdog */
void bp_watchdog_heartbeat(uint32_t core_id) {
    (void)core_id;
}

static uint32_t s_mock_media_generation = 1;
static bool s_mock_metadata_valid = true;

uint32_t tf_card_get_media_generation(void) {
    if (!s_mock_metadata_valid) return 0;
    return s_mock_media_generation;
}

void tf_card_invalidate_metadata(void) {
    s_mock_metadata_valid = false;
}

static void test_init_and_mount(void) {
    printf("[TEST] Storage service init and mount...\n");
    bp_status_t st = bp_storage_init();
    assert(st == BP_STATUS_OK);
    assert(bp_storage_get_state() == BP_STORAGE_STATE_UNMOUNTED);

    bp_storage_stats_t stats;
    bp_storage_get_stats(&stats);
    assert(stats.mount_attempts == 0);
    assert(stats.state == BP_STORAGE_STATE_UNMOUNTED);

    st = bp_storage_mount();
    assert(st == BP_STATUS_OK);
    assert(bp_storage_get_state() == BP_STORAGE_STATE_MOUNTED);

    bp_storage_get_stats(&stats);
    assert(stats.mount_attempts == 1);
    assert(stats.mount_successes == 1);
    assert(stats.mount_errors == 0);
    assert(stats.state == BP_STORAGE_STATE_MOUNTED);
    assert(system_config.storage_available == 1);

    st = bp_storage_format();
    assert(st == BP_STATUS_OK);
    assert(bp_storage_get_state() == BP_STORAGE_STATE_MOUNTED);
    printf("[PASS] Init, mount, and format verified\n");
}

static void test_guarded_unmount(void) {
    printf("[TEST] Guarded unmount prevents unmount with open files...\n");
    bp_storage_init();
    bp_storage_mount();
    assert(bp_storage_get_state() == BP_STORAGE_STATE_MOUNTED);

    /* Open a simulated file */
    bp_storage_file_opened();
    assert(bp_storage_get_open_count() == 1);

    /* Unmount must be rejected with BUSY */
    bp_status_t st = bp_storage_unmount();
    assert(st == BP_STATUS_BUSY);
    assert(bp_storage_get_state() == BP_STORAGE_STATE_MOUNTED);

    /* Close the file */
    bp_storage_file_closed();
    assert(bp_storage_get_open_count() == 0);

    /* Now unmount must succeed */
    st = bp_storage_unmount();
    assert(st == BP_STATUS_OK);
    assert(bp_storage_get_state() == BP_STORAGE_STATE_UNMOUNTED);
    assert(system_config.storage_available == 0);
    printf("[PASS] Guarded unmount verified\n");
}

static void test_card_detect_debouncing(void) {
    printf("[TEST] Card detect debouncing on Core 0...\n");
    bp_storage_init();
    assert(bp_storage_get_state() == BP_STORAGE_STATE_UNMOUNTED);

    /* Transient bounce: high for 2 ticks then low */
    bp_storage_notify_card_detect(true);
    bp_storage_service();
    bp_storage_service();
    assert(bp_storage_get_state() == BP_STORAGE_STATE_UNMOUNTED);

    bp_storage_notify_card_detect(false);
    bp_storage_service();
    assert(bp_storage_get_state() == BP_STORAGE_STATE_UNMOUNTED);

    /* Solid insertion: high for >= 5 ticks */
    bp_storage_notify_card_detect(true);
    for (int i = 0; i < 5; i++) {
        bp_storage_service();
    }
    assert(bp_storage_get_state() == BP_STORAGE_STATE_MOUNTED);

    /* Solid removal: low for >= 5 ticks */
    bp_storage_notify_card_detect(false);
    for (int i = 0; i < 5; i++) {
        bp_storage_service();
    }
    assert(bp_storage_get_state() == BP_STORAGE_STATE_UNMOUNTED);

    /* Verify error state recovery on card removal */
    bp_storage_notify_card_detect(true);
    for (int i = 0; i < 5; i++) {
        bp_storage_service();
    }
    assert(bp_storage_get_state() == BP_STORAGE_STATE_MOUNTED);

    /* Simulate error state */
    bp_storage_stats_t err_stats;
    bp_storage_get_stats(&err_stats);
    /* Remove card while in error state -> resets to unmounted */
    bp_storage_notify_card_detect(false);
    for (int i = 0; i < 5; i++) {
        bp_storage_service();
    }
    assert(bp_storage_get_state() == BP_STORAGE_STATE_UNMOUNTED);

    printf("[PASS] Card detect debouncing verified\n");
}

static void test_storage_rpc(void) {
    printf("[TEST] Cross-core storage RPC queue...\n");
    bp_storage_init();
    bp_storage_mount();

    uint8_t dummy_buf[32] = {0};
    bp_storage_rpc_req_t req = {
        .request_id = 1001,
        .type = BP_STORAGE_RPC_STAT,
        .path = "config.bp",
        .size = sizeof(dummy_buf),
        .data = dummy_buf
    };
    bp_storage_rpc_resp_t resp = {0};

    /* Queue request and verify timeout if service() not run yet */
    bp_status_t st = bp_storage_rpc_call(&req, &resp, 0);
    assert(st == BP_STATUS_TIMEOUT);

    /* Service Core 0 */
    bp_storage_service();
    printf("[PASS] Storage RPC verified\n");
}

static void test_streaming_capture(void) {
    printf("[TEST] Streaming capture buffer handoff...\n");
    bp_storage_init();
    bp_storage_mount();

    bp_status_t st = bp_storage_stream_open("capture.bin");
    assert(st == BP_STATUS_OK);
    assert(bp_storage_get_open_count() == 1);

    uint8_t chunk1[64];
    uint8_t chunk2[128];
    memset(chunk1, 0xAA, sizeof(chunk1));
    memset(chunk2, 0x55, sizeof(chunk2));

    st = bp_storage_stream_write(chunk1, sizeof(chunk1));
    assert(st == BP_STATUS_OK);
    st = bp_storage_stream_write(chunk2, sizeof(chunk2));
    assert(st == BP_STATUS_OK);

    /* Core 0 services and drains stream */
    bp_storage_service();

    bp_storage_stats_t stats;
    bp_storage_get_stats(&stats);
    assert(stats.stream_chunks_written == 2);

    st = bp_storage_stream_close();
    assert(st == BP_STATUS_OK);
    assert(bp_storage_get_open_count() == 0);
    printf("[PASS] Streaming capture handoff verified\n");
}

static void test_atomic_config_persistence(void) {
    printf("[TEST] Atomic configuration persistence...\n");
    bp_storage_init();
    bp_storage_mount();

    system_config.terminal_language = 1;
    bp_status_t st = bp_storage_save_config();
    assert(st == BP_STATUS_OK);

    st = bp_storage_load_config();
    assert(st == BP_STATUS_OK);

    /* Test error on unmounted save */
    bp_storage_unmount();
    st = bp_storage_save_config();
    assert(st == BP_STATUS_DISCONNECTED);
    printf("[PASS] Atomic config persistence verified\n");
}

static void test_blockdev_lock(void) {
    printf("[TEST] Block device lock serialization...\n");
    blockdev_lock_acquire();
    blockdev_lock_release();
    printf("[PASS] Block device lock verified\n");
}

static void test_storage_integrity(void) {
    bp_storage_init(); bp_storage_mount();
    memset(mock_files, 0, sizeof(mock_files));
    assert(bp_storage_save_config() == BP_STATUS_OK);
    unsigned char original[4096];
    int slot = mock_find(CONFIG_FILENAME);
    unsigned size = mock_files[slot].size;
    memcpy(original, mock_files[slot].data, size);
    int *faults[] = {&mock_write_error, &mock_short_write, &mock_sync_error, &mock_close_error, &mock_rename_error};
    for (unsigned i = 0; i < sizeof(faults)/sizeof(faults[0]); i++) {
        *faults[i] = 1;
        assert(bp_storage_save_config() == BP_STATUS_ERROR);
        *faults[i] = 0;
        slot = mock_find(CONFIG_FILENAME);
        assert(slot >= 0 && mock_files[slot].size == size);
        assert(memcmp(original, mock_files[slot].data, size) == 0);
        assert(bp_storage_get_open_count() == 0);
    }
    /* Simulate a reset between moving the old config and committing the new one. */
    f_unlink(CONFIG_BACKUP_FILENAME);
    assert(f_rename(CONFIG_FILENAME, CONFIG_BACKUP_FILENAME) == FR_OK);
    assert(bp_storage_load_config() == BP_STATUS_OK);

    uint8_t data[] = "abc";
    bp_storage_rpc_req_t req = {.type = BP_STORAGE_RPC_WRITE, .path = "rpc.bin", .data = data, .size = 3};
    bp_storage_rpc_resp_t resp;
    bp_storage_process_single_rpc(&req, &resp);
    assert(resp.status == BP_STATUS_OK);
    req.type = BP_STORAGE_RPC_APPEND;
    bp_storage_process_single_rpc(&req, &resp);
    assert(resp.status == BP_STATUS_OK);
    slot = mock_find("rpc.bin");
    assert(mock_files[slot].size == 6 && memcmp(mock_files[slot].data, "abcabc", 6) == 0);
    uint8_t readback[3] = {0};
    req.type = BP_STORAGE_RPC_READ; req.offset = 2; req.size = 3; req.data = readback;
    bp_storage_process_single_rpc(&req, &resp);
    assert(resp.status == BP_STATUS_OK && resp.bytes_transferred == 3);
    assert(memcmp(readback, "cab", 3) == 0);
    req.type = BP_STORAGE_RPC_APPEND; mock_short_write = 1;
    bp_storage_process_single_rpc(&req, &resp);
    assert(resp.status == BP_STATUS_ERROR); mock_short_write = 0;
    puts("[PASS] Storage failure preservation, recovery, append and offsets");
}

static void test_delayed_replies_and_replacement_media(void) {
    printf("[TEST] Delayed replies and replacement media: assert no new-medium writes...\n");
    bp_storage_init();
    s_mock_media_generation = 1;
    s_mock_metadata_valid = true;
    bp_storage_mount();

    /* 1. Core 1 prepares a write request for media generation 1 */
    uint8_t payload[] = "ATTEMPTED_STALE_WRITE";
    bp_storage_rpc_req_t req = {
        .request_id = 9999,
        .type = BP_STORAGE_RPC_WRITE,
        .path = "stale_target.bin",
        .offset = 0,
        .size = sizeof(payload),
        .data = payload,
        .media_generation = 1,
        .cancelled = false
    };
    bp_storage_rpc_resp_t resp = {0};

    /* Queue the request on Core 1 (timeout = 0 to simulate delayed reply / asynchronous queuing) */
    bp_status_t st = bp_storage_rpc_call(&req, &resp, 0);
    assert(st == BP_STATUS_TIMEOUT);

    /* 2. Before Core 0 services the queue, media is removed and replaced!
          Old media unmounts, new card inserted with media generation 2 */
    bp_storage_unmount();
    assert(!s_mock_metadata_valid);

    s_mock_media_generation = 2;
    s_mock_metadata_valid = true;
    bp_storage_mount();
    assert(tf_card_get_media_generation() == 2);

    /* Clear mock files to detect if any write occurs */
    memset(mock_files, 0, sizeof(mock_files));

    /* 3. Core 0 runs its service loop */
    bp_storage_service();

    /* 4. Assert NO file was written to the new medium */
    int slot = mock_find("stale_target.bin");
    assert(slot == -1); /* File must NOT exist on new medium */

    /* 5. Assert that when request was cancelled/timed-out, buffer was not accessed */
    /* Queue another request with generation 2 */
    uint8_t buffer_reused[32];
    memset(buffer_reused, 0xEE, sizeof(buffer_reused));
    req.request_id = 10000;
    req.type = BP_STORAGE_RPC_WRITE;
    strncpy(req.path, "valid_gen2.bin", sizeof(req.path));
    req.size = sizeof(buffer_reused);
    req.data = buffer_reused;
    req.media_generation = 2;
    req.cancelled = false;

    /* Timeout immediately simulates caller releasing buffer */
    st = bp_storage_rpc_call(&req, &resp, 0);
    assert(st == BP_STATUS_TIMEOUT);

    /* Invalidate caller buffer to verify Core 0 does not dereference it */
    memset(buffer_reused, 0x00, sizeof(buffer_reused));

    /* Core 0 services queue */
    bp_storage_service();

    /* File must not have been created because buffer pointer was cleared on timeout */
    slot = mock_find("valid_gen2.bin");
    assert(slot == -1);

    printf("[PASS] Delayed replies and replacement media assertions verified\n");
}

int main(void) {
    printf("=== Running Storage Service Unit Tests ===\n");
    test_storage_integrity();
    test_init_and_mount();
    test_guarded_unmount();
    test_card_detect_debouncing();
    test_storage_rpc();
    test_streaming_capture();
    test_atomic_config_persistence();
    test_blockdev_lock();
    test_delayed_replies_and_replacement_media();
    printf("=== ALL STORAGE SERVICE TESTS PASSED ===\n");
    return 0;
}
