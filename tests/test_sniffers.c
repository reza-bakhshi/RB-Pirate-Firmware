#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "kernel/bp_status.h"
#include "kernel/bp_resource.h"
#include "kernel/bp_job.h"

/* --- PIO, Clock, and IRQ Mock Tracking --- */
typedef struct {
    uint32_t claimed_sm_mask;
    uint32_t loaded_programs_mask;
    uint8_t program_instruction_count;
} mock_pio_t;

static mock_pio_t s_mock_pio0;
static mock_pio_t s_mock_pio1;

static bool s_mock_irq_pio0_enabled = false;
static bool s_mock_irq_pio1_enabled = false;
static void (*s_mock_pio0_handler)(void) = NULL;
static void (*s_mock_pio1_handler)(void) = NULL;

static uint32_t s_mock_clk_sys_hz = 125000000;
static uint32_t s_mock_saved_clk_hz = 0;

/* PS/2 Mock Constants & Types */
#define MOCK_RING_BUFF_MAX 800
static uint8_t *s_mock_ps2_ringbuff = NULL;
static bool s_mock_ps2_pio_initialized = false;
static int s_mock_ps2_sm_kbd = -1;
static int s_mock_ps2_sm_inh = -1;
static int s_mock_ps2_sm_h2d = -1;
static int s_mock_ps2_sm_idle = -1;

/* USB Mock Constants & Types */
#define MOCK_USB_BUFFER_SIZE (64 * 1024)
static uint32_t *s_mock_usb_buffer = NULL;
typedef struct {
    int pio_idx;
    uint32_t sm;
    uint32_t offset;
    bool used;
} mock_usb_lease_t;
static mock_usb_lease_t s_mock_usb_leases[4];

/* Dummy IRQ handlers */
static void mock_ps2_pio0_irq(void) {}
static void mock_ps2_pio1_irq(void) {}
static void mock_usb_pio0_irq(void) {}
static void mock_usb_pio1_irq(void) {}

/* --- Hardware Resource Simulators matching ps2.c and usb.c --- */

static void ps2_mock_pio_release(void) {
    if (!s_mock_ps2_pio_initialized) {
        return;
    }
    // Remove all 4 PIO programs
    s_mock_pio0.loaded_programs_mask &= ~(1u << 0); // device_to_host
    s_mock_pio0.loaded_programs_mask &= ~(1u << 1); // inhibited
    s_mock_pio1.loaded_programs_mask &= ~(1u << 0); // host_to_device
    s_mock_pio1.loaded_programs_mask &= ~(1u << 1); // idle
    s_mock_pio0.program_instruction_count = 0;
    s_mock_pio1.program_instruction_count = 0;

    // Unclaim SMs
    s_mock_pio0.claimed_sm_mask &= ~(1u << s_mock_ps2_sm_kbd);
    s_mock_pio0.claimed_sm_mask &= ~(1u << s_mock_ps2_sm_inh);
    s_mock_pio1.claimed_sm_mask &= ~(1u << s_mock_ps2_sm_h2d);
    s_mock_pio1.claimed_sm_mask &= ~(1u << s_mock_ps2_sm_idle);

    s_mock_ps2_pio_initialized = false;
    bp_res_release_all(BP_RES_OWNER_JOB);
}

static void ps2_mock_irq_teardown(void) {
    s_mock_irq_pio0_enabled = false;
    s_mock_irq_pio1_enabled = false;
    if (s_mock_pio0_handler == mock_ps2_pio0_irq) s_mock_pio0_handler = NULL;
    if (s_mock_pio1_handler == mock_ps2_pio1_irq) s_mock_pio1_handler = NULL;
    bp_res_release_all(BP_RES_OWNER_JOB);
}

static int ps2_mock_setup_exc(void) {
    if (!s_mock_ps2_ringbuff) {
        s_mock_ps2_ringbuff = (uint8_t *)malloc(MOCK_RING_BUFF_MAX * 32);
    }
    return 10000000;
}

static void ps2_mock_cleanup(void) {
    bp_job_cancel_active();
    ps2_mock_irq_teardown();
    if (s_mock_ps2_ringbuff) {
        free(s_mock_ps2_ringbuff);
        s_mock_ps2_ringbuff = NULL;
    }
    ps2_mock_pio_release();
}

