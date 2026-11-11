#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include <pthread.h>

#include "kernel/bp_status.h"
#include "kernel/bp_resource.h"

static void dummy_irq_handler(void) {
    /* mock handler */
}

static void test_init_state(void) {
    printf("[TEST] Resource table initialization...\n");
    bp_status_t st = bp_res_init();
    assert(st == BP_STATUS_OK);

    for (uint32_t i = 0; i < BP_RES_TOTAL_PIO_SM; i++) {
        assert(bp_res_is_free(BP_RES_CLASS_PIO_SM, i));
        assert(bp_res_get_owner(BP_RES_CLASS_PIO_SM, i) == BP_RES_OWNER_NONE);
    }
    for (uint32_t i = 0; i < BP_RES_MAX_DMA_CHANNELS; i++) {
        assert(bp_res_is_free(BP_RES_CLASS_DMA_CH, i));
        assert(bp_res_get_owner(BP_RES_CLASS_DMA_CH, i) == BP_RES_OWNER_NONE);
    }
    for (uint32_t i = 0; i < BP_RES_MAX_IRQS; i++) {
        assert(bp_res_is_free(BP_RES_CLASS_IRQ, i));
    }
    for (uint32_t i = 0; i < BP_RES_MAX_PERIPHS; i++) {
        assert(bp_res_is_free(BP_RES_CLASS_PERIPH, i));
    }
    assert(bp_res_is_free(BP_RES_CLASS_BIG_BUFFER, 0));
    printf("[PASS] Initial state clean\n");
}

static void test_pio_sm_acquire_release(void) {
    printf("[TEST] PIO SM acquire and conflict rejection...\n");
    bp_res_init();

    /* Acquire PIO0 SM0 */
    bp_status_t st = bp_res_acquire(BP_RES_CLASS_PIO_SM, 0, 101, "uart_rx");
    assert(st == BP_STATUS_OK);
    assert(!bp_res_is_free(BP_RES_CLASS_PIO_SM, 0));
    assert(bp_res_get_owner(BP_RES_CLASS_PIO_SM, 0) == 101);
    assert(strcmp(bp_res_get_owner_name(BP_RES_CLASS_PIO_SM, 0), "uart_rx") == 0);

    /* Verify adjacent SMs remain free */
    assert(bp_res_is_free(BP_RES_CLASS_PIO_SM, 1));

    /* Conflict rejection: owner 102 attempts to claim contested PIO0 SM0 */
    st = bp_res_acquire(BP_RES_CLASS_PIO_SM, 0, 102, "spi_cs");
    assert(st == BP_STATUS_NO_RESOURCE);
    assert(bp_res_get_owner(BP_RES_CLASS_PIO_SM, 0) == 101);

    /* Out of bounds index */
    st = bp_res_acquire(BP_RES_CLASS_PIO_SM, BP_RES_TOTAL_PIO_SM, 101, "bad");
    assert(st == BP_STATUS_INVALID_PARAM);

    /* Release with mismatched owner fails */
    st = bp_res_release(BP_RES_CLASS_PIO_SM, 0, 102);
    assert(st == BP_STATUS_INVALID_PARAM);
    assert(!bp_res_is_free(BP_RES_CLASS_PIO_SM, 0));

    /* Release with correct owner succeeds */
    st = bp_res_release(BP_RES_CLASS_PIO_SM, 0, 101);
    assert(st == BP_STATUS_OK);
    assert(bp_res_is_free(BP_RES_CLASS_PIO_SM, 0));
    printf("[PASS] PIO SM acquire/release verified\n");
}

static void test_pio_sm_claim_unused(void) {
    printf("[TEST] PIO SM auto-claim and exhaustion handling...\n");
    bp_res_init();

    uint32_t sm_indices[BP_RES_SM_PER_PIO];
    for (uint32_t i = 0; i < BP_RES_SM_PER_PIO; i++) {
        bp_status_t st = bp_res_claim_unused_pio_sm(1, 105, "ps2_sniff", &sm_indices[i]);
        assert(st == BP_STATUS_OK);
        assert(sm_indices[i] == i);
    }

    /* 5th attempt on PIO1 must return NO_RESOURCE without panicking */
    uint32_t extra_sm = 999;
    bp_status_t st = bp_res_claim_unused_pio_sm(1, 105, "ps2_overflow", &extra_sm);
    assert(st == BP_STATUS_NO_RESOURCE);
    assert(extra_sm == 999);

    /* Clean up all leases for owner 105 */
    uint32_t released = bp_res_release_all(105);
    assert(released == BP_RES_SM_PER_PIO);

    for (uint32_t i = 0; i < BP_RES_SM_PER_PIO; i++) {
        uint32_t global_idx = (1 * BP_RES_SM_PER_PIO) + i;
        assert(bp_res_is_free(BP_RES_CLASS_PIO_SM, global_idx));
    }
    printf("[PASS] PIO SM auto-claim verified\n");
}

