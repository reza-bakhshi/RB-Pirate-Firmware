#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>

#include "kernel/bp_status.h"
#include "kernel/bp_config.h"

static void test_config_defaults(void) {
    printf("[TEST] Config default initialization...\n");
    bp_config_t cfg;
    bp_config_init_defaults(&cfg);

    assert(cfg.version == BP_CONFIG_CURRENT_VERSION);
    assert(cfg.terminal_language == 0);
    assert(cfg.terminal_ansi_color == 1);
    assert(cfg.terminal_ansi_statusbar == 1);
    assert(cfg.display_format == 0);
    assert(cfg.led_brightness_divisor == 10);
    assert(cfg.terminal_usb_enable == 1);
    assert(cfg.terminal_uart_enable == 0);
    assert(cfg.terminal_uart_number == 1);
    assert(cfg.debug_uart_enable == 0);
    assert(cfg.debug_uart_number == 0);
    assert(cfg.disable_unique_usb_serial_number == 0);
    assert(cfg.bpio_debug_enable == 0);

    assert(bp_config_validate(&cfg));
    printf("[PASS] Config defaults verified\n");
}

static void test_config_validation(void) {
    printf("[TEST] Config boundary validation...\n");
    bp_config_t cfg;
    bp_config_init_defaults(&cfg);

    // NULL pointer
    assert(!bp_config_validate(NULL));

    // Zero version
    cfg.version = 0;
    assert(!bp_config_validate(&cfg));
    cfg.version = BP_CONFIG_CURRENT_VERSION;

    // Bad LED brightness divisor
    cfg.led_brightness_divisor = 0;
    assert(!bp_config_validate(&cfg));
    cfg.led_brightness_divisor = 101;
    assert(!bp_config_validate(&cfg));
    cfg.led_brightness_divisor = 10;
    assert(bp_config_validate(&cfg));

    // Bad UART ports
    cfg.terminal_uart_number = 2;
    assert(!bp_config_validate(&cfg));
    cfg.terminal_uart_number = 1;

    cfg.debug_uart_number = 3;
    assert(!bp_config_validate(&cfg));
    cfg.debug_uart_number = 0;

    assert(bp_config_validate(&cfg));
    printf("[PASS] Config validation verified\n");
}

static void test_config_migration(void) {
    printf("[TEST] Config migration from legacy schema...\n");
    bp_config_t legacy_cfg;
    memset(&legacy_cfg, 0, sizeof(legacy_cfg));
    legacy_cfg.version = 0;
    legacy_cfg.terminal_language = 1;
    legacy_cfg.led_brightness_divisor = 5;

    assert(bp_config_migrate(&legacy_cfg, 0) == BP_STATUS_OK);
    assert(legacy_cfg.version == 1);
    assert(legacy_cfg.terminal_language == 1);
    assert(legacy_cfg.led_brightness_divisor == 5);
    assert(legacy_cfg.bpio_debug_enable == 0);

    printf("[PASS] Config migration verified\n");
}

static void test_runtime_state_isolation(void) {
    printf("[TEST] Runtime state isolation from persisted config...\n");
    assert(bp_config_subsystem_init() == BP_STATUS_OK);

    bp_config_t* cfg = bp_config_get_mutable();
    bp_runtime_t* rt = bp_runtime_get_mutable();

    cfg->terminal_language = 2;
    rt->mode = 5;
    rt->storage_available = 1;

    // Modifying runtime does not touch persisted config
    assert(cfg->terminal_language == 2);
    assert(bp_config_get()->terminal_language == 2);
    assert(bp_runtime_get()->mode == 5);
    assert(bp_runtime_get()->storage_available == 1);

    printf("[PASS] Runtime state isolation verified\n");
}

static void test_ui_snapshot_seqlock(void) {
    printf("[TEST] UI snapshot seqlock atomic read/write (10,000 iterations)...\n");
    assert(bp_config_subsystem_init() == BP_STATUS_OK);

    // Initial publish
    bp_ui_snapshot_publish(100, 2, 3300, 3290, 150);

    bp_ui_snapshot_t snap;
    assert(bp_ui_snapshot_read(&snap));
    assert(snap.timestamp_ms == 100);
    assert(snap.mode == 2);
    assert(snap.vreg_mv == 3300);
    assert(snap.vout_mv == 3290);
    assert(snap.current_ma == 150);

    // 10,000 rapid updates & reads
    for (uint32_t i = 1; i <= 10000; i++) {
        uint8_t mode = (uint8_t)(i % 16);
        uint32_t v = 3300 + (i % 100);
        bp_ui_snapshot_publish(i, mode, v, v - 10, i % 500);

        bp_ui_snapshot_t read_snap;
        bool ok = bp_ui_snapshot_read(&read_snap);
        assert(ok);
        // Consistency: read snapshot must be self-consistent (vout_mv == vreg_mv - 10)
        assert(read_snap.vout_mv == read_snap.vreg_mv - 10);
    }

    printf("[PASS] UI snapshot seqlock verified with 10,000 consistent cycles\n");
}

int main(void) {
    printf("=== Running Configuration & UI Snapshot Test Suite ===\n");
    test_config_defaults();
    test_config_validation();
    test_config_migration();
    test_runtime_state_isolation();
    test_ui_snapshot_seqlock();
    printf("=== ALL CONFIGURATION & SNAPSHOT TESTS PASSED ===\n");
    return 0;
}
