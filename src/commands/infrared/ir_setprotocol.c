/**
 * @file ir_setprotocol.c
 * @brief INFRARED mode `setprotocol` command.
 * @details Selects the IR protocol used by send/receive/remote/devicebgone.
 *          The selection is stored in the mode settings.
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
#include <string.h>
#include "pico/stdlib.h"
#include "pirate.h"
#include "pirate/ir_protocols.h"
#include "command_struct.h"
#include "mode/infrared.h"
#include "lib/bp_args/bp_cmd.h"
#include "ui/ui_term.h"
#include "commands/infrared/ir_common.h"
#include "commands/infrared/ir_setprotocol.h"

static const char* const usage[] = {
    "setprotocol [protocol]",
    "Interactive protocol menu:%s setprotocol",
    "Select directly:%s setprotocol nec",
};

static const bp_command_positional_t positionals[] = {
    { "protocol", "protocol", T_HELP_IR_SETPROTOCOL_NAME, false },
    { 0 }
};

const bp_command_def_t ir_setprotocol_def = {
    .name         = "setprotocol",
    .description  = T_IR_CMD_SETPROTOCOL,
    .actions      = NULL,
    .action_count = 0,
    .opts         = NULL,
    .positionals      = positionals,
    .positional_count = 1,
    .usage        = usage,
    .usage_count  = count_of(usage),
};

void ir_setprotocol_handler(struct command_result *res) {
    if (bp_cmd_help_check(&ir_setprotocol_def, res->help_flag)) {
        return;
    }

    char name[17];
    int8_t proto = -1;

    if (bp_cmd_get_positional_string(&ir_setprotocol_def, 1, name, sizeof(name))) {
        proto = ir_protocols_find(name);
        if (proto < 0) {
            printf("Unknown protocol: %s\r\n", name);
        }
    } else {
        // interactive numbered menu
        uint8_t count = ir_protocols_count();
        printf("%sSelect IR protocol:%s\r\n", ui_term_color_info(), ui_term_color_reset());
        for (uint8_t i = 0; i < count; i++) {
            printf(" %d. %s\r\n", i + 1, ir_protocols_get(i)->display);
        }
        printf("Choice 1-%d then ENTER, x to exit > ", count);
        proto = (int8_t)ir_cmd_read_choice(count);
        if (proto < 0) return;
    }

    if (proto < 0) {
        res->error = true;
        return;
    }

    infrared_set_engine_protocol((uint8_t)proto);
    printf("%sProtocol: %s%s\r\n", ui_term_color_info(), ir_protocols_get((uint8_t)proto)->display, ui_term_color_reset());
    printf("Use with 'send <dev> <sub> <cmd>' (see send -h)\r\n");
}
