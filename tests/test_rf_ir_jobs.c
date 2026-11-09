#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>

#include "kernel/bp_status.h"
#include "kernel/bp_job.h"
#include "kernel/bp_resource.h"

static void dummy_irq(void) {}

// Mock slice for Sub-GHz Scan
typedef struct {
    uint32_t current_channel;
    uint32_t total_channels;
} mock_subghz_ctx_t;

static bp_status_t mock_subghz_scan_slice(void* ctx_ptr, uint8_t* out_percent) {
    mock_subghz_ctx_t* ctx = (mock_subghz_ctx_t*)ctx_ptr;
    if (bp_job_check_cancelled()) {
        return BP_STATUS_CANCELLED;
    }
    ctx->current_channel++;
    *out_percent = (uint8_t)((ctx->current_channel * 100) / ctx->total_channels);
    char buf[32];
    snprintf(buf, sizeof(buf), "CH %u Peak -85dBm", ctx->current_channel);
    bp_job_emit_progress(*out_percent, buf);

    if (ctx->current_channel >= ctx->total_channels) {
        return BP_STATUS_OK;
    }
    return BP_STATUS_BUSY;
}

// Mock slice for RFID Scanner
typedef struct {
    uint32_t tags_found;
    uint32_t max_tags;
} mock_rfid_ctx_t;

static bp_status_t mock_rfid_slice(void* ctx_ptr, uint8_t* out_percent) {
    mock_rfid_ctx_t* ctx = (mock_rfid_ctx_t*)ctx_ptr;
    if (bp_job_check_cancelled()) {
        return BP_STATUS_CANCELLED;
    }
    ctx->tags_found++;
    *out_percent = (uint8_t)((ctx->tags_found * 100) / ctx->max_tags);
    bp_job_emit_progress(*out_percent, "Tag detected");

    if (ctx->tags_found >= ctx->max_tags) {
        return BP_STATUS_OK;
    }
    return BP_STATUS_BUSY;
}

// Mock slice for RF24 Receiver
typedef struct {
    uint32_t packets;
} mock_rf24_ctx_t;

static bp_status_t mock_rf24_slice(void* ctx_ptr, uint8_t* out_percent) {
    mock_rf24_ctx_t* ctx = (mock_rf24_ctx_t*)ctx_ptr;
    if (bp_job_check_cancelled()) {
        return BP_STATUS_CANCELLED;
    }
    ctx->packets++;
    *out_percent = 50;
    return BP_STATUS_BUSY;
}

// Mock slice for IR Jammer
static bp_status_t mock_ir_jam_slice(void* ctx_ptr, uint8_t* out_percent) {
    (void)ctx_ptr;
    if (bp_job_check_cancelled()) {
        return BP_STATUS_CANCELLED;
    }
    *out_percent = 25;
    return BP_STATUS_BUSY;
}

// -----------------------------------------------------------------------------
// Test 1: Sub-GHz Job Dispatch and Progress
// -----------------------------------------------------------------------------
static void test_subghz_job_dispatch_and_progress(void) {
    printf("[RUN]  test_subghz_job_dispatch_and_progress\n");
    assert(bp_job_subsystem_init() == BP_STATUS_OK);
    assert(bp_res_init() == BP_STATUS_OK);

    mock_subghz_ctx_t ctx = { .current_channel = 0, .total_channels = 4 };
    uint32_t jid = 0;

    assert(bp_job_dispatch("subghz_scan", BP_JOB_PROTO_SUBGHZ, 10000,
                           mock_subghz_scan_slice, &ctx, &jid) == BP_STATUS_OK);
    assert(jid > 0);
    assert(bp_job_get_active_id() == jid);

    bp_job_state_t state;
    bp_status_t status;
    uint8_t pct;
    assert(bp_job_get_state(jid, &state, &status, &pct) == BP_STATUS_OK);
    assert(state == BP_JOB_STATE_QUEUED);

    // Step 1: 25%
    assert(bp_job_service(1010) == 1);
    assert(bp_job_get_state(jid, &state, &status, &pct) == BP_STATUS_OK);
    assert(state == BP_JOB_STATE_RUNNING);
    assert(pct == 25);

    // Step 2: 50%
    assert(bp_job_service(1020) == 1);
    assert(bp_job_get_state(jid, &state, &status, &pct) == BP_STATUS_OK);
    assert(pct == 50);

    // Step 3: 75%
    assert(bp_job_service(1030) == 1);
    assert(bp_job_get_state(jid, &state, &status, &pct) == BP_STATUS_OK);
    assert(pct == 75);

    // Step 4: 100% -> Done
    assert(bp_job_service(1040) == 1);
    assert(bp_job_get_state(jid, &state, &status, &pct) == BP_STATUS_OK);
    assert(state == BP_JOB_STATE_DONE);
    assert(status == BP_STATUS_OK);
    assert(pct == 100);

    // Drain output ring records
    bp_job_output_t out;
    uint32_t count = 0;
    while (bp_job_poll_output(&out) == BP_STATUS_OK) {
        assert(out.job_id == jid);
        count++;
    }
    assert(count >= 4);

    // No active job
    assert(bp_job_get_active_id() == 0);
    printf("[PASS] test_subghz_job_dispatch_and_progress\n");
}

