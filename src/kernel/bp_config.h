/**
 * @file bp_config.h
 * @brief Configuration, runtime state, and lockless UI snapshot architecture.
 *
 * Separates persisted settings (bp_config_t), volatile control-plane runtime
 * state (bp_runtime_t), and seqlock-protected atomic UI snapshots (bp_ui_snapshot_t).
 */

#ifndef BP_CONFIG_H
#define BP_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include "kernel/bp_status.h"
#include "platform/rb-pirate.h"
#include "pirate/psu.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BP_CONFIG_CURRENT_VERSION 1

/**
 * @brief Persisted system settings (saved to/from rbconfig.rb on storage).
 */
typedef struct bp_config {
    uint32_t version;                   /* Configuration schema version */
    uint32_t terminal_language;         /* Language index */
    uint32_t terminal_ansi_color;       /* ANSI color scheme */
    uint32_t terminal_ansi_statusbar;   /* Statusbar display mode */
    uint32_t display_format;            /* Number display format (dec, hex, etc.) */
    uint32_t led_effect;                /* LED animation effect */
    uint32_t led_color;                 /* LED color word (0xRRGGBB) */
    uint32_t led_brightness_divisor;    /* LED brightness divisor */
    uint32_t terminal_usb_enable;       /* Enable USB CDC terminal */
    uint32_t terminal_uart_enable;      /* Enable UART terminal */
    uint32_t terminal_uart_number;      /* UART terminal port number */
    uint32_t debug_uart_enable;         /* Developer debug UART enable */
    uint32_t debug_uart_number;         /* Developer debug UART port */
    uint32_t disable_unique_usb_serial_number; /* Manufacturing serial disable */
    uint32_t bpio_debug_enable;         /* BPIO protocol debug enable */
} bp_config_t;

/**
 * @brief Volatile runtime state owned exclusively by Core 0 control plane.
 */
typedef struct bp_runtime {
    uint8_t  mode;                      /* Currently active mode ID */
    uint8_t  hiz;                       /* HiZ mode state */
    uint8_t  mode_active;               /* Mode active state */
    const char* subprotocol_name;       /* Subprotocol label if any */
    bool     config_loaded_from_file;   /* Loaded from filesystem */
    bool     safe_mode;                 /* Crash loop safe mode */
    uint8_t  hardware_revision;         /* Hardware revision */
    uint8_t  storage_available;         /* SD card present */
    uint8_t  storage_mount_error;       /* Mount error code */
    uint8_t  storage_fat_type;          /* FatFS type */
    float    storage_size;              /* Storage capacity in GB */
    uint8_t  binmode_select;            /* Active binary mode */
    bool     binmode_lock_terminal;     /* Terminal locked by binary mode */
    bool     binmode_suppress_monitor;  /* Monitor sweep suppressed */
    bool     binmode_usb_rx_queue_enable;
    bool     binmode_usb_tx_queue_enable;
    bool     terminal_hide_cursor;      /* Terminal cursor hidden */
    bool     terminal_toolbar_pause;    /* Toolbar refresh paused */
    uint8_t  terminal_update;           /* Terminal update flags */
    uint16_t terminal_ansi_rows;
    uint16_t terminal_ansi_columns;
    uint32_t big_buffer_owner;
    uint8_t  error;
    psu_cal_t psu_cal;                  /* Active PSU calibration coefficients */
} bp_runtime_t;

/**
 * @brief Lockless atomic UI snapshot read by Core 1 and UI toolbars.
 */
typedef struct bp_ui_snapshot {
    volatile uint32_t seq;              /* Seqlock sequence counter */
    uint32_t timestamp_ms;              /* Time of last snapshot */
    uint8_t  mode;                      /* Active mode */
    uint32_t vreg_mv;                   /* Vreg measured millivolts */
    uint32_t vout_mv;                   /* Vout measured millivolts */
    uint32_t current_ma;                /* Measured current in tenths of mA */
    uint16_t pin_voltages[HW_PINS];     /* Pin voltages in mV */
    const char* pin_labels[HW_PINS];    /* Pin function labels */
    enum bp_pin_func pin_func[HW_PINS]; /* Pin functional role */
    uint8_t  pwm_active;                /* Bitmask of active PWM channels */
    uint8_t  freq_active;               /* Bitmask of active freq channels */
    uint8_t  pullup_enabled;            /* Pullups enabled */
    uint8_t  open_drain;                /* Open drain enabled */
    bool     psu_enabled;               /* PSU regulator state */
    bool     psu_fuse_ok;               /* Current detect fuse OK */
    bool     psu_error_overcurrent;     /* Overcurrent fault tripped */
    bool     psu_error_undervoltage;    /* Undervoltage fault tripped */
} bp_ui_snapshot_t;

/**
 * @brief Initialize config, runtime, and UI snapshot subsystem.
 */
bp_status_t bp_config_subsystem_init(void);

/**
 * @brief Initialize a config struct with safe compiled-in defaults.
 */
void bp_config_init_defaults(bp_config_t *cfg);

/**
 * @brief Validate configuration fields against valid ranges.
 */
bool bp_config_validate(const bp_config_t *cfg);

/**
 * @brief Migrate configuration across versions.
 */
bp_status_t bp_config_migrate(bp_config_t *cfg, uint32_t from_version);

/**
 * @brief Get read-only pointer to active persisted config.
 */
const bp_config_t* bp_config_get(void);

/**
 * @brief Get mutable pointer to active persisted config (Core 0 only).
 */
bp_config_t* bp_config_get_mutable(void);

/**
 * @brief Get read-only pointer to active runtime state.
 */
const bp_runtime_t* bp_runtime_get(void);

/**
 * @brief Get mutable pointer to active runtime state (Core 0 only).
 */
bp_runtime_t* bp_runtime_get_mutable(void);

/**
 * @brief Publish a new UI snapshot (Core 0 writer).
 */
void bp_ui_snapshot_publish(uint32_t now_ms, uint8_t mode, uint32_t vreg_mv, uint32_t vout_mv, uint32_t current_ma);

/**
 * @brief Publish a new UI snapshot with power and pin voltage telemetry (Core 0 writer).
 */
void bp_ui_snapshot_publish_power(uint32_t now_ms,
                                  uint8_t mode,
                                  uint32_t vreg_mv,
                                  uint32_t vout_mv,
                                  uint32_t current_ma,
                                  const uint16_t pin_voltages[HW_PINS],
                                  bool psu_enabled,
                                  bool psu_fuse_ok,
                                  bool psu_error_overcurrent,
                                  bool psu_error_undervoltage);

/**
 * @brief Read an atomic, consistent UI snapshot (Core 1 / Toolbars reader).
 * Returns true on success, false if max retries exceeded.
 */
bool bp_ui_snapshot_read(bp_ui_snapshot_t *out_snap);

#ifdef __cplusplus
}
#endif

#endif // BP_CONFIG_H
