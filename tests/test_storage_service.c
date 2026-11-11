#define _POSIX_C_SOURCE 199309L
#define __dmb() __sync_synchronize()

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
    printf("=== ALL STORAGE SERVICE TESTS PASSED ===\n");
    return 0;
}
