/**
 * @file ir_config.c
 * @brief INFRARED mode `config` command.
 * @details Configures which BIO pins are used for IR transmit (TX) and
 *          receive (RX). Defaults: TX = BIO4 (IR LED driver), RX = BIO5
 *          (36-40kHz demodulator). The selection is persisted in the mode
 *          settings. Note that the IR Toy hardware connects its sensors to
 *          BIO1/BIO3/BIO5/BIO7 and the IR LED to BIO4.
 *
 * Based on the INFRARED feature set of the ESP32-Bit-Pirate project
 * (https://github.com/geo-tp/ESP32-Bit-Pirate), adapted for the Bus Pirate 5.
 *
 * Modified for Bus Pirate 5 by community contributors (c) 2026
 * SPDX-License-Identifier: MIT
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "pirate.h"
#include "pirate/bio.h"
#include "command_struct.h"
#include "mode/infrared.h"
#include "lib/bp_args/bp_cmd.h"
#include "ui/ui_term.h"
#include "commands/infrared/ir_common.h"
#include "commands/infrared/ir_config.h"

static const char* const usage[] = {
    "config [-t <pin>] [-r <pin>]",
    "Interactive pin selection:%s config",
    "Set pins directly (BIO numbers):%s config -t 4 -r 5",
    "Hardware defaults:%s TX=BIO4 (IR LED), RX=BIO5 (36-40kHz demodulator)",
};

// non-const: the defaults are updated from the current settings at runtime
static bp_val_constraint_t pin_range_tx = {
    .type = BP_VAL_UINT32,
    .u = { .min = 0, .max = 7, .def = BIO4 },
    .prompt = T_IR_PIN_TX_PROMPT,
};

static bp_val_constraint_t pin_range_rx = {
    .type = BP_VAL_UINT32,
    .u = { .min = 0, .max = 7, .def = BIO5 },
    .prompt = T_IR_PIN_RX_PROMPT,
};

static const bp_command_opt_t opts[] = {
    { "tx", 't', BP_ARG_REQUIRED, "0-7", T_HELP_IR_CONFIG_TX_FLAG, &pin_range_tx },
    { "rx", 'r', BP_ARG_REQUIRED, "0-7", T_HELP_IR_CONFIG_RX_FLAG, &pin_range_rx },
    { 0 }
};

const bp_command_def_t ir_config_def = {
    .name         = "config",
    .description  = T_IR_CMD_CONFIG,
    .actions      = NULL,
    .action_count = 0,
    .opts         = opts,
    .usage        = usage,
    .usage_count  = count_of(usage),
};

void ir_config_handler(struct command_result *res) {
    if (bp_cmd_help_check(&ir_config_def, res->help_flag)) {
        return;
    }

    uint32_t tx_pin;
    uint32_t rx_pin;

    // current settings are the defaults, flags override
    pin_range_tx.u.def = infrared_get_tx_pin();
    pin_range_rx.u.def = infrared_get_rx_pin();

    printf("Current pins: TX=BIO%u, RX=BIO%u\r\n", infrared_get_tx_pin(), infrared_get_rx_pin());
    printf("IR Toy hardware: TX=BIO4 (IR LED), RX sensors: BIO1 (learner), BIO3 (38kHz barrier), BIO5 (36-40kHz), BIO7 (56kHz)\r\n");

    bp_cmd_status_t stx = bp_cmd_flag(&ir_config_def, 't', &tx_pin);
    if (stx == BP_CMD_INVALID) {
        res->error = true;
        return;
    }
    if (stx == BP_CMD_MISSING) {
        if (bp_cmd_prompt(&pin_range_tx, &tx_pin) != BP_CMD_OK) {
            return;
        }
    }

    bp_cmd_status_t srx = bp_cmd_flag(&ir_config_def, 'r', &rx_pin);
    if (srx == BP_CMD_INVALID) {
        res->error = true;
        return;
    }
    if (srx == BP_CMD_MISSING) {
        if (bp_cmd_prompt(&pin_range_rx, &rx_pin) != BP_CMD_OK) {
            return;
        }
    }

    if (!infrared_set_pins((uint8_t)tx_pin, (uint8_t)rx_pin)) {
        printf("Error setting pins\r\n");
        res->error = true;
        return;
    }

    printf("%sPins configured:%s TX=BIO%u (buffer pin %u), RX=BIO%u (buffer pin %u)\r\n",
           ui_term_color_info(), ui_term_color_reset(),
           (uint8_t)tx_pin, bio2bufiopin[tx_pin], (uint8_t)rx_pin, bio2bufiopin[rx_pin]);
}
