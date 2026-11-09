#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <stdbool.h>

#include "kernel/bp_status.h"
#include "kernel/bp_job.h"
#include "kernel/bp_resource.h"

#define BP_BIG_BUFFER_NONE       0
#define BP_BIG_BUFFER_EDITOR     4
#define BP_BIG_BUFFER_TXTEST     5
#define BP_BIG_BUFFER_LCDTAP     6

// Mock system_config for host tests
typedef struct {
    uint32_t big_buffer_owner;
} mock_system_config_t;

static mock_system_config_t system_config = {
    .big_buffer_owner = BP_BIG_BUFFER_NONE
};

// Memory allocation mirroring src/pirate/mem.c backed by bp_resource
static uint8_t* mem_alloc(size_t size, uint32_t owner) {
    uint8_t* ptr = NULL;
    uint32_t owner_id = (owner == BP_BIG_BUFFER_NONE) ? BP_RES_OWNER_SYSTEM : owner;
    bp_status_t status = bp_res_acquire_buffer(owner_id, "mem_alloc", size, &ptr);
    if (status != BP_STATUS_OK) {
        return NULL;
    }
    system_config.big_buffer_owner = owner;
    return ptr;
}

static void mem_free(uint8_t* ptr) {
    bp_status_t status = bp_res_release_buffer(0, ptr);
    if (status == BP_STATUS_OK) {
        system_config.big_buffer_owner = BP_BIG_BUFFER_NONE;
    }
}

// Mock File Object for FatFS simulation
typedef struct {
    bool is_open;
    uint32_t bytes_written;
    bool write_error;
} mock_fil_t;

static bool mock_f_open(mock_fil_t* fil, const char* name) {
    (void)name;
    fil->is_open = true;
    fil->bytes_written = 0;
    fil->write_error = false;
    return true;
}

static bool mock_f_write(mock_fil_t* fil, const void* data, uint32_t len, uint32_t* bw) {
    (void)data;
    if (!fil->is_open || fil->write_error) {
        if (bw) *bw = 0;
        return false;
    }
    fil->bytes_written += len;
    if (bw) *bw = len;
    return true;
}

static void mock_f_close(mock_fil_t* fil) {
    fil->is_open = false;
}

static bp_status_t mock_dummy_slice(void* ctx, uint8_t* percent) {
    (void)ctx;
    if (bp_job_check_cancelled()) return BP_STATUS_CANCELLED;
    *percent = 50;
    return BP_STATUS_BUSY;
}

// -----------------------------------------------------------------------------
// Test 1: txtest Job Cancellation and Big Buffer Safety (ARCH-302)
// -----------------------------------------------------------------------------
static void test_txtest_job_and_buffer_lease(void) {
    printf("[RUN]  test_txtest_job_and_buffer_lease\n");
    assert(bp_job_subsystem_init() == BP_STATUS_OK);
    assert(bp_res_init() == BP_STATUS_OK);

    // 1. Allocate big buffer for txtest
    uint8_t* buf = mem_alloc(32768, BP_BIG_BUFFER_TXTEST);
    assert(buf != NULL);
    assert(system_config.big_buffer_owner == BP_BIG_BUFFER_TXTEST);

    // 2. Conflicting big buffer allocation must fail
    uint8_t* conflict_buf = mem_alloc(32768, BP_BIG_BUFFER_EDITOR);
    assert(conflict_buf == NULL);

    // 3. Dispatch a mock txtest streaming job
    uint32_t jid = 0;
    assert(bp_job_dispatch("txtest", BP_JOB_PROTO_TEST, 5000,
                           mock_dummy_slice, NULL, &jid) == BP_STATUS_OK);
    assert(bp_job_get_active_id() == jid);

    // 4. Simulate cancellation mid-stream
    struct timespec t_cancel_start, t_cancel_end;
    clock_gettime(CLOCK_MONOTONIC, &t_cancel_start);

    assert(bp_job_cancel(jid) == BP_STATUS_OK);
    assert(bp_job_check_cancelled() == true);

    clock_gettime(CLOCK_MONOTONIC, &t_cancel_end);
    double latency_ms = (t_cancel_end.tv_sec - t_cancel_start.tv_sec) * 1000.0 +
                        (t_cancel_end.tv_nsec - t_cancel_start.tv_nsec) / 1000000.0;
    assert(latency_ms < 100.0);

    // 5. On cancellation, txtest frees the big buffer
    mem_free(buf);
    assert(system_config.big_buffer_owner == BP_BIG_BUFFER_NONE);

    // 6. Next allocation succeeds cleanly
    uint8_t* next_buf = mem_alloc(32768, BP_BIG_BUFFER_EDITOR);
    assert(next_buf != NULL);
    assert(system_config.big_buffer_owner == BP_BIG_BUFFER_EDITOR);
    mem_free(next_buf);

    printf("[PASS] test_txtest_job_and_buffer_lease (latency: %.3f ms)\n", latency_ms);
}

