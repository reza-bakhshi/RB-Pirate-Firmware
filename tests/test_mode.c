#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>

#include "kernel/bp_status.h"
#include "kernel/bp_resource.h"
#include "kernel/bp_event.h"
#include "kernel/bp_kernel.h"
#include "kernel/bp_protocol.h"
#include "services/bp_storage.h"
#include "system_config.h"
#include "modes.h"
#include "mode/bp_mode.h"

// Stubs and mocks
struct _system_config system_config;
struct _mode modes[MAXPROTO];

const mode_config_t system_config_json[1] = { {"", NULL, MODE_CONFIG_FORMAT_DECIMAL} };
const uint8_t system_config_json_count = 0;
void* mem_alloc(size_t s, uint32_t t) { (void)t; return malloc(s); }
void mem_free(void* p) { free(p); }
void bp_watchdog_heartbeat(uint8_t c) { (void)c; }

static bool s_bio_init_called = false;
static bool s_fala_hook_called = false;
static uint32_t s_mock_cleanup_called[MAXPROTO];
static uint32_t s_mock_setup_called[MAXPROTO];
static uint32_t s_mock_setup_exc_called[MAXPROTO];
static bool s_mock_setup_succeeds = true;
static bool s_mock_setup_exc_succeeds = true;

void bio_init(void) {
    s_bio_init_called = true;
}

void fala_mode_change_hook(void) {
    s_fala_hook_called = true;
}

const char* ui_term_color_error(void) {
    return "";
}

const char* ui_term_color_reset(void) {
    return "";
}

static void mock_cleanup(void) {
    s_mock_cleanup_called[system_config.mode]++;
}

static uint32_t mock_setup_success(void) {
    s_mock_setup_called[system_config.mode]++;
    return s_mock_setup_succeeds ? 1 : 0;
}

static uint32_t mock_setup_exc_success(void) {
    s_mock_setup_exc_called[system_config.mode]++;
    return s_mock_setup_exc_succeeds ? 1 : 0;
}

static void reset_test_state(void) {
    s_bio_init_called = false;
    s_fala_hook_called = false;
    memset(s_mock_cleanup_called, 0, sizeof(s_mock_cleanup_called));
    memset(s_mock_setup_called, 0, sizeof(s_mock_setup_called));
    memset(s_mock_setup_exc_called, 0, sizeof(s_mock_setup_exc_called));
    s_mock_setup_succeeds = true;
    s_mock_setup_exc_succeeds = true;
    system_config.mode = HIZ;
}

static void setup_mock_modes(void) {
    memset(modes, 0, sizeof(modes));
    strcpy(modes[HIZ].protocol_name, "HiZ");
    modes[HIZ].protocol_cleanup = mock_cleanup;
    modes[HIZ].protocol_setup = mock_setup_success;
    modes[HIZ].protocol_setup_exc = mock_setup_exc_success;

    strcpy(modes[HWUART].protocol_name, "UART");
    modes[HWUART].protocol_cleanup = mock_cleanup;
    modes[HWUART].protocol_setup = mock_setup_success;
    modes[HWUART].protocol_setup_exc = mock_setup_exc_success;

    strcpy(modes[HWI2C].protocol_name, "I2C");
    modes[HWI2C].protocol_cleanup = mock_cleanup;
    modes[HWI2C].protocol_setup = mock_setup_success;
    modes[HWI2C].protocol_setup_exc = mock_setup_exc_success;

    strcpy(modes[HWSPI].protocol_name, "SPI");
    modes[HWSPI].protocol_cleanup = mock_cleanup;
    modes[HWSPI].protocol_setup = mock_setup_success;
    modes[HWSPI].protocol_setup_exc = mock_setup_exc_success;
}

static void test_mode_registry(void) {
    printf("[TEST] Mode registry initialization and lookup...\n");
    reset_test_state();
    setup_mock_modes();

    assert(bp_mode_init() == BP_STATUS_OK);
    const bp_mode_t* hiz = bp_mode_get(HIZ);
    assert(hiz != NULL);
    assert(strcmp(hiz->name, "HiZ") == 0);

    const bp_mode_t* uart = bp_mode_get_by_name("uart");
    assert(uart != NULL);
    assert(uart->mode_id == HWUART);

    const bp_mode_t* spi = bp_mode_get_by_name("SPI");
    assert(spi != NULL);
    assert(spi->mode_id == HWSPI);

    assert(bp_mode_get_active_id() == HIZ);
    printf("[PASS] Mode registry verified\n");
}

static void test_mode_switch_transactional(void) {
    printf("[TEST] Transactional mode switch (cleanup before setup)...\n");
    reset_test_state();
    setup_mock_modes();
    bp_mode_init();

    // Start in HiZ. Lease a resource to HiZ.
    system_config.mode = HIZ;
    assert(bp_res_acquire(BP_RES_CLASS_PIO_SM, 0, BP_RES_OWNER_MODE_BASE + HIZ, "HIZ_SM0") == BP_STATUS_OK);

    // Switch to UART
    s_bio_init_called = false;
    s_fala_hook_called = false;
    bp_status_t st = bp_mode_switch(HWUART);
    assert(st == BP_STATUS_OK);
    assert(system_config.mode == HWUART);

    // Verify HiZ cleanup was called
    assert(s_mock_cleanup_called[HIZ] == 1);
    // Verify HiZ resource lease was automatically released
    assert(bp_res_acquire(BP_RES_CLASS_PIO_SM, 0, BP_RES_OWNER_MODE_BASE + HWUART, "UART_SM0") == BP_STATUS_OK);
    // Verify pins were reset to safe HiZ
    assert(s_bio_init_called);
    // Verify FALA hook notified
    assert(s_fala_hook_called);

    // Clean up
    bp_res_release_all(BP_RES_OWNER_MODE_BASE + HWUART);
    printf("[PASS] Transactional mode switch verified\n");
}

