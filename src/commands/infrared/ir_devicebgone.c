/**
 * @file ir_devicebgone.c
 * @brief INFRARED mode `devicebgone` command.
 * @details Sends a sequence of known power-off commands in the supported
 *          protocols to power down TVs, projectors, satellite receivers and
 *          other device types nearby.
 *
 * Based on the INFRARED feature set of the ESP32-Bit-Pirate project
 * (https://github.com/geo-tp/ESP32-Bit-Pirate), adapted for the Bus Pirate 5.
 * For a wider raw code sweep use the 'tvbgone' command.
 *
 * Modified for Bus Pirate 5 by community contributors (c) 2026
 * SPDX-License-Identifier: MIT
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "pirate.h"
#include "pirate/ir_protocols.h"
#include "command_struct.h"
#include "mode/infrared.h"
#include "lib/bp_args/bp_cmd.h"
#include "ui/ui_term.h"
#include "commands/infrared/ir_common.h"
#include "commands/infrared/ir_devicebgone.h"

static const char* const usage[] = {
    "devicebgone [-r <passes>]",
    "Send power-off commands:%s devicebgone",
    "Repeat the whole sequence 3 times:%s devicebgone -r 3",
};

static const bp_val_constraint_t passes_range = {
    .type = BP_VAL_UINT32,
    .u = { .min = 1, .max = 10, .def = 1 },
};

static const bp_command_opt_t opts[] = {
    { "passes", 'r', BP_ARG_REQUIRED, "1-10", T_HELP_IR_DEVICEBGONE_PASSES_FLAG, &passes_range },
    { 0 }
};

const bp_command_def_t ir_devicebgone_def = {
    .name         = "devicebgone",
    .description  = T_IR_CMD_DEVICEBGONE,
    .actions      = NULL,
    .action_count = 0,
    .opts         = opts,
    .usage        = usage,
    .usage_count  = count_of(usage),
};

// known power-off codes, verified against the Bit-Pirate universalOnOff table
// (engine protocol index, dev, sub, cmd, label)
static const struct _off_code {
    uint8_t proto;
    uint32_t dev;
    uint32_t sub;
    uint32_t cmd;
    const char *label;
} off_codes[] = {
    { 0,  4,  0, 8,      "NEC device" },          // {_NEC, 4, 0, 8}
    { 0,  80, 0, 63,     "NEC device" },          // {_NEC2, 80, -1, 63}
    { 3,  1,  0, 21,     "Sony TV" },             // {SONY12/20, 1, 0, 21}
    { 3,  2,  0, 21,     "Sony TV" },             // {SONY20, 2, 0, 21}
    { 6,  0,  0, 12,     "RC5 TV" },              // {_RC5, 0, 0, 12}
    { 7,  0,  0, 12,     "RC6 TV" },              // {_RC6, 0, 0, 12}
    { 1,  7,  0, 2,      "Samsung TV" },          // {SAMSUNG32, 7, 7, 2}
    { 2,  4,  0, 8,      "LG TV" },               // LG power (addr 4, cmd 8)
    { 5,  8,  0, 61,     "Panasonic TV" },        // {_PANASONIC, 8, 0, 61}
    { 8,  1,  0, 0x10,   "DISH satellite" },      // DISH power
    { 9,  1,  0, 233,    "Sharp device" },        // {_SHARP, 1, -1, 233}
};

void ir_devicebgone_handler(struct command_result *res) {
    if (bp_cmd_help_check(&ir_devicebgone_def, res->help_flag)) {
        return;
    }

    uint32_t passes;
    bp_cmd_status_t st = bp_cmd_flag(&ir_devicebgone_def, 'r', &passes);
    if (st == BP_CMD_INVALID) {
        res->error = true;
        return;
    }

    printf("Sending power-off commands for %u device types\r\n", count_of(off_codes));
    printf("Point the IR LED at the target device\r\n");

    // prepare the TX PIO once for the whole sequence
    uint32_t max_hz = 0;
    for (uint8_t i = 0; i < count_of(off_codes); i++) {
        uint32_t hz = ir_protocols_get(off_codes[i].proto)->carrier_hz;
        if (hz > max_hz) {
            max_hz = hz;
        }
    }
    ir_cmd_tx_begin(max_hz);

    for (uint32_t pass = 0; pass < passes; pass++) {
        for (uint8_t i = 0; i < count_of(off_codes); i++) {
            // ENTER stops the sequence (Bit-Pirate behavior)
            char c;
            if (ir_cmd_key_available(&c) && (c == '\r' || c == '\n')) {
                ir_cmd_tx_end();
                printf("\r\nStopped by user\r\n");
                return;
            }
            const ir_protocol_def_t *p = ir_protocols_get(off_codes[i].proto);
            printf("\r%s%s (%s, %ukHz): dev 0x%lx, cmd 0x%lx... ",
                   ui_term_color_info(), off_codes[i].label, p->display,
                   (unsigned)(p->carrier_hz / 1000),
                   (unsigned long)off_codes[i].dev, (unsigned long)off_codes[i].cmd);
            bool ok = ir_protocols_send(off_codes[i].proto, off_codes[i].dev, off_codes[i].sub, off_codes[i].cmd, 2);
            printf("%s\r\n", ok ? "done" : "error");
            busy_wait_us(250000); // 250ms between codes
        }
    }

    ir_cmd_tx_end();
    printf("\rDone!\r\n");
}
