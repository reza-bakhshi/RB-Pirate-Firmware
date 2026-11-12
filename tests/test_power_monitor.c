#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <sched.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <unistd.h>

#include "hardware/sync.h"
#include "hardware/adc.h"
#include "kernel/bp_status.h"
#include "kernel/bp_config.h"
#include "kernel/bp_job.h"
#include "system_config.h"
#include "pirate/amux.h"
#include "pirate/psu.h"

// Hardware stubs and variables for host execution
static uint32_t s_mock_time_us = 1000000;
uint32_t get_host_time_us(void) {
    return s_mock_time_us;
}
void mock_time_advance_us(uint32_t delta_us) {
    s_mock_time_us += delta_us;
}

uint16_t s_mock_adc_val = 1000;
uint16_t s_mock_current_amux_channel = 0;
uint16_t s_mock_channel_adc[16] = {0};
bool s_mock_use_channel_adc = false;
bool s_mock_gpio_fuse_tripped = false;
static spin_lock_t s_mock_spin_lock = 0;

static int8_t s_test_core_id = 0;
unsigned int get_core_num(void) {
    return (unsigned int)s_test_core_id;
}

uint16_t hw_adc_raw[16];
uint32_t hw_adc_voltage[16];
uint32_t hw_adc_avgsum_voltage[16];

struct _system_config system_config;

void bp_watchdog_heartbeat(uint8_t c) { (void)c; }

// Spinlock mock
uint32_t spin_lock_blocking(spin_lock_t *lock) {
    (void)lock;
    return 0;
}

void spin_unlock(spin_lock_t *lock, uint32_t save) {
    (void)lock;
    (void)save;
}

void spin_lock_unsafe_blocking(spin_lock_t *lock) {
    (void)lock;
}

void spin_unlock_unsafe(spin_lock_t *lock) {
    (void)lock;
}

uint spin_lock_claim_unused(bool required) {
    (void)required;
    return 1;
}

spin_lock_t* spin_lock_init(uint lock_num) {
    (void)lock_num;
    return &s_mock_spin_lock;
}

static bp_status_t mock_job_slice(void* ctx, uint8_t* out_percent) {
    (void)ctx;
    *out_percent = 50;
    return BP_STATUS_BUSY;
}

// -------------------------------------------------------------------------
// Test 1: Reentrant ADC lock on same core
// -------------------------------------------------------------------------
static void test_adc_reentrancy(void) {
    printf("[TEST] Reentrant ADC arbitration on the same core...\n");
    s_test_core_id = 0;

    assert(adc_get_owner() == -1);
    assert(adc_get_nest_count() == 0);

    // Initial acquire
    adc_busy_wait(true);
    assert(adc_get_owner() == 0);
    assert(adc_get_nest_count() == 1);
    assert(!adc_try_acquire()); /* Polling must not change the command's ADC mux. */

    // Reentrant acquire 4 more times (nesting level 5)
    for (int i = 0; i < 4; i++) {
        adc_busy_wait(true);
        assert(adc_get_owner() == 0);
        assert(adc_get_nest_count() == (uint32_t)(2 + i));
    }
    assert(adc_get_nest_count() == 5);

    // Release 4 times
    for (int i = 0; i < 4; i++) {
        adc_busy_wait(false);
        assert(adc_get_owner() == 0);
        assert(adc_get_nest_count() == (uint32_t)(4 - i));
    }
    assert(adc_get_nest_count() == 1);
    assert(adc_get_owner() == 0);

    // Final release
    adc_busy_wait(false);
    assert(adc_get_owner() == -1);
    assert(adc_get_nest_count() == 0);

    printf("[PASS] Reentrant ADC lock verified (nesting up to 5 levels)\n");
}

