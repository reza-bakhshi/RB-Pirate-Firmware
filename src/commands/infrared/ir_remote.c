/**
 * @file ir_remote.c
 * @brief INFRARED mode `remote` command.
 * @details Interactive shell that blasts common keys (VOL, MUTE, POWER...)
 *          across every supported protocol with known key tables. Each key
 *          press sends the key with all matching protocols, so at least one
 *          usually lands on the target device. x to stop.
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
#include "commands/infrared/ir_remote.h"

static const char* const usage[] = {
    "remote",
    "Open the universal remote shell:%s remote",
    "Keys:%s p=power u=vol+ d=vol- m=mute c=ch+ C=ch- o=ok 0-9=digits x=exit",
};

const bp_command_def_t ir_remote_def = {
    .name         = "remote",
    .description  = T_IR_CMD_REMOTE,
    .actions      = NULL,
    .action_count = 0,
    .opts         = NULL,
    .usage        = usage,
    .usage_count  = count_of(usage),
};

// remote keys
typedef enum {
    REMOTE_POWER = 0,
    REMOTE_VOLUP,
    REMOTE_VOLDN,
    REMOTE_MUTE,
    REMOTE_CHUP,
    REMOTE_CHDN,
    REMOTE_OK,
    REMOTE_PLAY,
    REMOTE_PAUSE,
    REMOTE_DIGITS, // first of the 10 digit keys
} remote_key_t;

// key table: protocol index, dev, sub, cmd, key
static const struct _remote_code {
    uint8_t proto;
    uint32_t dev;
    uint32_t sub;
    uint32_t cmd;
    uint8_t key;
} remote_codes[] = {
    // RC5 TV (device 0) - codes verified against Bit-Pirate IRDB tables
    { 6, 0, 0, 12, REMOTE_POWER },
    { 6, 0, 0, 16, REMOTE_VOLUP },
    { 6, 0, 0, 17, REMOTE_VOLDN },
    { 6, 0, 0, 13, REMOTE_MUTE },
    { 6, 0, 0, 32, REMOTE_CHUP },
    { 6, 0, 0, 33, REMOTE_CHDN },
    { 6, 0, 0, 43, REMOTE_PLAY },
    { 6, 0, 0, 48, REMOTE_PAUSE },
    { 6, 0, 0, 0,  REMOTE_DIGITS + 0 },
    { 6, 0, 0, 1,  REMOTE_DIGITS + 1 },
    { 6, 0, 0, 2,  REMOTE_DIGITS + 2 },
    { 6, 0, 0, 3,  REMOTE_DIGITS + 3 },
    { 6, 0, 0, 4,  REMOTE_DIGITS + 4 },
    { 6, 0, 0, 5,  REMOTE_DIGITS + 5 },
    { 6, 0, 0, 6,  REMOTE_DIGITS + 6 },
    { 6, 0, 0, 7,  REMOTE_DIGITS + 7 },
    { 6, 0, 0, 8,  REMOTE_DIGITS + 8 },
    { 6, 0, 0, 9,  REMOTE_DIGITS + 9 },
    // RC6 TV (device 0, mode 0)
    { 7, 0, 0, 12, REMOTE_POWER },
    { 7, 0, 0, 16, REMOTE_VOLUP },
    { 7, 0, 0, 17, REMOTE_VOLDN },
    { 7, 0, 0, 13, REMOTE_MUTE },
    // SONY TV (device 1, SIRC12) - codes verified against Bit-Pirate IRDB tables
    { 3, 1, 0, 21, REMOTE_POWER },
    { 3, 1, 0, 18, REMOTE_VOLUP },
    { 3, 1, 0, 19, REMOTE_VOLDN },
    { 3, 1, 0, 20, REMOTE_MUTE },
    { 3, 1, 0, 16, REMOTE_CHUP },
    { 3, 1, 0, 17, REMOTE_CHDN },
    { 3, 1, 0, 73, REMOTE_PLAY },
    { 3, 1, 0, 63, REMOTE_PAUSE },
    { 3, 1, 0, 0,  REMOTE_DIGITS + 0 },
    { 3, 1, 0, 1,  REMOTE_DIGITS + 1 },
    { 3, 1, 0, 2,  REMOTE_DIGITS + 2 },
    { 3, 1, 0, 3,  REMOTE_DIGITS + 3 },
    { 3, 1, 0, 4,  REMOTE_DIGITS + 4 },
    { 3, 1, 0, 5,  REMOTE_DIGITS + 5 },
    { 3, 1, 0, 6,  REMOTE_DIGITS + 6 },
    { 3, 1, 0, 7,  REMOTE_DIGITS + 7 },
    { 3, 1, 0, 8,  REMOTE_DIGITS + 8 },
    { 3, 1, 0, 9,  REMOTE_DIGITS + 9 },
    // SAMSUNG TV (device 7) - codes verified against Bit-Pirate IRDB tables
    { 1, 7, 0, 0x02, REMOTE_POWER },
    { 1, 7, 0, 0x07, REMOTE_VOLUP },
    { 1, 7, 0, 0x0b, REMOTE_VOLDN },
    { 1, 7, 0, 0x0f, REMOTE_MUTE },
    { 1, 7, 0, 0x12, REMOTE_CHUP },
    { 1, 7, 0, 0x13, REMOTE_CHDN },
    { 1, 7, 0, 71,   REMOTE_PLAY },
    { 1, 7, 0, 74,   REMOTE_PAUSE },
    // LG TV (device 4)
    { 2, 4, 0, 8, REMOTE_POWER },
    { 2, 4, 0, 2, REMOTE_VOLUP },
    { 2, 4, 0, 3, REMOTE_VOLDN },
    { 2, 4, 0, 9, REMOTE_MUTE },
    { 2, 4, 0, 0, REMOTE_CHUP },
    { 2, 4, 0, 1, REMOTE_CHDN },
    // PANASONIC TV (device 8, Bit-Pirate off code)
    { 5, 8, 0, 61, REMOTE_POWER },
    // DISH satellite (device 1)
    { 8, 1, 0, 0x10, REMOTE_POWER },
};

static void print_menu(void) {
    printf("p=power u=vol+ d=vol- m=mute c=ch+ C=ch- o=ok P=play a=pause 0-9=digits x=exit > ");
}

void ir_remote_handler(struct command_result *res) {
    if (bp_cmd_help_check(&ir_remote_def, res->help_flag)) {
        return;
    }

    printf("Universal remote shell\r\n");
    printf("Each key is sent with every protocol that has a known code\r\n");
    printf("p=power u=vol+ d=vol- m=mute c=ch+ C=ch- o=ok P=play a=pause 0-9=digits x=exit\r\n");

    ir_cmd_tx_begin(38000); // highest carrier used below (per-frame freq is set by the engine)

    while (true) {
        printf("%s", ui_term_color_prompt());
        print_menu();
        printf("%s", ui_term_color_reset());
        char c = ir_cmd_wait_key();
        printf("%c\r\n", c);

        uint8_t key;
        if (c == 'x') {
            break;
        } else if (c == 'p') {
            key = REMOTE_POWER;
        } else if (c == 'u') {
            key = REMOTE_VOLUP;
        } else if (c == 'd') {
            key = REMOTE_VOLDN;
        } else if (c == 'm') {
            key = REMOTE_MUTE;
        } else if (c == 'c') {
            key = REMOTE_CHUP;
        } else if (c == 'C') {
            key = REMOTE_CHDN;
        } else if (c == 'o') {
            key = REMOTE_OK;
        } else if (c == 'P') {
            key = REMOTE_PLAY;
        } else if (c == 'a') {
            key = REMOTE_PAUSE;
        } else if (c >= '0' && c <= '9') {
            key = REMOTE_DIGITS + (c - '0');
        } else {
            printf("Unknown key\r\n");
            continue;
        }

        for (uint8_t i = 0; i < count_of(remote_codes); i++) {
            if (remote_codes[i].key != key) {
                continue;
            }
            const ir_protocol_def_t *p = ir_protocols_get(remote_codes[i].proto);
            printf(" %s: dev 0x%lx, cmd 0x%lx ", p->display,
                   (unsigned long)remote_codes[i].dev, (unsigned long)remote_codes[i].cmd);
            bool ok = ir_protocols_send(remote_codes[i].proto, remote_codes[i].dev, remote_codes[i].sub, remote_codes[i].cmd, 2);
            printf("%s\r\n", ok ? "sent" : "error");
            busy_wait_us(150000); // 150ms between protocols
        }
    }

    ir_cmd_tx_end();
    printf("Exiting...\r\n");
}
