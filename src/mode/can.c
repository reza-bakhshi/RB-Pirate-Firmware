/**
 * @file can.c
 * @brief Controller Area Network (CAN) protocol mode for Bus Pirate
 */

#include <stdio.h>
#include <string.h>
#if __has_include("pico/stdlib.h")
#include "pico/stdlib.h"
#endif
#include "pirate.h"
#include "system_config.h"
#include "command_struct.h"
#include "bytecode.h"
#include "pirate/bio.h"
#include "ui/ui_term.h"
#include "ui/ui_help.h"
#include "ui/ui_prompt.h"
#include "lib/bp_args/bp_cmd.h"
#include "pirate/storage.h"
#include "pirate/button.h"
#include "usb_rx.h"
#include "pirate/hwcan_pio.h"
#include "mode/can.h"

static struct {
    uint32_t bitrate;
    uint32_t bio_tx;
    uint32_t bio_rx;
    bool sniff_active;
} mode_config = {
    .bitrate = HWCAN_DEFAULT_BITRATE,
    .bio_tx = 0,
    .bio_rx = 1,
    .sniff_active = false,
};

// Syntax transmit staging state
static struct {
    uint32_t id;
    uint8_t dlc;
    uint8_t data[8];
    bool in_frame;
    bool has_id;
    bool is_extended;
    bool is_rtr;
} s_tx_staging;

static const char pin_labels[][5] = { "TX", "RX" };

// -----------------------------------------------------------------------------
// CLI Setup Definitions
// -----------------------------------------------------------------------------

static const bp_val_choice_t baud_choices[] = {
    { "1000000", "1M",   0, 1000000 },
    { "500000",  "500k", 0, 500000 },
    { "250000",  "250k", 0, 250000 },
    { "125000",  "125k", 0, 125000 },
    { "100000",  "100k", 0, 100000 },
    { "50000",   "50k",  0, 50000 },
    { "20000",   "20k",  0, 20000 },
};

static const bp_val_constraint_t can_baud_choice = {
    .type = BP_VAL_CHOICE,
    .choice = { .choices = baud_choices, .count = count_of(baud_choices), .def = 500000 },
    .prompt = 0,
};

static const bp_val_constraint_t can_pin_range = {
    .type = BP_VAL_UINT32,
    .u = { .min = 0, .max = 7, .def = 0 },
    .prompt = 0,
    .hint = 0,
};

static const bp_command_opt_t can_setup_opts[] = {
    { "baud", 'b', BP_ARG_REQUIRED, "20000-1000000", 0, &can_baud_choice },
    { "tx",   't', BP_ARG_REQUIRED, "0-7",           0, &can_pin_range },
    { "rx",   'r', BP_ARG_REQUIRED, "0-7",           0, &can_pin_range },
    { 0 },
};

const bp_command_def_t can_setup_def = {
    .name = "can",
    .opts = can_setup_opts,
};

// -----------------------------------------------------------------------------
// Mode Lifecycle & Config
// -----------------------------------------------------------------------------

uint32_t can_setup(void) {
    const char config_file[] = "rbcan.rb";
    const mode_config_t config_t[] = {
        { "$.baudrate", &mode_config.bitrate, MODE_CONFIG_FORMAT_DECIMAL },
        { "$.bio_tx",   &mode_config.bio_tx,  MODE_CONFIG_FORMAT_DECIMAL },
        { "$.bio_rx",   &mode_config.bio_rx,  MODE_CONFIG_FORMAT_DECIMAL },
    };

    uint32_t temp = 0;
    bp_cmd_status_t st = bp_cmd_flag(&can_setup_def, 'b', &temp);
    if (st == BP_CMD_INVALID) return 0;

    bool interactive = (st == BP_CMD_MISSING);

    if (interactive) {
        if (storage_load_mode(config_file, config_t, count_of(config_t))) {
            printf("\r\n\r\n%sUse previous settings?%s\r\n", ui_term_color_info(), ui_term_color_reset());
            can_settings();
            int r = bp_cmd_yes_no_exit("");
            if (r == BP_YN_EXIT) return 0;
            if (r == BP_YN_YES)  return 1;
        }

        if (bp_cmd_prompt(&can_baud_choice, &mode_config.bitrate) != BP_CMD_OK) return 0;

        printf("CAN TX pin: ");
        if (bp_cmd_prompt(&can_pin_range, &mode_config.bio_tx) != BP_CMD_OK) return 0;

        printf("CAN RX pin: ");
        if (bp_cmd_prompt(&can_pin_range, &mode_config.bio_rx) != BP_CMD_OK) return 0;

        if (mode_config.bio_tx == mode_config.bio_rx) {
            printf("%sError: TX and RX pins must be different%s\r\n", ui_term_color_error(), ui_term_color_reset());
            return 0;
        }
    } else {
        mode_config.bitrate = temp;
        st = bp_cmd_flag(&can_setup_def, 't', &temp);
        if (st == BP_CMD_OK) mode_config.bio_tx = temp;
        st = bp_cmd_flag(&can_setup_def, 'r', &temp);
        if (st == BP_CMD_OK) mode_config.bio_rx = temp;
    }

    storage_save_mode(config_file, config_t, count_of(config_t));
    can_settings();
    return 1;
}