// -------------------------------------------------------------------------
// Test 2: Multicore ADC arbitration
// -------------------------------------------------------------------------
static void test_adc_multicore_arbitration(void) {
    printf("[TEST] Cross-core ADC arbitration & handoff...\n");

    // Core 0 acquires
    s_test_core_id = 0;
    adc_busy_wait(true);
    assert(adc_get_owner() == 0);

    // Switch to Core 1: verify owner is Core 0
    s_test_core_id = 1;
    assert(adc_get_owner() == 0);
    assert(!adc_busy_wait(true));
    assert(adc_get_owner() == 0 && adc_get_nest_count() == 1);
    assert(!adc_busy_wait(false)); /* A non-owner cannot release another core. */
    uint16_t channel_before = s_mock_current_amux_channel;
    assert(amux_read(3) == 0);
    assert(s_mock_current_amux_channel == channel_before);
    assert(!amux_dma_sweep_start());
    uint16_t untouched[16];
    memset(untouched, 0xA5, sizeof(untouched));
    assert(!amux_dma_get_coherent(untouched, 16));
    for (unsigned i = 0; i < 16; ++i) assert(untouched[i] == 0xA5A5);
    assert(adc_get_owner() == 0 && adc_get_nest_count() == 1);

    // Core 0 releases
    s_test_core_id = 0;
    adc_busy_wait(false);
    assert(adc_get_owner() == -1);

    // Core 1 acquires
    s_test_core_id = 1;
    adc_busy_wait(true);
    assert(adc_get_owner() == 1);
    assert(adc_get_nest_count() == 1);

    // Core 1 releases
    adc_busy_wait(false);
    assert(adc_get_owner() == -1);
    assert(adc_get_nest_count() == 0);

    s_test_core_id = 0;
    printf("[PASS] Cross-core arbitration & handoff verified\n");
}

// -------------------------------------------------------------------------
// Test 3: Multithreaded Seqlock Monitor Snapshot
// -------------------------------------------------------------------------
#define NUM_SEQLOCK_CYCLES 20000
static volatile bool s_writer_running = true;

static void* snapshot_writer_thread(void* arg) {
    (void)arg;
    uint16_t pin_v[HW_PINS];
    for (uint32_t i = 1; i <= NUM_SEQLOCK_CYCLES; i++) {
        for (int p = 0; p < HW_PINS; p++) {
            pin_v[p] = (uint16_t)(i + p * 100);
        }
        uint32_t vout = i * 10;
        uint32_t vreg = vout + 200; // Invariant: vreg == vout + 200
        uint32_t cur = i % 500;
        bp_ui_snapshot_publish_power(i, 1, vreg, vout, cur, pin_v, true, true, false, false);
    }
    s_writer_running = false;
    return NULL;
}

static void test_seqlock_snapshot_concurrency(void) {
    printf("[TEST] Multithreaded seqlock snapshot read/write (20,000 cycles)...\n");
    assert(bp_config_subsystem_init() == BP_STATUS_OK);

    s_writer_running = true;
    pthread_t writer;
    assert(pthread_create(&writer, NULL, snapshot_writer_thread, NULL) == 0);

    bp_ui_snapshot_t snap;
    uint32_t reads_performed = 0;
    uint32_t valid_reads = 0;

    while (s_writer_running || (valid_reads < 500 && reads_performed < 200000)) {
        if ((reads_performed % 16) == 0) {
            sched_yield();
        }
        if (bp_ui_snapshot_read(&snap)) {
            // Verify seqlock was not read while odd (in-progress write)
            assert((snap.seq & 1) == 0);
            if (snap.timestamp_ms > 0) {
                // Verify mathematical invariant written atomically
                assert(snap.vreg_mv == snap.vout_mv + 200);
                assert(snap.pin_voltages[0] == (uint16_t)(snap.timestamp_ms));
                valid_reads++;
            }
        }
        reads_performed++;
    }

    pthread_join(writer, NULL);
    assert(valid_reads > 100);
    printf("[PASS] Seqlock snapshot verified: %u reads, %u consistent snapshots, ZERO torn reads\n",
           reads_performed, valid_reads);
}