// -----------------------------------------------------------------------------
// Test 2: RFID Job Cancellation and Automatic Resource Cleanup
// -----------------------------------------------------------------------------
static void test_rfid_cancellation_and_resource_cleanup(void) {
    printf("[RUN]  test_rfid_cancellation_and_resource_cleanup\n");
    assert(bp_job_subsystem_init() == BP_STATUS_OK);
    assert(bp_res_init() == BP_STATUS_OK);

    mock_rfid_ctx_t ctx = { .tags_found = 0, .max_tags = 100 };
    uint32_t jid = 0;

    assert(bp_job_dispatch("rfid_scanner", BP_JOB_PROTO_RFID, 0,
                           mock_rfid_slice, &ctx, &jid) == BP_STATUS_OK);
    assert(jid > 0);

    // Lease peripherals under BP_RES_OWNER_JOB
    uint32_t pio_sm = 0;
    assert(bp_res_claim_unused_pio_sm(0, BP_RES_OWNER_JOB, "pn532_sm", &pio_sm) == BP_STATUS_OK);
    uint32_t dma_ch = 0;
    assert(bp_res_claim_unused_dma_ch(BP_RES_OWNER_JOB, "pn532_dma", &dma_ch) == BP_STATUS_OK);
    assert(bp_res_acquire_irq(12, BP_RES_OWNER_JOB, "pn532_irq", dummy_irq) == BP_STATUS_OK);

    // Verify resources are claimed
    assert(bp_res_get_owner(BP_RES_CLASS_PIO_SM, 0 * BP_RES_SM_PER_PIO + pio_sm) == BP_RES_OWNER_JOB);
    assert(bp_res_get_owner(BP_RES_CLASS_DMA_CH, dma_ch) == BP_RES_OWNER_JOB);
    assert(bp_res_get_owner(BP_RES_CLASS_IRQ, 12) == BP_RES_OWNER_JOB);
    assert(bp_res_count_owned(BP_RES_OWNER_JOB) == 3);

    // Run one service slice
    assert(bp_job_service(1010) == 1);
    assert(bp_job_get_active_id() == jid);

    // Cancel job and measure latency
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    assert(bp_job_cancel(jid) == BP_STATUS_OK);
    assert(bp_job_check_cancelled() == true);

    // Service Core 1 to process cancellation
    assert(bp_job_service(1020) == 1);
    clock_gettime(CLOCK_MONOTONIC, &t1);

    double elapsed_ms = (t1.tv_sec - t0.tv_sec) * 1000.0 + (t1.tv_nsec - t0.tv_nsec) / 1000000.0;
    printf("       cancellation latency: %.3f ms (must be < 100 ms)\n", elapsed_ms);
    assert(elapsed_ms < 100.0);

    bp_job_state_t state;
    bp_status_t status;
    assert(bp_job_get_state(jid, &state, &status, NULL) == BP_STATUS_OK);
    assert(state == BP_JOB_STATE_CANCELLED);
    assert(status == BP_STATUS_CANCELLED);

    // Verify ALL hardware leases owned by BP_RES_OWNER_JOB were automatically released!
    assert(bp_res_is_free(BP_RES_CLASS_PIO_SM, 0 * BP_RES_SM_PER_PIO + pio_sm));
    assert(bp_res_is_free(BP_RES_CLASS_DMA_CH, dma_ch));
    assert(bp_res_is_free(BP_RES_CLASS_IRQ, 12));
    assert(bp_res_count_owned(BP_RES_OWNER_JOB) == 0);

    printf("[PASS] test_rfid_cancellation_and_resource_cleanup\n");
}

// -----------------------------------------------------------------------------
// Test 3: RF24 Job Deadline Timeout and Teardown
// -----------------------------------------------------------------------------
static void test_rf24_timeout_and_teardown(void) {
    printf("[RUN]  test_rf24_timeout_and_teardown\n");
    assert(bp_job_subsystem_init() == BP_STATUS_OK);
    assert(bp_res_init() == BP_STATUS_OK);

    mock_rf24_ctx_t ctx = { .packets = 0 };
    uint32_t jid = 0;

    // 200 ms timeout
    assert(bp_job_dispatch("rf24_receive", BP_JOB_PROTO_RF24, 200,
                           mock_rf24_slice, &ctx, &jid) == BP_STATUS_OK);

    uint32_t dma_ch = 0;
    assert(bp_res_claim_unused_dma_ch(BP_RES_OWNER_JOB, "nrf24_dma", &dma_ch) == BP_STATUS_OK);
    assert(bp_res_get_owner(BP_RES_CLASS_DMA_CH, dma_ch) == BP_RES_OWNER_JOB);

    // Service at t=1000 (starts running)
    assert(bp_job_service(1000) == 1);
    bp_job_state_t state;
    assert(bp_job_get_state(jid, &state, NULL, NULL) == BP_STATUS_OK);
    assert(state == BP_JOB_STATE_RUNNING);

    // Service at t=1100 (still running)
    assert(bp_job_service(1100) == 1);
    assert(bp_job_get_state(jid, &state, NULL, NULL) == BP_STATUS_OK);
    assert(state == BP_JOB_STATE_RUNNING);

    // Service at t=1250 (exceeds 200ms timeout)
    assert(bp_job_service(1250) == 1);
    bp_status_t status;
    assert(bp_job_get_state(jid, &state, &status, NULL) == BP_STATUS_OK);
    assert(state == BP_JOB_STATE_TIMEOUT);
    assert(status == BP_STATUS_TIMEOUT);

    // Leased resource must be released
    assert(bp_res_is_free(BP_RES_CLASS_DMA_CH, dma_ch));
    assert(bp_res_count_owned(BP_RES_OWNER_JOB) == 0);

    printf("[PASS] test_rf24_timeout_and_teardown\n");
}