uint32_t can_setup_exc(void) {
    if (!hwcan_init(mode_config.bitrate, (uint8_t)mode_config.bio_rx, (uint8_t)mode_config.bio_tx)) {
        printf("%sFailed to initialize CAN PIO hardware%s\r\n", ui_term_color_error(), ui_term_color_reset());
        return 0;
    }

    system_bio_update_purpose_and_label(true, mode_config.bio_tx, BP_PIN_MODE, pin_labels[0]);
    system_bio_update_purpose_and_label(true, mode_config.bio_rx, BP_PIN_MODE, pin_labels[1]);

    memset(&s_tx_staging, 0, sizeof(s_tx_staging));
    mode_config.sniff_active = false;
    return mode_config.bitrate;
}

void can_cleanup(void) {
    hwcan_cleanup();
    system_bio_update_purpose_and_label(false, mode_config.bio_tx, BP_PIN_MODE, 0);
    system_bio_update_purpose_and_label(false, mode_config.bio_rx, BP_PIN_MODE, 0);
    bio_init();
}

void can_settings(void) {
    printf("\r\nCAN Settings:\r\n");
    printf(" Bitrate: %u bps\r\n", mode_config.bitrate);
    printf(" TX Pin : BIO%u\r\n", mode_config.bio_tx);
    printf(" RX Pin : BIO%u\r\n", mode_config.bio_rx);
}

void can_help(void) {
    printf("CAN (Controller Area Network) 2.0B PIO mode\r\n");
    printf(" Syntax:\r\n");
    printf("  [ <id> <data0> <data1> ... ]   Transmit standard CAN frame\r\n");
    printf("  r                              Read / pop received frame from buffer\r\n");
    printf(" Commands:\r\n");
    printf("  can.sniff                      Live CAN packet monitor (press button to exit)\r\n");
    printf("  can.filter -i <id> -m <mask    Set hardware acceptance filter\r\n");
    printf("  can.stats                      Display packet counters\r\n");
}

uint32_t can_get_speed(void) {
    return mode_config.bitrate;
}

bool can_preflight_sanity_check(void) {
    return ui_help_sanity_check(true, 0x00);
}

// -----------------------------------------------------------------------------
// Syntax Execution Handlers
// -----------------------------------------------------------------------------

void can_start(struct _bytecode* result, struct _bytecode* next) {
    (void)next;
    s_tx_staging.in_frame = true;
    s_tx_staging.has_id = false;
    s_tx_staging.dlc = 0;
    s_tx_staging.is_extended = false;
    s_tx_staging.is_rtr = false;
    memset(s_tx_staging.data, 0, sizeof(s_tx_staging.data));
    result->error = SERR_NONE;
}

void can_start_alt(struct _bytecode* result, struct _bytecode* next) {
    can_start(result, next);
}

void can_stop(struct _bytecode* result, struct _bytecode* next) {
    (void)next;
    if (!s_tx_staging.in_frame || !s_tx_staging.has_id) {
        result->error = SERR_ERROR;
        s_tx_staging.in_frame = false;
        return;
    }

    bool ok = hwcan_tx_frame(s_tx_staging.id, s_tx_staging.dlc, s_tx_staging.data,
                             s_tx_staging.is_extended, s_tx_staging.is_rtr);
    result->error = ok ? SERR_NONE : SERR_ERROR;
    s_tx_staging.in_frame = false;
}

void can_stop_alt(struct _bytecode* result, struct _bytecode* next) {
    can_stop(result, next);
}

void can_write(struct _bytecode* result, struct _bytecode* next) {
    (void)next;
    if (!s_tx_staging.in_frame) {
        result->error = SERR_ERROR;
        return;
    }

    if (!s_tx_staging.has_id) {
        // First token inside brackets is the CAN ID
        s_tx_staging.id = result->out_data;
        if (s_tx_staging.id > 0x7FF) {
            s_tx_staging.is_extended = true;
        }
        s_tx_staging.has_id = true;
        result->error = SERR_NONE;
    } else {
        // Subsequent tokens are payload data bytes
        if (s_tx_staging.dlc < 8) {
            s_tx_staging.data[s_tx_staging.dlc++] = (uint8_t)(result->out_data & 0xFF);
            result->error = SERR_NONE;
        } else {
            result->error = SERR_ERROR; // Exceeded 8-byte maximum CAN DLC
        }
    }
}

void can_read(struct _bytecode* result, struct _bytecode* next) {
    (void)next;
    hwcan_frame_t frame;
    if (hwcan_pop_frame(&frame)) {
        result->in_data = frame.id;
        result->error = SERR_NONE;
        printf("\r\n[CAN RX] ID: 0x%03X (%s) DLC: %u Data:",
               frame.id, frame.is_extended ? "Ext" : "Std", frame.dlc);
        for (uint8_t i = 0; i < frame.dlc; i++) {
            printf(" %02X", frame.data[i]);
        }
        printf("\r\n");
    } else {
        result->error = SERR_ERROR;
    }
}

