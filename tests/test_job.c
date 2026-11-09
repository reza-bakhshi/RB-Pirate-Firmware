#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>

#include "kernel/bp_status.h"
#include "kernel/bp_job.h"

// Test 1: Submit and Run to Done
typedef struct test_job_ctx {
    uint32_t step;
    uint32_t max_steps;
} test_job_ctx_t;

static bp_status_t test_job_slice(void* ctx_ptr, uint8_t* out_percent) {
    test_job_ctx_t* ctx = (test_job_ctx_t*)ctx_ptr;
    ctx->step++;
    *out_percent = (uint8_t)((ctx->step * 100) / ctx->max_steps);
    bp_job_emit_progress(*out_percent, "processing");

    if (ctx->step >= ctx->max_steps) {
        return BP_STATUS_OK;
    }
    return BP_STATUS_BUSY;
}

static void test_submit_and_done(void) {
    printf("[RUN]  test_submit_and_done\n");
    assert(bp_job_subsystem_init() == BP_STATUS_OK);

    test_job_ctx_t ctx = { .step = 0, .max_steps = 4 };
    uint32_t jid = 0;

    assert(bp_job_submit("count_job", BP_JOB_PROTO_TEST, 5000, test_job_slice, &ctx, 1000, &jid) == BP_STATUS_OK);
    assert(jid > 0);

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

    // Drain output records
    bp_job_output_t out;
    uint32_t count = 0;
    while (bp_job_poll_output(&out) == BP_STATUS_OK) {
        assert(out.job_id == jid);
        count++;
    }
    assert(count >= 4);

    // Next service call should be idle
    assert(bp_job_service(1050) == 0);

    printf("[PASS] test_submit_and_done\n");
}

// Test 2: Busy Rejection
static void test_busy_rejection(void) {
    printf("[RUN]  test_busy_rejection\n");
    assert(bp_job_subsystem_init() == BP_STATUS_OK);

    test_job_ctx_t ctx = { .step = 0, .max_steps = 10 };
    uint32_t jid1 = 0, jid2 = 0;

    assert(bp_job_submit("job1", BP_JOB_PROTO_TEST, 5000, test_job_slice, &ctx, 1000, &jid1) == BP_STATUS_OK);

    // Attempt second job while job1 is queued
    assert(bp_job_submit("job2", BP_JOB_PROTO_TEST, 5000, test_job_slice, &ctx, 1000, &jid2) == BP_STATUS_BUSY);

    // Run one slice to put job1 in RUNNING state
    bp_job_service(1010);

    // Attempt second job while job1 is running
    assert(bp_job_submit("job2", BP_JOB_PROTO_TEST, 5000, test_job_slice, &ctx, 1010, &jid2) == BP_STATUS_BUSY);

    printf("[PASS] test_busy_rejection\n");
}

// Test 3: Cancellation and Latency
static void test_cancellation(void) {
    printf("[RUN]  test_cancellation\n");
    assert(bp_job_subsystem_init() == BP_STATUS_OK);

    test_job_ctx_t ctx = { .step = 0, .max_steps = 100 };
    uint32_t jid = 0;

    assert(bp_job_submit("long_job", BP_JOB_PROTO_TEST, 10000, test_job_slice, &ctx, 2000, &jid) == BP_STATUS_OK);

    // Slice 1
    bp_job_service(2010);

    // Cancel request and measure latency
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    assert(bp_job_cancel(jid) == BP_STATUS_OK);
    assert(bp_job_check_cancelled() == true);

    // Next service slice observes cancellation
    bp_job_service(2020);

    clock_gettime(CLOCK_MONOTONIC, &end);
    double latency_ms = (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_nsec - start.tv_nsec) / 1000000.0;
    printf("       cancellation latency: %.3f ms (must be < 100 ms)\n", latency_ms);
    assert(latency_ms < 100.0);

    bp_job_state_t state;
    bp_status_t status;
    assert(bp_job_get_state(jid, &state, &status, NULL) == BP_STATUS_OK);
    assert(state == BP_JOB_STATE_CANCELLED);
    assert(status == BP_STATUS_CANCELLED);

    printf("[PASS] test_cancellation\n");
}

// Test 4: Deadline Timeout
static void test_deadline_timeout(void) {
    printf("[RUN]  test_deadline_timeout\n");
    assert(bp_job_subsystem_init() == BP_STATUS_OK);

    test_job_ctx_t ctx = { .step = 0, .max_steps = 1000 };
    uint32_t jid = 0;

    // Timeout in 50 ms
    assert(bp_job_submit("timeout_job", BP_JOB_PROTO_TEST, 50, test_job_slice, &ctx, 1000, &jid) == BP_STATUS_OK);

    // t=1010 ms (10 ms elapsed, within deadline)
    assert(bp_job_service(1010) == 1);
    bp_job_state_t state;
    bp_status_t status;
    assert(bp_job_get_state(jid, &state, &status, NULL) == BP_STATUS_OK);
    assert(state == BP_JOB_STATE_RUNNING);

    // t=1060 ms (60 ms elapsed, timeout deadline expired)
    assert(bp_job_service(1060) == 1);
    assert(bp_job_get_state(jid, &state, &status, NULL) == BP_STATUS_OK);
    assert(state == BP_JOB_STATE_TIMEOUT);
    assert(status == BP_STATUS_TIMEOUT);

    printf("[PASS] test_deadline_timeout\n");
}

int main(void) {
    printf("=== Protocol Job Runtime Test Suite ===\n");
    test_submit_and_done();
    test_busy_rejection();
    test_cancellation();
    test_deadline_timeout();
    printf("=== ALL PROTOCOL JOB TESTS PASSED ===\n");
    return 0;
}