// -------------------------------------------------------------------------
// Test 4: PSU Overcurrent Trip and Job Abort
// -------------------------------------------------------------------------
static void test_psu_overcurrent_job_abort(void) {
    printf("[TEST] PSU overcurrent detection and protocol job abort...\n");
    assert(bp_job_subsystem_init() == BP_STATUS_OK);

    // Submit an active protocol job
    uint32_t job_id = 0;
    assert(bp_job_submit("proto_stream", BP_JOB_PROTO_TEST, 5000, mock_job_slice, NULL, 0, &job_id) == BP_STATUS_OK);
    assert(job_id > 0);
    assert(bp_job_get_active_id() == job_id);

    // Simulate active PSU
    psu_status.enabled = true;
    psu_status.error_overcurrent = false;
    psu_status.error_undervoltage = false;
    psu_status.error_pending = false;
    psu_status.undervoltage_limit_adc = 500;
    s_mock_use_channel_adc = true;
    s_mock_channel_adc[HW_ADC_MUX_CURRENT_DETECT] = 2000; // Normal fuse ADC (> 300)
    s_mock_channel_adc[HW_ADC_MUX_VREF_VOUT] = 1000;    // Normal Vout (> 500)
    s_mock_gpio_fuse_tripped = false;                    // GPIO high = fuse OK

    // Normal poll with fuse OK: no error
    assert(!psu_poll_fuse_vout_error());
    assert(psu_status.enabled == true);
    assert(bp_job_get_active_id() == job_id);

    // Simulate overcurrent fuse blowing (ADC read < 300, GPIO low = fuse tripped)
    s_mock_channel_adc[HW_ADC_MUX_CURRENT_DETECT] = 80;
    s_mock_gpio_fuse_tripped = true;
    bool error_detected = psu_poll_fuse_vout_error();
    assert(error_detected == true);

    // Verify PSU was immediately disabled
    assert(psu_status.enabled == false);
    assert(psu_status.error_overcurrent == true);
    assert(psu_status.error_pending == true);
    assert(psu_status.last_fuse_sample_valid);
    assert(psu_status.last_fuse_adc == 80);

    // Verify cancellation was immediately requested on the active job
    assert(bp_job_check_cancelled() == true);

    // Service the cancellation on Core 1
    assert(bp_job_service(100) == 1);

    // Verify protocol job was automatically cancelled to protect hardware
    bp_job_state_t jstate;
    bp_status_t jstatus;
    uint8_t jpct;
    assert(bp_job_get_state(job_id, &jstate, &jstatus, &jpct) == BP_STATUS_OK);
    assert(jstate == BP_JOB_STATE_CANCELLED);
    assert(bp_job_get_active_id() == 0);

    printf("[PASS] PSU overcurrent trip verified: regulator disabled and active job aborted cleanly\n");
}

// -------------------------------------------------------------------------
// Test 5: PSU Undervoltage Debounce and Persistent Short-Circuit Trip
// -------------------------------------------------------------------------
static void test_psu_undervoltage_debounce_and_trip(void) {
    printf("[TEST] PSU undervoltage debounce filtering and persistent trip...\n");

    // Submit an active protocol job
    uint32_t job_id = 0;
    assert(bp_job_submit("proto_stream2", BP_JOB_PROTO_TEST, 5000, mock_job_slice, NULL, 0, &job_id) == BP_STATUS_OK);
    assert(job_id > 0);
    assert(bp_job_get_active_id() == job_id);

    // Setup active PSU with undervoltage threshold at 500 ADC counts
    psu_status.enabled = true;
    psu_status.error_overcurrent = false;
    psu_status.error_undervoltage = false;
    psu_status.error_pending = false;
    psu_status.undervoltage_limit_override = false;
    psu_status.undervoltage_limit_adc = 500;
    s_mock_use_channel_adc = true;
    s_mock_channel_adc[HW_ADC_MUX_CURRENT_DETECT] = 2000; // Normal fuse (> 300)
    s_mock_channel_adc[HW_ADC_MUX_VREF_VOUT] = 1000;    // Normal Vout (> 500)
    s_mock_gpio_fuse_tripped = false;

    // Initial check: all OK
    assert(!psu_poll_fuse_vout_error());
    assert(psu_status.enabled == true);

    // Case 1: Transient dip (2 cycles below limit) should NOT trip PSU due to debouncing
    s_mock_channel_adc[HW_ADC_MUX_VREF_VOUT] = 400; // Below 500 limit
    assert(!psu_poll_fuse_vout_error()); // fail_count = 1 -> no trip
    assert(psu_status.enabled == true);
    assert(!psu_poll_fuse_vout_error()); // fail_count = 2 -> no trip
    assert(psu_status.enabled == true);

    // Reading recovers to normal: debounce counter must reset
    s_mock_channel_adc[HW_ADC_MUX_VREF_VOUT] = 1000;
    assert(!psu_poll_fuse_vout_error()); // recovered -> fail_count = 0
    assert(psu_status.enabled == true);

    // Case 2: Persistent undervoltage / short-circuit (4 consecutive cycles below threshold)
    s_mock_channel_adc[HW_ADC_MUX_VREF_VOUT] = 400;
    assert(!psu_poll_fuse_vout_error()); // cycle 1: fail_count = 1
    assert(psu_status.enabled == true);
    assert(!psu_poll_fuse_vout_error()); // cycle 2: fail_count = 2
    assert(psu_status.enabled == true);
    assert(!psu_poll_fuse_vout_error()); // cycle 3: fail_count = 3
    assert(psu_status.enabled == true);

    // Cycle 4: debounce limit reached -> MUST trip!
    bool error_detected = psu_poll_fuse_vout_error();
    assert(error_detected == true);
    assert(psu_status.enabled == false);
    assert(psu_status.error_undervoltage == true);
    assert(psu_status.error_overcurrent == false);
    assert(psu_status.error_pending == true);

    // Verify protocol job was automatically cancelled to protect hardware
    assert(bp_job_check_cancelled() == true);
    assert(bp_job_service(100) == 1);
    bp_job_state_t jstate;
    bp_status_t jstatus;
    uint8_t jpct;
    assert(bp_job_get_state(job_id, &jstate, &jstatus, &jpct) == BP_STATUS_OK);
    assert(jstate == BP_JOB_STATE_CANCELLED);
    assert(bp_job_get_active_id() == 0);

    // Clear error flag and verify debounce resets
    psu_clear_error_flag();
    assert(psu_status.error_pending == false);

    printf("[PASS] PSU undervoltage debounce filtering & short-circuit trip verified\n");
}