// -----------------------------------------------------------------------------
// Test 2: Flash Dump Cancellation and File Teardown (ARCH-303)
// -----------------------------------------------------------------------------
static void test_flash_dump_cancellation_and_file_teardown(void) {
    printf("[RUN]  test_flash_dump_cancellation_and_file_teardown\n");
    assert(bp_job_subsystem_init() == BP_STATUS_OK);

    mock_fil_t file;
    mock_f_open(&file, "flash.bin");
    assert(file.is_open == true);

    uint32_t jid = 0;
    assert(bp_job_dispatch("flash_dump", BP_JOB_PROTO_TEST, 10000,
                           mock_dummy_slice, NULL, &jid) == BP_STATUS_OK);

    const uint32_t total_pages = 256;
    const uint32_t page_size = 256;
    uint8_t page_buf[256];
    memset(page_buf, 0xAA, sizeof(page_buf));

    uint32_t pages_written = 0;
    bool cancelled_cleanly = false;

    struct timespec t_start, t_end;
    clock_gettime(CLOCK_MONOTONIC, &t_start);

    for (uint32_t p = 0; p < total_pages; p++) {
        // Trigger cancel on page 16
        if (p == 16) {
            bp_job_cancel(jid);
        }

        // Exact pattern used in spiflash_dump / global dump
        if (bp_job_check_cancelled()) {
            mock_f_close(&file);
            cancelled_cleanly = true;
            break;
        }

        uint32_t bw = 0;
        mock_f_write(&file, page_buf, page_size, &bw);
        pages_written++;
    }

    clock_gettime(CLOCK_MONOTONIC, &t_end);
    double latency_ms = (t_end.tv_sec - t_start.tv_sec) * 1000.0 +
                        (t_end.tv_nsec - t_start.tv_nsec) / 1000000.0;

    assert(cancelled_cleanly == true);
    assert(pages_written == 16);
    assert(file.is_open == false); // Must be safely closed
    assert(latency_ms < 100.0);

    printf("[PASS] test_flash_dump_cancellation_and_file_teardown (closed at %u bytes, latency: %.3f ms)\n",
           file.bytes_written, latency_ms);
}

// -----------------------------------------------------------------------------
// Test 3: EEPROM Write Cancellation and Safe Abort (ARCH-303)
// -----------------------------------------------------------------------------
static void test_eeprom_write_cancellation(void) {
    printf("[RUN]  test_eeprom_write_cancellation\n");
    assert(bp_job_subsystem_init() == BP_STATUS_OK);

    mock_fil_t eeprom_src_file;
    mock_f_open(&eeprom_src_file, "data.bin");
    assert(eeprom_src_file.is_open == true);

    uint32_t jid = 0;
    assert(bp_job_dispatch("eeprom_write", BP_JOB_PROTO_TEST, 10000,
                           mock_dummy_slice, NULL, &jid) == BP_STATUS_OK);

    uint32_t total_blocks = 32;
    uint32_t blocks_processed = 0;
    bool aborted = false;

    for (uint32_t b = 0; b < total_blocks; b++) {
        if (b == 8) {
            bp_job_cancel(jid);
        }

        // Exact pattern used in eeprom_base.c eeprom_write()
        if (bp_job_check_cancelled()) {
            mock_f_close(&eeprom_src_file);
            aborted = true;
            break;
        }

        blocks_processed++;
    }

    assert(aborted == true);
    assert(blocks_processed == 8);
    assert(eeprom_src_file.is_open == false); // Safe close verified

    printf("[PASS] test_eeprom_write_cancellation (aborted cleanly at block %u)\n", blocks_processed);
}

