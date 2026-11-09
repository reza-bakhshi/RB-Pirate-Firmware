#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>

#include "kernel/bp_status.h"
#include "kernel/bp_event.h"
#include "kernel/bp_timer.h"
#include "kernel/bp_work.h"
#include "kernel/bp_op.h"
#include "kernel/bp_log.h"
#include "kernel/bp_health.h"
#include "kernel/bp_kernel.h"
#include "services/bp_storage.h"
#include "system_config.h"
#include "modes.h"

struct _system_config system_config;
struct _mode modes[MAXPROTO];
void bio_init(void) {}
void fala_mode_change_hook(void) {}
const char* ui_term_color_error(void) { return ""; }
const char* ui_term_color_reset(void) { return ""; }

const mode_config_t system_config_json[1] = { {"", NULL, MODE_CONFIG_FORMAT_DECIMAL} };
const uint8_t system_config_json_count = 0;
void* mem_alloc(size_t s, uint32_t t) { (void)t; return malloc(s); }
void mem_free(void* p) { free(p); }
void bp_watchdog_heartbeat(uint8_t c) { (void)c; }

// Test 1: Event Queue
static void test_event_queue(void) {
    printf("[RUN]  test_event_queue\n");
    bp_event_t buffer[16];
    bp_event_queue_t q;

    // Bad init
    assert(bp_event_queue_init(NULL, buffer, 16) == BP_STATUS_INVALID_PARAM);
    assert(bp_event_queue_init(&q, buffer, 15) == BP_STATUS_INVALID_PARAM); // Not power of 2

    // Good init
    assert(bp_event_queue_init(&q, buffer, 16) == BP_STATUS_OK);
    assert(bp_event_queue_is_empty(&q));
    assert(!bp_event_queue_is_full(&q));
    assert(bp_event_queue_count(&q) == 0);

    // Fill to capacity
    for (uint32_t i = 0; i < 16; i++) {
        bp_event_t ev = {
            .id = (uint16_t)(BP_EV_CUSTOM_START + i),
            .source = 0,
            .timestamp_ms = i * 10,
            .data.u32[0] = i * 100,
            .data.u32[1] = i * 1000
        };
        assert(bp_event_queue_post(&q, &ev) == BP_STATUS_OK);
    }
    assert(bp_event_queue_is_full(&q));
    assert(bp_event_queue_count(&q) == 16);
    assert(bp_event_queue_high_water(&q) == 16);

    // Overflow check
    bp_event_t overflow_ev = { .id = 999 };
    assert(bp_event_queue_post(&q, &overflow_ev) == BP_STATUS_QUEUE_FULL);
    assert(bp_event_queue_drops(&q) == 1);

    // Drain and verify FIFO order
    for (uint32_t i = 0; i < 16; i++) {
        bp_event_t popped;
        assert(bp_event_queue_poll(&q, &popped) == BP_STATUS_OK);
        assert(popped.id == BP_EV_CUSTOM_START + i);
        assert(popped.timestamp_ms == i * 10);
        assert(popped.data.u32[0] == i * 100);
        assert(popped.data.u32[1] == i * 1000);
    }
    assert(bp_event_queue_is_empty(&q));

    // Underflow check
    bp_event_t empty_ev;
    assert(bp_event_queue_poll(&q, &empty_ev) == BP_STATUS_QUEUE_EMPTY);

    printf("[PASS] test_event_queue\n");
}

// Test 2: Soft Timers
static volatile uint32_t s_timer1_fired = 0;
static volatile uint32_t s_timer2_fired = 0;

static void timer1_cb(void* arg) {
    (void)arg;
    s_timer1_fired++;
}

static void timer2_cb(void* arg) {
    (void)arg;
    s_timer2_fired++;
}

static void test_soft_timers(void) {
    printf("[RUN]  test_soft_timers\n");
    assert(bp_timer_subsystem_init() == BP_STATUS_OK);

    s_timer1_fired = 0;
    s_timer2_fired = 0;

    bp_timer_t* t1 = bp_timer_create(timer1_cb, NULL, false); // 10 ms one-shot
    bp_timer_t* t2 = bp_timer_create(timer2_cb, NULL, true);  // 5 ms periodic
    assert(t1 != NULL);
    assert(t2 != NULL);

    assert(bp_timer_start(t1, 10) == BP_STATUS_OK);
    assert(bp_timer_start(t2, 5) == BP_STATUS_OK);
    assert(bp_timer_active_count() == 2);

    // Advance 4 ms -> neither should expire
    bp_timer_tick_ms(4);
    assert(bp_timer_dispatch() == 0);
    assert(s_timer1_fired == 0);
    assert(s_timer2_fired == 0);

    // Advance 2 ms (total 6 ms) -> t2 expires (periodic 5 ms), t1 has 4 ms left
    bp_timer_tick_ms(2);
    assert(bp_timer_dispatch() == 1);
    assert(s_timer1_fired == 0);
    assert(s_timer2_fired == 1);

    // Advance 5 ms (total 11 ms) -> both expire (t1 one-shot, t2 periodic second firing)
    bp_timer_tick_ms(5);
    assert(bp_timer_dispatch() == 2);
    assert(s_timer1_fired == 1);
    assert(s_timer2_fired == 2);

    // Advance 10 ms -> t1 was one-shot so it should not fire again; t2 fires twice
    bp_timer_tick_ms(5);
    assert(bp_timer_dispatch() == 1);
    bp_timer_tick_ms(5);
    assert(bp_timer_dispatch() == 1);
    assert(s_timer1_fired == 1);
    assert(s_timer2_fired == 4);

    // Stop t2 and destroy both
    assert(bp_timer_stop(t2) == BP_STATUS_OK);
    assert(bp_timer_active_count() == 0);
    assert(bp_timer_destroy(t1) == BP_STATUS_OK);
    assert(bp_timer_destroy(t2) == BP_STATUS_OK);

    printf("[PASS] test_soft_timers\n");
}