void can_clkh(struct _bytecode* result, struct _bytecode* next) { (void)result; (void)next; }
void can_clkl(struct _bytecode* result, struct _bytecode* next) { (void)result; (void)next; }
void can_dath(struct _bytecode* result, struct _bytecode* next) { (void)result; (void)next; }
void can_datl(struct _bytecode* result, struct _bytecode* next) { (void)result; (void)next; }
void can_dats(struct _bytecode* result, struct _bytecode* next) { (void)result; (void)next; }
void can_tick_clock(struct _bytecode* result, struct _bytecode* next) { (void)result; (void)next; }
void can_bitr(struct _bytecode* result, struct _bytecode* next) { (void)result; (void)next; }
void can_macro(uint32_t macro) { (void)macro; }

void can_periodic(void) {
    if (!mode_config.sniff_active) return;

    hwcan_frame_t frame;
    while (hwcan_pop_frame(&frame)) {
        printf("[CAN] %8u us | ID: 0x%0*X [%s%s] DLC: %u |",
               frame.timestamp_us,
               frame.is_extended ? 8 : 3,
               frame.id,
               frame.is_extended ? "EXT" : "STD",
               frame.is_rtr ? " RTR" : "",
               frame.dlc);
        for (uint8_t i = 0; i < frame.dlc; i++) {
            printf(" %02X", frame.data[i]);
        }
        printf("\r\n");
    }
}

// -----------------------------------------------------------------------------
// Mode Commands
// -----------------------------------------------------------------------------

static const bp_command_opt_t can_sniff_opts[] = {
    { "verbose", 'v', BP_ARG_NONE, "Verbose decoding", 0 },
    { 0 },
};

static const bp_command_def_t can_sniff_def = {
    .name = "sniff",
    .opts = can_sniff_opts,
};

static void can_sniff_handler(struct command_result* res) {
    (void)res;
    printf("%sStarting CAN sniffer. Press button or any key to stop.%s\r\n",
           ui_term_color_info(), ui_term_color_reset());

    mode_config.sniff_active = true;

    while (true) {
        can_periodic();

        char c;
        if (rx_fifo_try_get(&c)) {
            break;
        }
        if (button_get(0)) {
            break;
        }
        busy_wait_us_32(100);
    }

    mode_config.sniff_active = false;
    printf("\r\n%sCAN sniffer stopped.%s\r\n", ui_term_color_notice(), ui_term_color_reset());
}

static const bp_command_opt_t can_filter_opts[] = {
    { "id",      'i', BP_ARG_REQUIRED, "Acceptance ID filter", 0 },
    { "mask",    'm', BP_ARG_REQUIRED, "Acceptance bitmask", 0 },
    { "disable", 'd', BP_ARG_NONE,     "Disable filter", 0 },
    { 0 },
};

static const bp_command_def_t can_filter_def = {
    .name = "filter",
    .opts = can_filter_opts,
};

static void can_filter_handler(struct command_result* res) {
    (void)res;
    if (bp_cmd_find_flag(&can_filter_def, 'd')) {
        hwcan_set_filter(0, 0, false);
        printf("%sCAN filter disabled (accepting all IDs).%s\r\n",
               ui_term_color_info(), ui_term_color_reset());
        return;
    }

    uint32_t id = 0, mask = 0x7FF;
    bp_cmd_get_uint32(&can_filter_def, 'i', &id);
    bp_cmd_get_uint32(&can_filter_def, 'm', &mask);

    hwcan_set_filter(id, mask, true);
    printf("%sCAN filter enabled: ID=0x%X Mask=0x%X%s\r\n",
           ui_term_color_info(), id, mask, ui_term_color_reset());
}

static const bp_command_opt_t can_stats_opts[] = {
    { "reset", 'r', BP_ARG_NONE, "Reset statistics counters", 0 },
    { 0 },
};

static const bp_command_def_t can_stats_def = {
    .name = "stats",
    .opts = can_stats_opts,
};

static void can_stats_handler(struct command_result* res) {
    (void)res;
    if (bp_cmd_find_flag(&can_stats_def, 'r')) {
        hwcan_reset_stats();
        printf("%sCAN statistics counters reset.%s\r\n", ui_term_color_info(), ui_term_color_reset());
        return;
    }

    uint32_t rx = 0, tx = 0, err = 0;
    hwcan_get_stats(&rx, &tx, &err);
    printf("\r\nCAN Bus Statistics:\r\n");
    printf(" Frames Received   : %u\r\n", rx);
    printf(" Frames Transmitted: %u\r\n", tx);
    printf(" Errors / Overflows: %u\r\n", err);
    printf(" RX Queue Pending  : %u\r\n", hwcan_rx_available());
}

const struct _mode_command_struct can_commands[] = {
    {
        .func = &can_sniff_handler,
        .def = &can_sniff_def,
        .supress_fala_capture = true,
    },
    {
        .func = &can_filter_handler,
        .def = &can_filter_def,
        .supress_fala_capture = true,
    },
    {
        .func = &can_stats_handler,
        .def = &can_stats_def,
        .supress_fala_capture = true,
    },
};

const uint32_t can_commands_count = count_of(can_commands);