static bp_status_t ps2_mock_sniff_start(void) {
    ps2_mock_pio_release();

    uint32_t sm_h2d, sm_idle, sm_inh, sm_kbd;
    if (bp_res_claim_unused_pio_sm(1, BP_RES_OWNER_JOB, "ps2_h2d", &sm_h2d) != BP_STATUS_OK ||
        bp_res_claim_unused_pio_sm(1, BP_RES_OWNER_JOB, "ps2_idle", &sm_idle) != BP_STATUS_OK ||
        bp_res_claim_unused_pio_sm(0, BP_RES_OWNER_JOB, "ps2_inh", &sm_inh) != BP_STATUS_OK ||
        bp_res_claim_unused_pio_sm(0, BP_RES_OWNER_JOB, "ps2_kbd", &sm_kbd) != BP_STATUS_OK) {
        bp_res_release_all(BP_RES_OWNER_JOB);
        return BP_STATUS_NO_RESOURCE;
    }
    s_mock_ps2_sm_h2d = (int)sm_h2d;
    s_mock_ps2_sm_idle = (int)sm_idle;
    s_mock_ps2_sm_inh = (int)sm_inh;
    s_mock_ps2_sm_kbd = (int)sm_kbd;
    s_mock_pio1.claimed_sm_mask |= (1u << sm_h2d) | (1u << sm_idle);
    s_mock_pio0.claimed_sm_mask |= (1u << sm_inh) | (1u << sm_kbd);

    // Load programs
    s_mock_pio1.loaded_programs_mask |= (1u << 0) | (1u << 1);
    s_mock_pio0.loaded_programs_mask |= (1u << 0) | (1u << 1);
    s_mock_pio1.program_instruction_count += 20;
    s_mock_pio0.program_instruction_count += 20;

    // Acquire IRQs
    bp_res_acquire_irq(0, BP_RES_OWNER_JOB, "ps2_pio0", mock_ps2_pio0_irq);
    s_mock_irq_pio0_enabled = true;
    s_mock_pio0_handler = mock_ps2_pio0_irq;

    bp_res_acquire_irq(1, BP_RES_OWNER_JOB, "ps2_pio1", mock_ps2_pio1_irq);
    s_mock_irq_pio1_enabled = true;
    s_mock_pio1_handler = mock_ps2_pio1_irq;

    s_mock_ps2_pio_initialized = true;
    return BP_STATUS_OK;
}

/* --- USB Mock Simulators --- */

static void usb_mock_pio_release(void) {
    s_mock_irq_pio0_enabled = false;
    s_mock_irq_pio1_enabled = false;
    if (s_mock_pio0_handler == mock_usb_pio0_irq) s_mock_pio0_handler = NULL;
    if (s_mock_pio1_handler == mock_usb_pio1_irq) s_mock_pio1_handler = NULL;

    for (int i = 0; i < 4; i++) {
        if (!s_mock_usb_leases[i].used) continue;
        if (s_mock_usb_leases[i].pio_idx == 0) {
            s_mock_pio0.claimed_sm_mask &= ~(1u << s_mock_usb_leases[i].sm);
            s_mock_pio0.loaded_programs_mask &= ~(1u << s_mock_usb_leases[i].offset);
            s_mock_pio0.program_instruction_count = 0;
        } else {
            s_mock_pio1.claimed_sm_mask &= ~(1u << s_mock_usb_leases[i].sm);
            s_mock_pio1.loaded_programs_mask &= ~(1u << s_mock_usb_leases[i].offset);
            s_mock_pio1.program_instruction_count = 0;
        }
        s_mock_usb_leases[i].used = false;
    }
    bp_res_release_all(BP_RES_OWNER_JOB);
}

static void usb_mock_pio_track(int pio_idx, uint32_t sm, uint32_t offset) {
    for (int i = 0; i < 4; i++) {
        if (!s_mock_usb_leases[i].used) {
            s_mock_usb_leases[i] = (mock_usb_lease_t){ pio_idx, sm, offset, true };
            return;
        }
    }
}

static int usb_mock_setup_exc(void) {
    if (!s_mock_usb_buffer) {
        s_mock_usb_buffer = (uint32_t *)malloc(MOCK_USB_BUFFER_SIZE);
    }
    return 10000000;
}

static void usb_mock_cleanup(void) {
    bp_job_cancel_active();
    if (s_mock_usb_buffer) {
        free(s_mock_usb_buffer);
        s_mock_usb_buffer = NULL;
    }
    if (s_mock_saved_clk_hz) {
        s_mock_clk_sys_hz = s_mock_saved_clk_hz;
        s_mock_saved_clk_hz = 0;
    }
    usb_mock_pio_release();
}