static void test_psu_undervoltage_calculation(void) {
    printf("[TEST] PSU undervoltage limit calculations across voltage levels...\n");

    // Test 1.2V with 10% limit: must clamp to 200mV minimum noise floor (1.00V limit)
    psu_status.voltage_actual_int = 12000;
    psu_set_undervoltage_limit(&psu_status, 10);
    assert(psu_status.undervoltage_limit_override == false);
    assert(psu_status.undervoltage_percent == 10);
    assert(psu_status.undervoltage_limit_int == 10000); // 1.00V
    assert(psu_status.undervoltage_limit_adc == (uint16_t)((10000UL * 4096UL) / 66000UL));

    // Test 1.65V with 10% limit: must clamp to 200mV minimum noise floor (1.45V limit)
    psu_status.voltage_actual_int = 16500;
    psu_set_undervoltage_limit(&psu_status, 10);
    assert(psu_status.undervoltage_limit_int == 14500); // 1.45V
    assert(psu_status.undervoltage_limit_adc == (uint16_t)((14500UL * 4096UL) / 66000UL));

    // Test 3.3V with 10% limit: 330mV drop (> 200mV) -> 2.97V limit
    psu_status.voltage_actual_int = 33000;
    psu_set_undervoltage_limit(&psu_status, 10);
    assert(psu_status.undervoltage_limit_int == 29700); // 2.97V
    assert(psu_status.undervoltage_limit_adc == (uint16_t)((29700UL * 4096UL) / 66000UL));

    // Test 5.0V with 10% limit: 500mV drop (> 200mV) -> 4.50V limit
    psu_status.voltage_actual_int = 50000;
    psu_set_undervoltage_limit(&psu_status, 10);
    assert(psu_status.undervoltage_limit_int == 45000); // 4.50V
    assert(psu_status.undervoltage_limit_adc == (uint16_t)((45000UL * 4096UL) / 66000UL));

    // Test 100% (disabled)
    psu_set_undervoltage_limit(&psu_status, 100);
    assert(psu_status.undervoltage_limit_override == true);

    printf("[PASS] PSU undervoltage limit calculations verified\n");
}

