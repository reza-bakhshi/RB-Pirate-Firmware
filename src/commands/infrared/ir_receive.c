/**
 * @file ir_receive.c
 * @brief INFRARED mode `receive` command.
 * @details Continuously listens for IR signals, auto-detects the protocol and
 *          displays the decoded device/command values (or the raw aIR packet
 *          when no protocol matches).
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
#include "pirate/ir_protocols.h"
#include "command_struct.h"
#include "mode/infrared.h"
#include "lib/bp_args/bp_cmd.h"
#include "ui/ui_term.h"
#include "commands/infrared/ir_common.h"
#include "commands/infrared/ir_receive.h"

static const char* const usage[] = {
    "receive",
    "Listen and decode IR signals, x to exit:%s receive",
    "Tip:%s use 'irrx' for raw capture, record and retransmit",
};

const bp_command_def_t ir_receive_def = {
    .name         = "receive",
    .description  = T_IR_CMD_RECEIVE,
    .actions      = NULL,
    .action_count = 0,
    .opts         = NULL,
    .usage        = usage,
    .usage_count  = count_of(usage),
};

// print the captured frame as an aIR packet
static void print_air(const uint32_t *pairs, uint16_t count, float mod_freq) {
    uint8_t khz = (mod_freq > 0) ? (uint8_t)((mod_freq / 1000.0f) + 0.5f) : 38;
    printf("  aIR: $%u:", khz);
    for (uint16_t i = 0; i < count; i++) {
        printf("%u,%u,", (unsigned)(pairs[i] >> 16), (unsigned)(pairs[i] & 0xffff));
    }
    printf(";\r\n");
}

void ir_receive_handler(struct command_result *res) {
    if (bp_cmd_help_check(&ir_receive_def, res->help_flag)) {
        return;
    }

    uint8_t rx_pin = infrared_get_rx_pin();
    printf("Listening on BIO%u, x to exit...\r\n", rx_pin);

    ir_cmd_rx_begin(rx_pin);

    while (true) {
        uint32_t pairs[IR_CMD_MAX_PAIRS];
        uint16_t count;
        float mod_freq;

        if (ir_cmd_capture(pairs, &count, &mod_freq)) {
            int8_t proto;
            uint32_t dev, sub, cmd;
            if (ir_protocols_decode(pairs, count, &proto, &dev, &sub, &cmd)) {
                const ir_protocol_def_t *p = ir_protocols_get((uint8_t)proto);
                if (dev == IR_REPEAT_FRAME) {
                    printf("%s%s%s repeat frame\r\n", ui_term_color_info(), p->display, ui_term_color_reset());
                } else {
                    printf("%s%s%s (%ukHz) device 0x%lx (%lu), sub 0x%lx (%lu), command 0x%lx (%lu)\r\n",
                           ui_term_color_info(), p->display, ui_term_color_reset(),
                           (unsigned)(p->carrier_hz / 1000),
                           (unsigned long)dev, (unsigned long)dev,
                           (unsigned long)sub, (unsigned long)sub,
                           (unsigned long)cmd, (unsigned long)cmd);
                }
            } else {
                printf("%sRAW%s %u pairs, no protocol match\r\n", ui_term_color_info(), ui_term_color_reset(), count);
            }
            print_air(pairs, count, mod_freq);
        }

        char c;
        if (ir_cmd_key_available(&c) && c == 'x') {
            break;
        }
    }

    ir_cmd_rx_end();
    printf("Exiting...\r\n");
}
