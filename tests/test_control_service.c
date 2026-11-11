/* Actual control dispatcher remains runnable while a worker slice is blocked. */
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include "services/bp_control.h"
#include "kernel/bp_job.h"
#include "services/bp_protocol_call.h"
#include <sched.h>
static _Thread_local unsigned core;
unsigned get_core_num(void) { return core; }
static unsigned polls, requests;
static uint8_t entered, unblock;
static void poll_hook(void) {
    assert(core == 0);
    polls++;
    bp_control_poll(); /* Nested output/MSC polling must not recurse. */
}
static bool request_hook(uint8_t code) { requests++; return code == 42; }
static bp_status_t stalled_slice(void* ctx, uint8_t* percent) {
    (void)ctx; (void)percent;
    __atomic_store_n(&entered, 1, __ATOMIC_RELEASE);
    while (!__atomic_load_n(&unblock, __ATOMIC_ACQUIRE)) {}
    return BP_STATUS_CANCELLED;
}
static void *worker(void* unused) {
    (void)unused; core = 1;
    bp_control_poll();
    bp_control_poll_core(0); /* Even a wrong caller-supplied ID cannot bypass ownership. */
    assert(!bp_control_request(42));
    bp_job_service(2);
    return NULL;
}
static uint8_t stop_calls;
uint32_t bp_service(uint8_t requested_core) {
    assert(requested_core == 0 && core == 0);
    bp_control_poll();
    sched_yield();
    return 0;
}
static void *transaction_worker(void* unused) {
    (void)unused; core = 1;
    while (!__atomic_load_n(&stop_calls, __ATOMIC_ACQUIRE)) { bp_job_service(2); sched_yield(); }
    return NULL;
}
static bp_status_t transaction(void* ctx, uint8_t* percent) {
    assert(core == 1);
    *(uint32_t*)ctx = 0x12345678;
    *percent = 100;
    return BP_STATUS_OK;
}
static void test_transaction_calls(void) {
    pthread_t thread;
    assert(!pthread_create(&thread, NULL, transaction_worker, NULL));
    for (unsigned i = 0; i < 1000; i++) {
        uint32_t result = 0;
        assert(bp_protocol_call("transaction", transaction, &result) == BP_STATUS_OK);
        assert(result == 0x12345678);
    }
    __atomic_store_n(&stop_calls, 1, __ATOMIC_RELEASE);
    assert(!pthread_join(thread, NULL));
    core = 1;
    assert(bp_protocol_call("wrong_core", transaction, NULL) == BP_STATUS_INVALID_PARAM);
    core = 0;
    assert(bp_protocol_call("null", NULL, NULL) == BP_STATUS_INVALID_PARAM);
    puts("[PASS] 1000 worker transactions publish results before returning to Core 0");
}

int main(void) {
    bp_control_poll(); /* Safe before boot hooks are installed. */
    bp_control_init(poll_hook, request_hook);
    assert(bp_control_request(42) && !bp_control_request(0));
    bp_job_subsystem_init();
    uint32_t job;
    assert(bp_job_submit("blocked", 0, 0, stalled_slice, NULL, 1, &job) == BP_STATUS_OK);
    pthread_t thread;
    assert(!pthread_create(&thread, NULL, worker, NULL));
    while (!__atomic_load_n(&entered, __ATOMIC_ACQUIRE)) {}
    for (unsigned i = 0; i < 1000; i++) bp_control_poll();
    assert(polls == 1000 && requests == 2);
    assert(bp_job_cancel(job) == BP_STATUS_OK);
    __atomic_store_n(&unblock, 1, __ATOMIC_RELEASE);
    assert(!pthread_join(thread, NULL));
    assert(bp_job_get_active_id() == 0);
    test_transaction_calls();
    puts("[PASS] Control polling stays on Core 0, does not recurse, and continues with a blocked Core 1 job");
}