// -----------------------------------------------------------------------------
// Test 4: Infrared Job Slice Cancellation
// -----------------------------------------------------------------------------
static void test_ir_job_slice_cancellation(void) {
    printf("[RUN]  test_ir_job_slice_cancellation\n");
    assert(bp_job_subsystem_init() == BP_STATUS_OK);
    assert(bp_res_init() == BP_STATUS_OK);

    uint32_t jid = 0;
    assert(bp_job_dispatch("ir_jam", BP_JOB_PROTO_INFRARED, 0,
                           mock_ir_jam_slice, NULL, &jid) == BP_STATUS_OK);

    uint32_t pio_sm = 0;
    assert(bp_res_claim_unused_pio_sm(1, BP_RES_OWNER_JOB, "ir_tx_sm", &pio_sm) == BP_STATUS_OK);
    assert(bp_res_get_owner(BP_RES_CLASS_PIO_SM, 1 * BP_RES_SM_PER_PIO + pio_sm) == BP_RES_OWNER_JOB);

    assert(bp_job_service(1000) == 1);
    assert(bp_job_cancel(jid) == BP_STATUS_OK);
    assert(bp_job_service(1010) == 1);

    bp_job_state_t state;
    bp_status_t status;
    assert(bp_job_get_state(jid, &state, &status, NULL) == BP_STATUS_OK);
    assert(state == BP_JOB_STATE_CANCELLED);
    assert(status == BP_STATUS_CANCELLED);

    assert(bp_res_is_free(BP_RES_CLASS_PIO_SM, 1 * BP_RES_SM_PER_PIO + pio_sm));
    assert(bp_res_count_owned(BP_RES_OWNER_JOB) == 0);

    printf("[PASS] test_ir_job_slice_cancellation\n");
}

// -----------------------------------------------------------------------------
// Test 5: Concurrency Guard - Reject Second Active Dispatch
// -----------------------------------------------------------------------------
static void test_concurrency_guard(void) {
    printf("[RUN]  test_concurrency_guard\n");
    assert(bp_job_subsystem_init() == BP_STATUS_OK);

    mock_subghz_ctx_t ctx1 = { .current_channel = 0, .total_channels = 10 };
    mock_rfid_ctx_t ctx2 = { .tags_found = 0, .max_tags = 10 };
    uint32_t jid1 = 0, jid2 = 0;

    assert(bp_job_dispatch("subghz_scan", BP_JOB_PROTO_SUBGHZ, 5000,
                           mock_subghz_scan_slice, &ctx1, &jid1) == BP_STATUS_OK);

    // Reject second job while jid1 is QUEUED
    assert(bp_job_dispatch("rfid_scanner", BP_JOB_PROTO_RFID, 5000,
                           mock_rfid_slice, &ctx2, &jid2) == BP_STATUS_BUSY);

    // Step once to make jid1 RUNNING
    assert(bp_job_service(1000) == 1);

    // Reject second job while jid1 is RUNNING
    assert(bp_job_dispatch("rfid_scanner", BP_JOB_PROTO_RFID, 5000,
                           mock_rfid_slice, &ctx2, &jid2) == BP_STATUS_BUSY);

    // Cancel jid1
    assert(bp_job_cancel(jid1) == BP_STATUS_OK);
    assert(bp_job_service(1010) == 1);

    // Now jid2 can be dispatched
    assert(bp_job_dispatch("rfid_scanner", BP_JOB_PROTO_RFID, 5000,
                           mock_rfid_slice, &ctx2, &jid2) == BP_STATUS_OK);
    assert(jid2 > 0);

    printf("[PASS] test_concurrency_guard\n");
}

int main(void) {
    printf("=== RF & IR Protocol Jobs Test Suite ===\n");
    test_subghz_job_dispatch_and_progress();
    test_rfid_cancellation_and_resource_cleanup();
    test_rf24_timeout_and_teardown();
    test_ir_job_slice_cancellation();
    test_concurrency_guard();
    printf("=== ALL RF & IR PROTOCOL JOB TESTS PASSED ===\n");
    return 0;
}