// -------------------------------------------------------------------------
// Test 7: AMUX DMA Autonomous Sweep & Zero Spinlocks (ARCH-820)
// -------------------------------------------------------------------------
static void test_amux_dma_pipeline(void) {
    printf("[TEST] AMUX DMA autonomous sweep & zero spinlock verification...\n");

    /* Ensure initialized */
    bool ok = amux_dma_init();
    assert(ok);
    assert(amux_dma_is_enabled());

    int32_t ch_data = amux_dma_get_data_channel();
    int32_t ch_ctrl = amux_dma_get_ctrl_channel();
    assert(ch_data >= 0);
    assert(ch_ctrl >= 0);
    assert(ch_data != ch_ctrl);

    /* Setup mock channel ADC readings */
    s_mock_use_channel_adc = true;
    for (int i = 0; i < 16; i++) {
        s_mock_channel_adc[i] = (uint16_t)(1000 + i * 50);
    }

    /* Start DMA sweep */
    assert(!amux_dma_is_busy());
    ok = amux_dma_sweep_start();
    assert(ok);
    assert(!amux_dma_is_busy());

    /* Verify coherent snapshot extraction */
    uint16_t raw_coherent[HW_ADC_COUNT] = {0};
    ok = amux_dma_get_coherent(raw_coherent, HW_ADC_COUNT);
    assert(ok);
    for (int i = 0; i < HW_ADC_MUX_COUNT; i++) {
        assert(raw_coherent[i] == (uint16_t)(1000 + i * 50));
    }

    /* Verify amux_sweep() consumes DMA ping-pong buffer with ZERO spinlock ownership */
    assert(adc_get_owner() == -1);
    amux_sweep();
    assert(adc_get_owner() == -1); // Must NOT hold or leave spinlock acquired!

    for (int i = 0; i < HW_ADC_MUX_COUNT; i++) {
        assert(hw_adc_raw[i] == (uint16_t)(1000 + i * 50));
    }

    /* Verify deinit releases resources cleanly */
    amux_dma_deinit();
    assert(!amux_dma_is_enabled());
    assert(amux_dma_get_data_channel() == -1);
    assert(amux_dma_get_ctrl_channel() == -1);

    /* Re-init for subsequent test passes */
    ok = amux_dma_init();
    assert(ok);

    s_mock_use_channel_adc = false;
    printf("[PASS] AMUX DMA autonomous sweep & zero spinlocks verified\n");
}

