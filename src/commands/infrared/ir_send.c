/**
 * @file ir_send.c
 * @brief INFRARED mode `send` command.
 * @details Sends an IR command with the current protocol, device, sub-device
 *          and command, repeating the frame (default 3 times). Check IRDB
 *          (https://github.com/probonopd/irdb) for device/sub/command codes.
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
#include "pirate/ir_protocols.h"
#include "command_struct.h"
#include "mode/infrared.h"
#include "lib/bp_args/bp_cmd.h"
#include "ui/ui_term.h"
#include "commands/infrared/ir_common.h"
#include "commands/infrared/ir_send.h"

static const char* const usage[] = {
    "send <dev> <sub> <cmd> [-p <protocol>] [-r <repeats>]",
    "Send command:%s send 0 0 64",
    "One-shot protocol:%s send 7 0 2 -p samsung",
    "Repeat 10 times:%s send 4 0 8 -r 10",
    "Protocol argument conventions:%s dev=sub=cmd are protocol specific (see -h -p)",
};

// protocol selection for -p (must match the ir_protocol_table order)
static const bp_val_choice_t ir_proto_choices[] = {
    { "nec", NULL, T_IR_PROTO_NEC, 0 },
    { "samsung", NULL, T_IR_PROTO_SAMSUNG, 1 },
    { "lg", NULL, T_IR_PROTO_LG, 2 },
    { "sony", NULL, T_IR_PROTO_SONY, 3 },
    { "jvc", NULL, T_IR_PROTO_JVC, 4 },
    { "panasonic", NULL, T_IR_PROTO_PANASONIC, 5 },
    { "rc5", NULL, T_IR_PROTO_RC5, 6 },
    { "rc6", NULL, T_IR_PROTO_RC6, 7 },
    { "dish", NULL, T_IR_PROTO_DISH, 8 },
    { "sharp", NULL, T_IR_PROTO_SHARP, 9 },
};
// keep in sync with the engine table
typedef char _ir_proto_choice_check[(count_of(ir_proto_choices) == IR_PROTOCOL_COUNT) ? 1 : -1];

static const bp_val_constraint_t ir_proto_choice = {
    .type = BP_VAL_CHOICE,
    .choice = { .choices = ir_proto_choices, .count = count_of(ir_proto_choices), .def = 0 },
    .prompt = T_IR_PROTOCOL_MENU,
};

static const bp_val_constraint_t repeats_range = {
    .type = BP_VAL_UINT32,
    .u = { .min = 1, .max = 50, .def = 3 },
};

static const bp_command_positional_t positionals[] = {
    { "dev", "device", T_HELP_IR_SEND_DEV, true },
    { "sub", "subdevice", T_HELP_IR_SEND_SUB, true },
    { "cmd", "command", T_HELP_IR_SEND_CMD, true },
    { 0 }
};

static const bp_command_opt_t opts[] = {
    { "protocol", 'p', BP_ARG_REQUIRED, "protocol", T_HELP_IR_SEND_PROTOCOL_FLAG, &ir_proto_choice },
    { "repeats",  'r', BP_ARG_REQUIRED, "1-50",     T_HELP_IR_SEND_REPEATS_FLAG, &repeats_range },
    { 0 }
};

const bp_command_def_t ir_send_def = {
    .name         = "send",
    .description  = T_IR_CMD_SEND,
    .actions      = NULL,
    .action_count = 0,
    .opts         = opts,
    .positionals  = positionals,
    .positional_count = 3,
    .usage        = usage,
    .usage_count  = count_of(usage),
};

void ir_send_handler(struct command_result *res) {
    if (bp_cmd_help_check(&ir_send_def, res->help_flag)) {
        return;
    }

    uint32_t dev, sub, cmd, repeats;
    uint8_t proto = infrared_get_engine_protocol();

    if (bp_cmd_get_positional_uint32(&ir_send_def, 1, &dev) != true ||
        bp_cmd_get_positional_uint32(&ir_send_def, 2, &sub) != true ||
        bp_cmd_get_positional_uint32(&ir_send_def, 3, &cmd) != true) {
        printf("Device, sub-device and command are required\r\n");
        bp_cmd_help_show(&ir_send_def);
        res->error = true;
        return;
    }

    // one-shot protocol override
    uint32_t proto_value;
    bp_cmd_status_t pst = bp_cmd_flag(&ir_send_def, 'p', &proto_value);
    if (pst == BP_CMD_OK) {
        proto = (uint8_t)proto_value;
    } else if (pst == BP_CMD_INVALID) {
        res->error = true;
        return;
    }

    bp_cmd_status_t rst = bp_cmd_flag(&ir_send_def, 'r', &repeats);
    if (rst == BP_CMD_INVALID) {
        res->error = true;
        return;
    }

    const ir_protocol_def_t *p = ir_protocols_get(proto);
    printf("%s%s%s %ukHz: dev 0x%lx (%lu), sub 0x%lx (%lu), cmd 0x%lx (%lu)\r\n",
           ui_term_color_info(), p->display, ui_term_color_reset(),
           (unsigned)(p->carrier_hz / 1000),
           (unsigned long)dev, (unsigned long)dev,
           (unsigned long)sub, (unsigned long)sub,
           (unsigned long)cmd, (unsigned long)cmd);

    ir_cmd_tx_begin(p->carrier_hz);
    bool ok = ir_protocols_send(proto, dev, sub, cmd, (uint8_t)repeats);
    ir_cmd_tx_end();

    if (!ok) {
        printf("Error encoding frame\r\n");
        res->error = true;
        return;
    }
    printf("Sent %lu frame(s)\r\n", (unsigned long)repeats);
}
