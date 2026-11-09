/**
 * @file bp_config.c
 * @brief Implementation of config, runtime, and UI snapshot services.
 */

#include "kernel/bp_config.h"
#include <string.h>

#if __has_include("pico/stdlib.h") && !defined(BP_HOST_TEST)
#include "pico/stdlib.h"
#include "hardware/sync.h"
#endif

static bp_config_t s_config;
static bp_runtime_t s_runtime;
static bp_ui_snapshot_t s_snapshot;
static bool s_initialized = false;

static inline void bp_memory_barrier(void) {
#if defined(__arm__) || defined(__thumb__)
    __asm volatile("dmb" : : : "memory");
#else
    __sync_synchronize();
#endif
}

void bp_config_init_defaults(bp_config_t *cfg) {
    if (!cfg) return;
    memset(cfg, 0, sizeof(bp_config_t));
    cfg->version = BP_CONFIG_CURRENT_VERSION;
    cfg->terminal_language = 0;
    cfg->terminal_ansi_color = 1;
    cfg->terminal_ansi_statusbar = 1;
    cfg->display_format = 0;
    cfg->led_effect = 8;
    cfg->led_color = 0x0000FF;
    cfg->led_brightness_divisor = 10;
    cfg->terminal_usb_enable = 1;
    cfg->terminal_uart_enable = 0;
    cfg->terminal_uart_number = 1;
    cfg->debug_uart_enable = 0;
    cfg->debug_uart_number = 0;
    cfg->disable_unique_usb_serial_number = 0;
    cfg->bpio_debug_enable = 0;
}

bool bp_config_validate(const bp_config_t *cfg) {
    if (!cfg) return false;
    if (cfg->version == 0 || cfg->version > BP_CONFIG_CURRENT_VERSION) return false;
    if (cfg->led_brightness_divisor == 0 || cfg->led_brightness_divisor > 100) return false;
    if (cfg->terminal_uart_number > 1) return false;
    if (cfg->debug_uart_number > 1) return false;
    return true;
}

bp_status_t bp_config_migrate(bp_config_t *cfg, uint32_t from_version) {
    if (!cfg) return BP_STATUS_INVALID_PARAM;
    if (from_version == 0) {
        // Upgrade legacy schema to version 1
        cfg->version = 1;
        cfg->bpio_debug_enable = 0;
    }
    return BP_STATUS_OK;
}

bp_status_t bp_config_subsystem_init(void) {
    if (s_initialized) return BP_STATUS_OK;

    bp_config_init_defaults(&s_config);
    memset(&s_runtime, 0, sizeof(s_runtime));
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    s_snapshot.seq = 0;

    s_initialized = true;
    return BP_STATUS_OK;
}

const bp_config_t* bp_config_get(void) {
    if (!s_initialized) bp_config_subsystem_init();
    return &s_config;
}

bp_config_t* bp_config_get_mutable(void) {
    if (!s_initialized) bp_config_subsystem_init();
    return &s_config;
}

const bp_runtime_t* bp_runtime_get(void) {
    if (!s_initialized) bp_config_subsystem_init();
    return &s_runtime;
}

bp_runtime_t* bp_runtime_get_mutable(void) {
    if (!s_initialized) bp_config_subsystem_init();
    return &s_runtime;
}

void bp_ui_snapshot_publish_power(uint32_t now_ms,
                                  uint8_t mode,
                                  uint32_t vreg_mv,
                                  uint32_t vout_mv,
                                  uint32_t current_ma,
                                  const uint16_t pin_voltages[HW_PINS],
                                  bool psu_enabled,
                                  bool psu_fuse_ok,
                                  bool psu_error_overcurrent,
                                  bool psu_error_undervoltage) {
    uint32_t seq = s_snapshot.seq;
    s_snapshot.seq = seq + 1; // Mark write in progress (odd)
    bp_memory_barrier();

    s_snapshot.timestamp_ms = now_ms;
    s_snapshot.mode = mode;
    s_snapshot.vreg_mv = vreg_mv;
    s_snapshot.vout_mv = vout_mv;
    s_snapshot.current_ma = current_ma;
    if (pin_voltages) {
        for (int i = 0; i < HW_PINS; i++) {
            s_snapshot.pin_voltages[i] = pin_voltages[i];
        }
    }
    s_snapshot.psu_enabled = psu_enabled;
    s_snapshot.psu_fuse_ok = psu_fuse_ok;
    s_snapshot.psu_error_overcurrent = psu_error_overcurrent;
    s_snapshot.psu_error_undervoltage = psu_error_undervoltage;

    bp_memory_barrier();
    s_snapshot.seq = seq + 2; // Mark write complete (even)
}

void bp_ui_snapshot_publish(uint32_t now_ms, uint8_t mode, uint32_t vreg_mv, uint32_t vout_mv, uint32_t current_ma) {
    bp_ui_snapshot_publish_power(now_ms, mode, vreg_mv, vout_mv, current_ma, NULL, false, true, false, false);
}

bool bp_ui_snapshot_read(bp_ui_snapshot_t *out_snap) {
    if (!out_snap) return false;

    uint32_t s0, s1;
    uint32_t retries = 0;

    while (retries < 16) {
        s0 = s_snapshot.seq;
        if (s0 & 1) {
            retries++;
            continue;
        }

        bp_memory_barrier();
        *out_snap = s_snapshot;
        bp_memory_barrier();

        s1 = s_snapshot.seq;
        if (s0 == s1 && !(s1 & 1)) {
            return true;
        }
        retries++;
    }

    return false;
}
