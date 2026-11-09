/*
 * Host-side model test for the terminal TX drop/overflow policy.
 *
 * usb_tx.c is hardware-coupled, so this test exercises the same policy on the
 * real SPSC queue used by the firmware: empty, full, wrap-around, drop and
 * one-shot truncation marker.
 */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Host mocks for the Pico SDK primitives used by spsc_queue.h. */
#define __dmb() __sync_synchronize()
static inline void tight_loop_contents(void) {
    __asm__ volatile("" ::: "memory");
}

#include "spsc_queue.h"

#define TEST_CAPACITY 64u

typedef struct {
    spsc_queue_t q;
    uint8_t storage[TEST_CAPACITY];
    uint32_t dropped_bytes;
    uint32_t overflow_events;
    uint32_t high_water;
    bool overflow_pending;
    uint32_t overflow_bytes;
} tx_model_t;

static void model_init(tx_model_t *m) {
    memset(m, 0, sizeof(*m));
    spsc_queue_init(&m->q, m->storage, TEST_CAPACITY);
}

static void model_raw_add(tx_model_t *m, uint8_t value) {
    assert(spsc_queue_try_add(&m->q, value));
    uint32_t level = spsc_queue_level(&m->q);
    if (level > m->high_water) {
        m->high_water = level;
    }
}

static void model_emit_marker(tx_model_t *m) {
    if (!m->overflow_pending) {
        return;
    }
    char marker[64];
    int len = snprintf(marker, sizeof(marker), "\r\n[output truncated: %lu bytes]\r\n",
                       (unsigned long)m->overflow_bytes);
    assert(len > 0 && (uint32_t)len < sizeof(marker));
    if (spsc_queue_free(&m->q) <= (uint32_t)len) {
        return;
    }
    for (int i = 0; i < len; i++) {
        model_raw_add(m, (uint8_t)marker[i]);
    }
    m->overflow_pending = false;
    m->overflow_bytes = 0;
}

static bool model_put(tx_model_t *m, uint8_t value) {
    model_emit_marker(m);
    if (spsc_queue_try_add(&m->q, value)) {
        uint32_t level = spsc_queue_level(&m->q);
        if (level > m->high_water) {
            m->high_water = level;
        }
        return true;
    }
    if (!m->overflow_pending) {
        m->overflow_pending = true;
        m->overflow_bytes = 0;
        m->overflow_events++;
    }
    m->dropped_bytes++;
    m->overflow_bytes++;
    return false;
}

static void test_empty(void) {
    tx_model_t m;
    model_init(&m);
    assert(spsc_queue_is_empty(&m.q));
    assert(model_put(&m, 'A'));
    assert(!spsc_queue_is_empty(&m.q));
    uint8_t out = 0;
    assert(spsc_queue_try_remove(&m.q, &out));
    assert(out == 'A');
    assert(spsc_queue_is_empty(&m.q));
    printf("PASS empty\n");
}

static void test_full_and_drop(void) {
    tx_model_t m;
    model_init(&m);
    // Capacity - 1 slots are usable.
    for (uint32_t i = 0; i < TEST_CAPACITY - 1u; i++) {
        assert(model_put(&m, (uint8_t)('a' + i)));
    }
    assert(spsc_queue_is_full(&m.q));
    assert(!model_put(&m, 'X'));
    assert(m.dropped_bytes == 1);
    assert(m.overflow_events == 1);
    assert(m.overflow_pending);
    printf("PASS full/drop\n");
}

static void test_marker(void) {
    tx_model_t m;
    model_init(&m);
    for (uint32_t i = 0; i < TEST_CAPACITY - 1u; i++) {
        assert(model_put(&m, (uint8_t)i));
    }
    assert(!model_put(&m, 'X'));
    assert(!model_put(&m, 'Y'));

    // Drain the ring, then a successful put should first emit the marker.
    uint8_t out;
    while (spsc_queue_try_remove(&m.q, &out)) {
    }
    assert(model_put(&m, 'Z'));

    char seen[128] = { 0 };
    uint32_t n = 0;
    while (spsc_queue_try_remove(&m.q, &out)) {
        assert(n + 1 < sizeof(seen));
        seen[n++] = (char)out;
    }
    assert(strstr(seen, "[output truncated: 2 bytes]") != NULL);
    assert(!m.overflow_pending);
    assert(m.dropped_bytes == 2);
    assert(m.overflow_events == 1);
    printf("PASS marker\n");
}

static void test_wrap(void) {
    tx_model_t m;
    model_init(&m);
    for (uint32_t i = 0; i < 1000u; i++) {
        assert(model_put(&m, (uint8_t)(i & 0xffu)));
        uint8_t out;
        assert(spsc_queue_try_remove(&m.q, &out));
        assert(out == (uint8_t)(i & 0xffu));
    }
    assert(spsc_queue_is_empty(&m.q));
    assert(m.high_water > 0);
    printf("PASS wrap-around\n");
}

int main(void) {
    test_empty();
    test_full_and_drop();
    test_marker();
    test_wrap();
    printf("ALL TERMINAL TX TESTS PASSED\n");
    return 0;
}