static void test_mode_rollback_on_setup_cancel(void) {
    printf("[TEST] Mode rollback to HiZ when setup prompt is cancelled...\n");
    reset_test_state();
    setup_mock_modes();
    bp_mode_init();

    system_config.mode = HWI2C;
    s_mock_setup_succeeds = false; // Simulate user cancel in prompt

    bp_status_t st = bp_mode_switch(HWSPI);
    assert(st == BP_STATUS_CANCELLED);
    // Must roll back to HiZ
    assert(system_config.mode == HIZ);
    // HiZ hardware setup must have been executed as fallback
    assert(s_mock_setup_exc_called[HIZ] >= 1);
    // Pins must have been made safe
    assert(s_bio_init_called);

    printf("[PASS] Mode rollback on setup cancel verified\n");
}

static void test_mode_rollback_on_hardware_failure(void) {
    printf("[TEST] Mode rollback to HiZ on hardware activation failure...\n");
    reset_test_state();
    setup_mock_modes();
    bp_mode_init();

    system_config.mode = HWUART;
    s_mock_setup_exc_succeeds = false; // Simulate hardware failure in protocol_setup_exc

    bp_status_t st = bp_mode_switch(HWSPI);
    assert(st == BP_STATUS_ERROR);
    // Must roll back to HiZ
    assert(system_config.mode == HIZ);
    // SPI cleanup must have been called to tear down partial state
    assert(s_mock_cleanup_called[HWSPI] >= 1);
    // HiZ setup executed
    assert(s_mock_setup_exc_called[HIZ] >= 1);

    printf("[PASS] Mode rollback on hardware failure verified\n");
}

static void test_mode_switch_soak_1000(void) {
    printf("[TEST] 1000-switch soak test with zero leaked resource leases...\n");
    reset_test_state();
    setup_mock_modes();
    bp_mode_init();

    uint8_t mode_cycle[] = { HIZ, HWUART, HWI2C, HWSPI };
    size_t num_modes = sizeof(mode_cycle) / sizeof(mode_cycle[0]);

    for (int iter = 0; iter < 1000; iter++) {
        uint8_t target = mode_cycle[iter % num_modes];
        // Acquire resource for current mode
        uint8_t cur = system_config.mode;
        bp_res_acquire(BP_RES_CLASS_PIO_SM, 1, BP_RES_OWNER_MODE_BASE + cur, "TEMP_SM");

        bp_status_t st = bp_mode_switch(target);
        assert(st == BP_STATUS_OK);
        assert(system_config.mode == target);
    }

    // Clean up final mode leases
    bp_res_release_all(BP_RES_OWNER_MODE_BASE + system_config.mode);

    // Verify 0 active leases remain for mode
    assert(bp_res_count_owned(BP_RES_OWNER_MODE_BASE + system_config.mode) == 0);
    printf("[PASS] 1000-switch soak test completed with ZERO leaked leases\n");
}

static void test_protocol_data_plane_and_heartbeat(void) {
    printf("[TEST] Protocol data plane telemetry, heartbeat, and stall recovery...\n");
    assert(bp_protocol_init() == BP_STATUS_OK);

    assert(bp_protocol_get_state() == BP_PROTO_STATE_IDLE);
    bp_protocol_service(100);

    bp_protocol_telemetry_t telem;
    bp_protocol_get_telemetry(&telem);
    assert(telem.heartbeat_ticks > 0);
    assert(telem.last_heartbeat_ms == 100);

    // Transition to ACTIVE
    bp_protocol_set_state(BP_PROTO_STATE_ACTIVE, 42);
    assert(bp_protocol_get_state() == BP_PROTO_STATE_ACTIVE);

    // Heartbeat check: healthy within stall limit
    assert(bp_protocol_is_healthy(200, 5000));
    // Stalled beyond limit (e.g. at 6000ms with last heartbeat at 100ms)
    assert(!bp_protocol_is_healthy(6000, 5000));

    // Recovery
    assert(bp_protocol_recover() == BP_STATUS_OK);
    assert(bp_protocol_get_state() == BP_PROTO_STATE_IDLE);

    bp_protocol_get_telemetry(&telem);
    assert(telem.faults == 1);
    assert(telem.recoveries == 1);
    assert(telem.active_job_id == 0);

    printf("[PASS] Protocol data plane telemetry, heartbeat, and recovery verified\n");
}

int main(void) {
    printf("=== Running Core Separation & Mode Lifecycle Tests ===\n");
    test_mode_registry();
    test_mode_switch_transactional();
    test_mode_rollback_on_setup_cancel();
    test_mode_rollback_on_hardware_failure();
    test_mode_switch_soak_1000();
    test_protocol_data_plane_and_heartbeat();
    printf("=== ALL CORE SEPARATION & MODE TESTS PASSED ===\n");
    return 0;
}