static void test_dma_channel_leasing(void) {
    printf("[TEST] DMA channel acquisition and claim...\n");
    bp_res_init();

    uint32_t ch0 = 99;
    uint32_t ch1 = 99;
    bp_status_t st = bp_res_claim_unused_dma_ch(110, "la_data", &ch0);
    assert(st == BP_STATUS_OK);
    assert(ch0 == 0);

    st = bp_res_claim_unused_dma_ch(110, "la_ctrl", &ch1);
    assert(st == BP_STATUS_OK);
    assert(ch1 == 1);

    /* Direct claim of DMA channel 4 */
    st = bp_res_acquire(BP_RES_CLASS_DMA_CH, 4, 110, "rgb_dma");
    assert(st == BP_STATUS_OK);
    assert(!bp_res_is_free(BP_RES_CLASS_DMA_CH, 4));

    /* Conflict rejection */
    st = bp_res_acquire(BP_RES_CLASS_DMA_CH, 4, 111, "other");
    assert(st == BP_STATUS_NO_RESOURCE);

    /* Out of bounds */
    st = bp_res_acquire(BP_RES_CLASS_DMA_CH, BP_RES_MAX_DMA_CHANNELS, 110, "bad");
    assert(st == BP_STATUS_INVALID_PARAM);

    /* Release */
    st = bp_res_release(BP_RES_CLASS_DMA_CH, ch0, 110);
    assert(st == BP_STATUS_OK);
    assert(bp_res_is_free(BP_RES_CLASS_DMA_CH, ch0));

    uint32_t released = bp_res_release_all(110);
    assert(released == 2); /* ch1 and ch4 */
    assert(bp_res_is_free(BP_RES_CLASS_DMA_CH, 1));
    assert(bp_res_is_free(BP_RES_CLASS_DMA_CH, 4));
    printf("[PASS] DMA leasing verified\n");
}

static void test_irq_leasing(void) {
    printf("[TEST] Hardware IRQ vector leasing...\n");
    bp_res_init();

    bp_status_t st = bp_res_acquire_irq(15, 120, "pio0_irq", dummy_irq_handler);
    assert(st == BP_STATUS_OK);
    assert(!bp_res_is_free(BP_RES_CLASS_IRQ, 15));
    assert(bp_res_get_owner(BP_RES_CLASS_IRQ, 15) == 120);

    /* Contested IRQ */
    st = bp_res_acquire_irq(15, 121, "contest", dummy_irq_handler);
    assert(st == BP_STATUS_NO_RESOURCE);

    /* Release */
    st = bp_res_release_irq(15, 120);
    assert(st == BP_STATUS_OK);
    assert(bp_res_is_free(BP_RES_CLASS_IRQ, 15));
    printf("[PASS] IRQ leasing verified\n");
}

static void test_big_buffer_leasing(void) {
    printf("[TEST] Big Buffer arena leasing...\n");
    bp_res_init();

    uint8_t* ptr = NULL;
    bp_status_t st = bp_res_acquire_buffer(130, "editor", 32768, &ptr);
    assert(st == BP_STATUS_OK);
    assert(ptr != NULL);
    assert(ptr == bp_res_get_big_buffer_ptr());
    assert(!bp_res_is_free(BP_RES_CLASS_BIG_BUFFER, 0));

    /* Contested buffer allocation fails gracefully */
    uint8_t* ptr2 = NULL;
    st = bp_res_acquire_buffer(131, "logic", 65536, &ptr2);
    assert(st == BP_STATUS_NO_RESOURCE);
    assert(ptr2 == NULL);

    /* Exceed buffer capacity */
    st = bp_res_acquire_buffer(132, "too_big", BP_BIG_BUFFER_SIZE + 1, &ptr2);
    assert(st == BP_STATUS_INVALID_PARAM);

    /* Bad pointer release */
    uint8_t fake_buf[16];
    st = bp_res_release_buffer(130, fake_buf);
    assert(st == BP_STATUS_INVALID_PARAM);

    /* Legitimate release */
    st = bp_res_release_buffer(130, ptr);
    assert(st == BP_STATUS_OK);
    assert(bp_res_is_free(BP_RES_CLASS_BIG_BUFFER, 0));
    printf("[PASS] Big Buffer leasing verified\n");
}

static void test_transactional_release_all(void) {
    printf("[TEST] Transactional release_all across multiple owners...\n");
    bp_res_init();

    /* Owner 101 claims PIO0 SM1, DMA 2, IRQ 10, Big Buffer */
    assert(bp_res_acquire(BP_RES_CLASS_PIO_SM, 1, 101, "m1_sm") == BP_STATUS_OK);
    assert(bp_res_acquire(BP_RES_CLASS_DMA_CH, 2, 101, "m1_dma") == BP_STATUS_OK);
    assert(bp_res_acquire_irq(10, 101, "m1_irq", dummy_irq_handler) == BP_STATUS_OK);
    uint8_t* buf = NULL;
    assert(bp_res_acquire_buffer(101, "m1_buf", 65536, &buf) == BP_STATUS_OK);

    /* Owner 102 claims PIO1 SM2, DMA 4 */
    assert(bp_res_acquire(BP_RES_CLASS_PIO_SM, 6, 102, "m2_sm") == BP_STATUS_OK);
    assert(bp_res_acquire(BP_RES_CLASS_DMA_CH, 4, 102, "m2_dma") == BP_STATUS_OK);

    assert(bp_res_count_owned(101) == 4);
    assert(bp_res_count_owned(102) == 2);

    /* Release all for owner 101 */
    uint32_t released = bp_res_release_all(101);
    assert(released == 4);
    assert(bp_res_count_owned(101) == 0);

    /* Owner 102 must be completely unaffected */
    assert(bp_res_count_owned(102) == 2);
    assert(!bp_res_is_free(BP_RES_CLASS_PIO_SM, 6));
    assert(!bp_res_is_free(BP_RES_CLASS_DMA_CH, 4));

    /* Release owner 102 */
    released = bp_res_release_all(102);
    assert(released == 2);
    assert(bp_res_count_owned(102) == 0);
    printf("[PASS] Transactional rollback verified\n");
}