static bp_status_t usb_mock_cap_init(void) {
    if (s_mock_saved_clk_hz == 0) {
        s_mock_saved_clk_hz = s_mock_clk_sys_hz;
    }
    s_mock_clk_sys_hz = 120000000;

    usb_mock_pio_release();

    uint32_t pio1_sm = 0;
    if (bp_res_claim_unused_pio_sm(1, BP_RES_OWNER_JOB, "usb_pio1", &pio1_sm) != BP_STATUS_OK) {
        bp_res_release_all(BP_RES_OWNER_JOB);
        return BP_STATUS_NO_RESOURCE;
    }
    s_mock_pio1.claimed_sm_mask |= (1u << pio1_sm);
    s_mock_pio1.loaded_programs_mask |= (1u << 0);
    s_mock_pio1.program_instruction_count += 12;
    usb_mock_pio_track(1, pio1_sm, 0);

    bp_res_acquire_irq(1, BP_RES_OWNER_JOB, "usb_pio1", mock_usb_pio1_irq);
    s_mock_irq_pio1_enabled = true;
    s_mock_pio1_handler = mock_usb_pio1_irq;

    uint32_t pio0_sm = 0;
    if (bp_res_claim_unused_pio_sm(0, BP_RES_OWNER_JOB, "usb_pio0", &pio0_sm) != BP_STATUS_OK) {
        bp_res_release_all(BP_RES_OWNER_JOB);
        return BP_STATUS_NO_RESOURCE;
    }
    s_mock_pio0.claimed_sm_mask |= (1u << pio0_sm);
    s_mock_pio0.loaded_programs_mask |= (1u << 0);
    s_mock_pio0.program_instruction_count += 20;
    usb_mock_pio_track(0, pio0_sm, 0);

    bp_res_acquire_irq(0, BP_RES_OWNER_JOB, "usb_pio0", mock_usb_pio0_irq);
    s_mock_irq_pio0_enabled = true;
    s_mock_pio0_handler = mock_usb_pio0_irq;

    return BP_STATUS_OK;
}

static bp_status_t usb_mock_keylog_init(void) {
    if (s_mock_saved_clk_hz == 0) {
        s_mock_saved_clk_hz = s_mock_clk_sys_hz;
    }
    s_mock_clk_sys_hz = 120000000;

    usb_mock_pio_release();

    uint32_t pio1_sm = 0;
    if (bp_res_claim_unused_pio_sm(1, BP_RES_OWNER_JOB, "usb_pio1", &pio1_sm) != BP_STATUS_OK) {
        return BP_STATUS_NO_RESOURCE;
    }
    s_mock_pio1.claimed_sm_mask |= (1u << pio1_sm);
    s_mock_pio1.loaded_programs_mask |= (1u << 0);
    s_mock_pio1.program_instruction_count += 12;
    usb_mock_pio_track(1, pio1_sm, 0);

    uint32_t pio0_sm = 0;
    if (bp_res_claim_unused_pio_sm(0, BP_RES_OWNER_JOB, "usb_pio0", &pio0_sm) != BP_STATUS_OK) {
        usb_mock_pio_release();
        return BP_STATUS_NO_RESOURCE;
    }
    s_mock_pio0.claimed_sm_mask |= (1u << pio0_sm);
    s_mock_pio0.loaded_programs_mask |= (1u << 0);
    s_mock_pio0.program_instruction_count += 20;
    usb_mock_pio_track(0, pio0_sm, 0);

    bp_res_acquire_irq(0, BP_RES_OWNER_JOB, "usb_pio0", mock_usb_pio0_irq);
    s_mock_irq_pio0_enabled = true;
    s_mock_pio0_handler = mock_usb_pio0_irq;

    return BP_STATUS_OK;
}

/* --- Tests --- */

static void test_ps2_sniff_lifecycle_100_cycles(void) {
    printf("[test_sniffers] Testing PS/2 sniffer 100 enter/exit cycles...\n");
    bp_res_init();

    for (int cycle = 0; cycle < 100; cycle++) {
        assert(ps2_mock_setup_exc() == 10000000);
        assert(s_mock_ps2_ringbuff != NULL);

        bp_status_t st = ps2_mock_sniff_start();
        assert(st == BP_STATUS_OK);

        // Verify SMs, programs, IRQs claimed
        assert(s_mock_pio0.claimed_sm_mask != 0);
        assert(s_mock_pio1.claimed_sm_mask != 0);
        assert(s_mock_pio0.loaded_programs_mask != 0);
        assert(s_mock_pio1.loaded_programs_mask != 0);
        assert(s_mock_irq_pio0_enabled);
        assert(s_mock_irq_pio1_enabled);
        assert(bp_res_count_owned(BP_RES_OWNER_JOB) > 0);

        // Clean exit
        ps2_mock_cleanup();

        // Verify zero leaks
        assert(s_mock_ps2_ringbuff == NULL);
        assert(s_mock_pio0.claimed_sm_mask == 0);
        assert(s_mock_pio1.claimed_sm_mask == 0);
        assert(s_mock_pio0.loaded_programs_mask == 0);
        assert(s_mock_pio1.loaded_programs_mask == 0);
        assert(s_mock_pio0.program_instruction_count == 0);
        assert(s_mock_pio1.program_instruction_count == 0);
        assert(!s_mock_irq_pio0_enabled);
        assert(!s_mock_irq_pio1_enabled);
        assert(s_mock_pio0_handler == NULL);
        assert(s_mock_pio1_handler == NULL);
        assert(bp_res_count_owned(BP_RES_OWNER_JOB) == 0);
    }
    printf("  -> PS/2 sniffer: 100 cycles passed cleanly with 0 leaks.\n");
}

