#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "pirate/mem_pool.h"

static void test_pool_basic(void) {
    printf("[RUN]  test_pool_basic\n");
    uint8_t buffer[256];
    bp_pool_t pool;

    /* 256 bytes / 32 bytes = 8 blocks */
    bool ok = bp_pool_init(&pool, buffer, sizeof(buffer), 32);
    assert(ok);
    assert(pool.capacity == 8);
    assert(bp_pool_get_used(&pool) == 0);
    assert(bp_pool_get_free(&pool) == 8);
    assert(bp_pool_get_peak(&pool) == 0);

    void *b0 = bp_pool_alloc(&pool);
    void *b1 = bp_pool_alloc(&pool);
    assert(b0 != NULL);
    assert(b1 != NULL);
    assert(b0 != b1);
    assert(bp_pool_get_used(&pool) == 2);
    assert(bp_pool_get_peak(&pool) == 2);

    /* Free b0 */
    bool freed = bp_pool_free(&pool, b0);
    assert(freed);
    assert(bp_pool_get_used(&pool) == 1);
    assert(bp_pool_get_free(&pool) == 7);

    /* Reallocate */
    void *b0_again = bp_pool_alloc(&pool);
    assert(b0_again == b0);
    assert(bp_pool_get_used(&pool) == 2);

    /* Free all */
    assert(bp_pool_free(&pool, b1));
    assert(bp_pool_free(&pool, b0_again));
    assert(bp_pool_get_used(&pool) == 0);
    assert(bp_pool_get_free(&pool) == 8);
    assert(bp_pool_get_peak(&pool) == 2);

    printf("[PASS] test_pool_basic\n");
}

static void test_pool_exhaustion_and_bounds(void) {
    printf("[RUN]  test_pool_exhaustion_and_bounds\n");
    uint8_t buffer[128];
    bp_pool_t pool;

    /* 128 bytes / 32 bytes = 4 blocks */
    assert(bp_pool_init(&pool, buffer, sizeof(buffer), 32));
    assert(pool.capacity == 4);

    void *blocks[4];
    for (int i = 0; i < 4; i++) {
        blocks[i] = bp_pool_alloc(&pool);
        assert(blocks[i] != NULL);
    }
    assert(bp_pool_get_used(&pool) == 4);
    assert(bp_pool_get_free(&pool) == 0);

    /* Fifth alloc must fail */
    void *fail = bp_pool_alloc(&pool);
    assert(fail == NULL);

    /* Invalid pointer frees must fail */
    uint8_t outside;
    assert(!bp_pool_free(&pool, &outside));
    assert(!bp_pool_free(&pool, NULL));
    assert(!bp_pool_free(&pool, (uint8_t *)blocks[0] + 1)); /* Misaligned */

    /* Free blocks */
    for (int i = 0; i < 4; i++) {
        assert(bp_pool_free(&pool, blocks[i]));
    }
    assert(bp_pool_get_used(&pool) == 0);

    printf("[PASS] test_pool_exhaustion_and_bounds\n");
}

static void test_arena_basic(void) {
    printf("[RUN]  test_arena_basic\n");
    uint8_t buffer[256];
    bp_arena_t arena;

    assert(bp_arena_init(&arena, buffer, sizeof(buffer)));
    assert(bp_arena_get_used(&arena) == 0);
    assert(bp_arena_get_free(&arena) == 256);

    void *p1 = bp_arena_alloc(&arena, 20, 4);
    assert(p1 != NULL);
    assert(((size_t)p1 % 4) == 0);
    assert(bp_arena_get_used(&arena) >= 20);

    void *p2 = bp_arena_alloc(&arena, 32, 8);
    assert(p2 != NULL);
    assert(((size_t)p2 % 8) == 0);

    size_t peak = bp_arena_get_peak(&arena);
    assert(peak > 0);

    /* Reset */
    bp_arena_reset(&arena);
    assert(bp_arena_get_used(&arena) == 0);
    assert(bp_arena_get_free(&arena) == 256);
    assert(bp_arena_get_peak(&arena) == peak); /* Peak preserved */

    printf("[PASS] test_arena_basic\n");
}

static void test_arena_overflow(void) {
    printf("[RUN]  test_arena_overflow\n");
    uint8_t buffer[64];
    bp_arena_t arena;

    assert(bp_arena_init(&arena, buffer, sizeof(buffer)));
    void *p = bp_arena_alloc(&arena, 50, 4);
    assert(p != NULL);

    /* Cannot fit 20 bytes more */
    void *fail = bp_arena_alloc(&arena, 20, 4);
    assert(fail == NULL);

    printf("[PASS] test_arena_overflow\n");
}

int main(void) {
    printf("=== Memory Pool & Arena Test Suite ===\n");
    test_pool_basic();
    test_pool_exhaustion_and_bounds();
    test_arena_basic();
    test_arena_overflow();
    printf("=== ALL MEMORY POOL TESTS PASSED ===\n");
    return 0;
}