static void test_mode_switch_soak(void) {
    printf("[TEST] 1000 mode-switch soak test with zero leaks...\n");
    bp_res_init();

    for (uint32_t cycle = 0; cycle < 1000; cycle++) {
        uint32_t mode_id = (cycle % 18) + 1;
        uint32_t owner = BP_RES_OWNER_MODE_BASE + mode_id;

        /* Simulate mode setup */
        uint32_t sm = 0;
        bp_status_t st_sm = bp_res_claim_unused_pio_sm(0, owner, "soak_sm", &sm);
        assert(st_sm == BP_STATUS_OK);

        uint32_t ch = 0;
        bp_status_t st_dma = bp_res_claim_unused_dma_ch(owner, "soak_dma", &ch);
        assert(st_dma == BP_STATUS_OK);

        if ((cycle % 3) == 0) {
            uint8_t* b = NULL;
            bp_status_t st_buf = bp_res_acquire_buffer(owner, "soak_buf", 16384, &b);
            assert(st_buf == BP_STATUS_OK);
        }

        /* 10% simulated setup error with rollback */
        if ((cycle % 10) == 0) {
            bp_res_release_all(owner);
            assert(bp_res_count_owned(owner) == 0);
            continue;
        }

        /* Normal mode teardown */
        uint32_t count = bp_res_release_all(owner);
        assert(count >= 2);
        assert(bp_res_count_owned(owner) == 0);
    }

    /* Verify 100% free after 1000 cycles */
    for (uint32_t i = 0; i < BP_RES_TOTAL_PIO_SM; i++) {
        assert(bp_res_is_free(BP_RES_CLASS_PIO_SM, i));
    }
    for (uint32_t i = 0; i < BP_RES_MAX_DMA_CHANNELS; i++) {
        assert(bp_res_is_free(BP_RES_CLASS_DMA_CH, i));
    }
    for (uint32_t i = 0; i < BP_RES_MAX_IRQS; i++) {
        assert(bp_res_is_free(BP_RES_CLASS_IRQ, i));
    }
    assert(bp_res_is_free(BP_RES_CLASS_BIG_BUFFER, 0));
    printf("[PASS] 1000 mode-switch soak test passed with ZERO leaked resources\n");
}

static uint8_t concurrent_channel[BP_RES_MAX_DMA_CHANNELS];
static void* lease_worker(void* arg) {
    uint32_t owner = (uint32_t)(uintptr_t)arg;
    for (unsigned i = 0; i < 10000; i++) {
        uint32_t channel;
        bp_status_t st = bp_res_claim_unused_dma_ch(owner, "stress", &channel);
        assert(st == BP_STATUS_OK);
        assert(!__atomic_exchange_n(&concurrent_channel[channel], 1, __ATOMIC_ACQ_REL));
        __atomic_store_n(&concurrent_channel[channel], 0, __ATOMIC_RELEASE);
        assert(bp_res_release(BP_RES_CLASS_DMA_CH, channel, owner) == BP_STATUS_OK);
    }
    return NULL;
}
static void test_concurrent_leases(void) {
    bp_res_init(); /* Firmware also initializes before launching the second core. */
    pthread_t workers[2];
    for (unsigned i = 0; i < 2; i++) assert(!pthread_create(&workers[i], NULL, lease_worker, (void*)(uintptr_t)(100 + i)));
    for (unsigned i = 0; i < 2; i++) assert(!pthread_join(workers[i], NULL));
    assert(bp_res_count_owned(100) == 0 && bp_res_count_owned(101) == 0);
    puts("[PASS] Two concurrent owners: 20,000 DMA lease/release operations");
}

int main(void) {
    printf("=== Bus Pirate Hardware Resource Lease Manager Test Suite ===\n");
    test_concurrent_leases();
    test_init_state();
    test_pio_sm_acquire_release();
    test_pio_sm_claim_unused();
    test_dma_channel_leasing();
    test_irq_leasing();
    test_big_buffer_leasing();
    test_transactional_release_all();
    test_mode_switch_soak();

    printf("\nResource table status printout test:\n");
    bp_res_print_table();

    printf("\n=== ALL RESOURCE LEASE TESTS PASSED ===\n");
    return 0;
}