// Test 3: Deferred Work
static uint32_t s_work_executed = 0;
static void work_handler(void* arg) {
    uint32_t val = (uint32_t)(uintptr_t)arg;
    s_work_executed += val;
}

static void test_deferred_work(void) {
    printf("[RUN]  test_deferred_work\n");
    bp_work_queue_t wq;
    assert(bp_work_queue_init(&wq) == BP_STATUS_OK);

    s_work_executed = 0;
    bp_work_item_t* w1 = bp_work_item_create(&wq, work_handler, (void*)(uintptr_t)10);
    bp_work_item_t* w2 = bp_work_item_create(&wq, work_handler, (void*)(uintptr_t)20);
    assert(w1 && w2);

    assert(bp_work_schedule(w1) == BP_STATUS_OK);
    assert(bp_work_schedule(w2) == BP_STATUS_OK);
    assert(bp_work_pending_count(&wq) == 2);

    // Cancel w2
    assert(bp_work_cancel(w2) == BP_STATUS_OK);
    assert(bp_work_pending_count(&wq) == 1);

    // Dispatch
    assert(bp_work_dispatch(&wq) == 1);
    assert(s_work_executed == 10);
    assert(bp_work_pending_count(&wq) == 0);

    // Clean up
    assert(bp_work_item_destroy(w1) == BP_STATUS_OK);
    assert(bp_work_item_destroy(w2) == BP_STATUS_OK);

    printf("[PASS] test_deferred_work\n");
}

// Test 4: Cancellable Operations and Latency
static void test_cancellable_op(void) {
    printf("[RUN]  test_cancellable_op\n");
    assert(bp_op_subsystem_init() == BP_STATUS_OK);

    bp_op_t op;
    assert(bp_op_begin(&op, "flash_read", 5000, 100) == BP_STATUS_OK);
    assert(bp_op_get_active() == &op);
    assert(!bp_op_is_cancelled(&op));

    // Measure cancellation latency
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    // Simulate asynchronous Ctrl+C
    assert(bp_op_request_cancel_active(BP_CANCEL_REASON_CTRL_C) == BP_STATUS_OK);
    assert(bp_op_is_cancelled(&op));
    assert(op.cancel_reason == BP_CANCEL_REASON_CTRL_C);

    clock_gettime(CLOCK_MONOTONIC, &end);
    double latency_ms = (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_nsec - start.tv_nsec) / 1000000.0;
    printf("       cancellation latency: %.3f ms (must be < 100 ms)\n", latency_ms);
    assert(latency_ms < 100.0);

    bp_op_end(&op);
    assert(bp_op_get_active() == NULL);

    printf("[PASS] test_cancellable_op\n");
}

// Test 5: Safe Log Ring
static void test_log_ring(void) {
    printf("[RUN]  test_log_ring\n");
    assert(bp_log_subsystem_init() == BP_STATUS_OK);

    assert(bp_log_post(BP_LOG_INFO, "SPI", "Transfer 1024 bytes", 1, 1500) == BP_STATUS_OK);
    assert(bp_log_post(BP_LOG_WARN, "PSU", "Current spike 250mA", 0, 1505) == BP_STATUS_OK);
    assert(bp_log_count() == 2);

    bp_log_entry_t entry;
    assert(bp_log_pop(&entry) == BP_STATUS_OK);
    assert(entry.core == 1);
    assert(entry.level == BP_LOG_INFO);
    assert(strcmp(entry.tag, "SPI") == 0);
    assert(strcmp(entry.msg, "Transfer 1024 bytes") == 0);

    assert(bp_log_pop(&entry) == BP_STATUS_OK);
    assert(entry.core == 0);
    assert(entry.level == BP_LOG_WARN);
    assert(strcmp(entry.tag, "PSU") == 0);

    assert(bp_log_count() == 0);
    printf("[PASS] test_log_ring\n");
}

// Test 6: Full Kernel Run & Service Integration
static uint32_t s_kernel_events_received = 0;
static void test_kernel_event_handler(const bp_event_t* ev) {
    if (ev->id == BP_EV_BUTTON_PRESS) {
        s_kernel_events_received++;
    }
}

static void test_kernel_service_integration(void) {
    printf("[RUN]  test_kernel_service_integration\n");
    assert(bp_kernel_init() == BP_STATUS_OK);

    s_kernel_events_received = 0;
    bp_kernel_set_event_handler(0, test_kernel_event_handler);

    // Post an event to Core 0
    bp_event_t ev = {
        .id = BP_EV_BUTTON_PRESS,
        .source = 1,
        .timestamp_ms = 50
    };
    assert(bp_kernel_post_event(0, &ev) == BP_STATUS_OK);

    // Service Core 0
    uint32_t work = bp_service(0);
    assert(work >= 1);
    assert(s_kernel_events_received == 1);

    // Verify metrics
    bp_kernel_metrics_t m;
    bp_health_get_metrics(&m);
    assert(m.core0_ticks >= 1);
    assert(m.core0_events_dispatched == 1);

    printf("[PASS] test_kernel_service_integration\n");
}

int main(void) {
    printf("=== Service Kernel Test Suite ===\n");
    test_event_queue();
    test_soft_timers();
    test_deferred_work();
    test_cancellable_op();
    test_log_ring();
    test_kernel_service_integration();
    printf("=== ALL SERVICE KERNEL TESTS PASSED ===\n");
    return 0;
}