// -------------------------------------------------------------------------
// Test 8: Stale Telemetry Timeout and Fault Injection Safe Shutdown
// -------------------------------------------------------------------------
static void test_psu_stale_telemetry_trip(void) {
    printf("[TEST] PSU stale telemetry trip & fault injection safe shutdown...\n");

    // Submit an active protocol job
    uint32_t job_id = 0;
    assert(bp_job_submit("proto_stream3", BP_JOB_PROTO_TEST, 5000, mock_job_slice, NULL, 0, &job_id) == BP_STATUS_OK);
    assert(job_id > 0);
    assert(bp_job_get_active_id() == job_id);

    // Setup active PSU
    psu_status.enabled = true;
    psu_status.error_overcurrent = false;
    psu_status.error_undervoltage = false;
    psu_status.error_stale_telemetry = false;
    psu_status.error_pending = false;
    psu_status.undervoltage_limit_adc = 500;
    s_mock_use_channel_adc = true;
    s_mock_channel_adc[HW_ADC_MUX_CURRENT_DETECT] = 2000;
    s_mock_channel_adc[HW_ADC_MUX_VREF_VOUT] = 1000;
    s_mock_gpio_fuse_tripped = false;

    // Refresh telemetry sweep
    assert(amux_dma_sweep_start());
    assert(amux_is_telemetry_fresh(AMUX_MAX_FRAME_AGE_US));

    // Initial check: all healthy, PSU enabled, job intact
    assert(!psu_poll_fuse_vout_error());
    assert(psu_status.enabled == true);
    assert(bp_job_get_active_id() == job_id);

    // Case 1: Advance time beyond 250ms (300ms) without new sweep -> Telemetry Stalls
    mock_time_advance_us(300000);
    assert(!amux_is_telemetry_fresh(AMUX_MAX_FRAME_AGE_US));

    // Next poll must trip due to stale telemetry!
    bool tripped = psu_poll_fuse_vout_error();
    assert(tripped == true);
    assert(psu_status.enabled == false);
    assert(psu_status.error_stale_telemetry == true);
    assert(psu_status.error_pending == true);

    // Verify active job cancelled
    assert(bp_job_check_cancelled() == true);
    assert(bp_job_service(100) == 1);
    bp_job_state_t jstate;
    bp_status_t jstatus;
    uint8_t jpct;
    assert(bp_job_get_state(job_id, &jstate, &jstatus, &jpct) == BP_STATUS_OK);
    assert(jstate == BP_JOB_STATE_CANCELLED);
    assert(bp_job_get_active_id() == 0);

    // Clear error
    psu_clear_error_flag();
    assert(!psu_status.error_pending);
    assert(!psu_status.error_stale_telemetry);

    // Case 2: Run a fresh sweep to bring telemetry back to life
    assert(amux_dma_sweep_start());
    assert(amux_is_telemetry_fresh(AMUX_MAX_FRAME_AGE_US));

    // Case 3: Explicit pipeline fault injection
    uint32_t job_id2 = 0;
    assert(bp_job_submit("proto_stream4", BP_JOB_PROTO_TEST, 5000, mock_job_slice, NULL, 0, &job_id2) == BP_STATUS_OK);
    psu_status.enabled = true;
    psu_status.error_pending = false;
    psu_status.error_stale_telemetry = false;

    amux_inject_fault(AMUX_FAULT_STALL);
    assert(amux_get_state() == AMUX_STATE_FAULT);
    assert(!amux_is_telemetry_fresh(AMUX_MAX_FRAME_AGE_US));

    // Extraction must fail while in fault state
    amux_frame_t frame;
    assert(!amux_get_frame(&frame));
    uint16_t raw_buf[16];
    assert(!amux_dma_get_coherent(raw_buf, 16));
    assert(!amux_dma_sweep_start()); // Sweeps blocked in fault

    // PSU poll must trip
    tripped = psu_poll_fuse_vout_error();
    assert(tripped == true);
    assert(psu_status.enabled == false);
    assert(psu_status.error_stale_telemetry == true);
    assert(bp_job_check_cancelled() == true);
    assert(bp_job_service(100) == 1);

    // Case 4: Clear fault and restore normal operation
    amux_clear_fault();
    assert(amux_get_state() == AMUX_STATE_IDLE);
    assert(amux_dma_sweep_start());
    assert(amux_is_telemetry_fresh(AMUX_MAX_FRAME_AGE_US));
    assert(amux_get_frame(&frame));
    assert(frame.valid);

    psu_clear_error_flag();
    s_mock_use_channel_adc = false;
    printf("[PASS] PSU stale telemetry trip & fault injection safe shutdown verified\n");
}

// -------------------------------------------------------------------------
// Test 9: Snapshot Lapping & Coherent Extraction (ARCH-820)
// -------------------------------------------------------------------------
static void test_amux_snapshot_lapping(void) {
    printf("[TEST] AMUX snapshot lapping and seqlock extraction...\n");

    amux_frame_t frame;
    assert(amux_get_frame(&frame));
    assert(frame.valid);
    uint32_t gen = frame.generation;

    // Trigger multiple rapid sweeps to advance generation
    for (int i = 0; i < 10; i++) {
        mock_time_advance_us(1000);
        assert(amux_dma_sweep_start());
    }

    assert(amux_get_frame(&frame));
    assert(frame.valid);
    assert(frame.generation == gen + 10);

    printf("[PASS] AMUX snapshot lapping and seqlock extraction verified\n");
}

int main(void) {
    printf("=== Running Power Monitor & ADC Subsystem Tests ===\n");
    amux_init();
    test_adc_reentrancy();
    test_adc_multicore_arbitration();
    test_seqlock_snapshot_concurrency();
    test_psu_overcurrent_job_abort();
    test_psu_undervoltage_debounce_and_trip();
    test_psu_undervoltage_calculation();
    test_amux_dma_pipeline();
    test_psu_stale_telemetry_trip();
    test_amux_snapshot_lapping();
    printf("=== ALL POWER MONITOR & ADC TESTS PASSED ===\n");
    return 0;
}