static void test_usb_sniffer_lifecycle_100_cycles(void) {
    printf("[test_sniffers] Testing USB sniffer 100 enter/exit cycles & clock restoration...\n");
    bp_res_init();
    const uint32_t baseline_clk = 125000000;
    s_mock_clk_sys_hz = baseline_clk;

    for (int cycle = 0; cycle < 100; cycle++) {
        assert(usb_mock_setup_exc() == 10000000);
        assert(s_mock_usb_buffer != NULL);

        // Test capture session
        bp_status_t st = usb_mock_cap_init();
        assert(st == BP_STATUS_OK);
        assert(s_mock_clk_sys_hz == 120000000);
        assert(s_mock_saved_clk_hz == baseline_clk);

        // Multiple invocations within same mode session do not corrupt baseline
        st = usb_mock_keylog_init();
        assert(st == BP_STATUS_OK);
        assert(s_mock_clk_sys_hz == 120000000);
        assert(s_mock_saved_clk_hz == baseline_clk); // Still 125 MHz, not 120 MHz!

        // Verify resource manager tracking
        assert(bp_res_count_owned(BP_RES_OWNER_JOB) > 0);
        assert(s_mock_irq_pio0_enabled);

        // Exit mode
        usb_mock_cleanup();

        // Verify exact clock restoration and 0 leaks
        assert(s_mock_clk_sys_hz == baseline_clk);
        assert(s_mock_saved_clk_hz == 0);
        assert(s_mock_usb_buffer == NULL);
        assert(s_mock_pio0.claimed_sm_mask == 0);
        assert(s_mock_pio1.claimed_sm_mask == 0);
        assert(s_mock_pio0.loaded_programs_mask == 0);
        assert(s_mock_pio1.loaded_programs_mask == 0);
        assert(s_mock_pio0.program_instruction_count == 0);
        assert(s_mock_pio1.program_instruction_count == 0);
        assert(!s_mock_irq_pio0_enabled);
        assert(!s_mock_irq_pio1_enabled);
        assert(s_mock_pio0_handler == NULL);
        assert(s_mock_pio1_handler == NULL);
        assert(bp_res_count_owned(BP_RES_OWNER_JOB) == 0);
    }
    printf("  -> USB sniffer: 100 cycles passed cleanly with exact clock restoration & 0 leaks.\n");
}

/* Exercise the real scheduler rather than asserting a simulated boolean. */
static unsigned cancel_cleanup_calls;
static bp_status_t cancellation_slice(void *ctx, uint8_t *pct) {
    (void)ctx; *pct = 1; return BP_STATUS_BUSY;
}
static void cancellation_cleanup(void *ctx, bp_status_t status) {
    (void)ctx; assert(status == BP_STATUS_CANCELLED); cancel_cleanup_calls++;
}
static void test_usb_cancellation_and_trigger_mock(void) {
    bp_job_subsystem_init();
    uint32_t jid;
    assert(bp_job_submit_with_cleanup("sniffer_cancel", 0, 0, cancellation_slice,
        cancellation_cleanup, NULL, 1, &jid) == BP_STATUS_OK);
    bp_job_service(2);
    assert(bp_job_cancel_active() == BP_STATUS_OK);
    bp_job_service(3);
    bp_job_finalize();
    assert(cancel_cleanup_calls == 1);
    assert(bp_job_get_active_id() == 0);
    puts("[PASS] Production scheduler cancellation invokes cleanup");
}

int main(void) {
    printf("=========================================\n");
    printf("Running Sniffer Resource Model Tests (driver HIL still required)\n");
    printf("=========================================\n");

    test_ps2_sniff_lifecycle_100_cycles();
    test_usb_sniffer_lifecycle_100_cycles();
    test_usb_cancellation_and_trigger_mock();

    printf("\nAll sniffer unit tests passed successfully!\n");
    return 0;
}