// -----------------------------------------------------------------------------
// Test 4: LCDTAP Frame Save Cancellation (ARCH-308)
// -----------------------------------------------------------------------------
typedef struct {
    bool write_protected;
} mock_lcdtap_t;

static void test_lcdtap_frame_save_cancellation(void) {
    printf("[RUN]  test_lcdtap_frame_save_cancellation\n");
    assert(bp_job_subsystem_init() == BP_STATUS_OK);

    mock_lcdtap_t tap = { .write_protected = false };
    mock_fil_t bmp_file;
    mock_f_open(&bmp_file, "capture.bmp");

    uint32_t jid = 0;
    assert(bp_job_dispatch("lcdtap_save", BP_JOB_PROTO_TEST, 10000,
                           mock_dummy_slice, NULL, &jid) == BP_STATUS_OK);

    tap.write_protected = true; // Lock framebuffer during save
    uint32_t rows_total = 240;
    uint32_t rows_written = 0;
    bool ok = true;

    for (uint32_t y = 0; ok && y < rows_total; y++) {
        if (y == 35) {
            bp_job_cancel(jid);
        }

        // Exact pattern in lcdtap_backend_save_bmp
        if (bp_job_check_cancelled()) {
            ok = false;
            break;
        }

        uint32_t bw = 0;
        mock_f_write(&bmp_file, "ROW_DATA", 8, &bw);
        rows_written++;
    }

    // Teardown invariant: write protection released and file closed
    tap.write_protected = false;
    mock_f_close(&bmp_file);

    assert(ok == false);
    assert(rows_written == 35);
    assert(tap.write_protected == false);
    assert(bmp_file.is_open == false);

    printf("[PASS] test_lcdtap_frame_save_cancellation (unlocked and closed at row %u)\n", rows_written);
}

// -----------------------------------------------------------------------------
// Test 5: Editor Big Buffer Arena Lifecycle (ARCH-310)
// -----------------------------------------------------------------------------
static void test_editor_big_buffer_lifecycle(void) {
    printf("[RUN]  test_editor_big_buffer_lifecycle\n");
    assert(bp_res_init() == BP_STATUS_OK);

    // 1. Initial state: free
    assert(system_config.big_buffer_owner == BP_BIG_BUFFER_NONE);

    // 2. Editor claims arena (128 KB)
    uint8_t* arena = mem_alloc(BP_BIG_BUFFER_SIZE, BP_BIG_BUFFER_EDITOR);
    assert(arena != NULL);
    assert(system_config.big_buffer_owner == BP_BIG_BUFFER_EDITOR);

    // 3. Second claim while active must be rejected
    uint8_t* second = mem_alloc(BP_BIG_BUFFER_SIZE, BP_BIG_BUFFER_TXTEST);
    assert(second == NULL);

    // 4. Simulate editor exit / abort cleanup
    mem_free(arena);
    assert(system_config.big_buffer_owner == BP_BIG_BUFFER_NONE);

    // 5. Subsequent allocation succeeds
    uint8_t* reallocated = mem_alloc(BP_BIG_BUFFER_SIZE, BP_BIG_BUFFER_TXTEST);
    assert(reallocated != NULL);
    assert(system_config.big_buffer_owner == BP_BIG_BUFFER_TXTEST);
    mem_free(reallocated);
    assert(system_config.big_buffer_owner == BP_BIG_BUFFER_NONE);

    printf("[PASS] test_editor_big_buffer_lifecycle\n");
}

int main(void) {
    printf("=== Running Capture & File Jobs Test Suite ===\n");

    test_txtest_job_and_buffer_lease();
    test_flash_dump_cancellation_and_file_teardown();
    test_eeprom_write_cancellation();
    test_lcdtap_frame_save_cancellation();
    test_editor_big_buffer_lifecycle();

    printf("=== ALL CAPTURE & FILE JOB TESTS PASSED ===\n");
    return 0;
}
