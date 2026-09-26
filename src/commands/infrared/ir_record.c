/**
 * @file ir_record.c
 * @brief INFRARED mode `record` command.
 * @details Interactively records IR signals and appends them to a .ir file in
 *          Flipper Zero format (parsed entries when the protocol is detected,
 *          raw entries otherwise). Up to 64 signals per recording session.
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
#include "pirate/bio.h"
#include "pirate/ir_protocols.h"
#include "command_struct.h"
#include "mode/infrared.h"
#include "lib/bp_args/bp_cmd.h"
#include "ui/ui_term.h"
#include "fatfs/ff.h"
#include "pirate/storage.h"
#include "commands/infrared/ir_common.h"
#include "commands/infrared/ir_flipper.h"
#include "commands/infrared/ir_record.h"

#define MAX_RECORDED 64

static const char* const usage[] = {
    "record [file]",
    "Record signals to rem.ir:%s record rem.ir",
    "Files use the Flipper Zero .ir format, 8.3 file names",
    "Parsed entries are written when the protocol is detected, raw otherwise",
};

static const bp_command_positional_t positionals[] = {
    { "file", "file", T_HELP_IR_RECORD_FILE, false },
    { 0 }
};

const bp_command_def_t ir_record_def = {
    .name         = "record",
    .description  = T_IR_CMD_RECORD,
    .actions      = NULL,
    .action_count = 0,
    .opts         = NULL,
    .positionals      = positionals,
    .positional_count = 1,
    .usage        = usage,
    .usage_count  = count_of(usage),
};

void ir_record_handler(struct command_result *res) {
    if (bp_cmd_help_check(&ir_record_def, res->help_flag)) {
        return;
    }

    char file[64];
    char name[33];

    if (!bp_cmd_get_positional_string(&ir_record_def, 1, file, sizeof(file))) {
        strncpy(file, "record.ir", sizeof(file));
    }

    printf("Signal base name (ENTER for %s): ", file);
    uint8_t name_len = 0;
    while (name_len < (sizeof(name) - 1)) {
        char c = ir_cmd_wait_key();
        if (c == '\r' || c == '\n') {
            break;
        }
        if (c == 0x7f || c == 0x08) { // backspace
            if (name_len > 0) {
                name_len--;
                printf("\b \b");
            }
            continue;
        }
        printf("%c", c);
        name[name_len++] = c;
    }
    name[name_len] = 0;
    printf("\r\n");
    if (name_len == 0) { // default to the file name without extension
        strncpy(name, file, sizeof(name) - 1);
        name[sizeof(name) - 1] = 0;
        char *dot = strchr(name, '.');
        if (dot) {
            *dot = 0;
        }
    }

    FIL file_handle;
    FRESULT fr = f_open(&file_handle, file, FA_WRITE | FA_OPEN_ALWAYS);
    if (fr != FR_OK) {
        printf("Error opening file %s for writing\r\n", file);
        res->error = true;
        return;
    }
    f_lseek(&file_handle, f_size(&file_handle)); // append
    if (!ir_flipper_write_header_if_needed(&file_handle)) {
        printf("Error writing file header\r\n");
        res->error = true;
        f_close(&file_handle);
        return;
    }

    uint8_t rx_pin = infrared_get_rx_pin();
    printf("Recording to %s, up to %d signals, x to stop\r\n", file, MAX_RECORDED);
    printf("Point a remote at the RX sensor (BIO%u)\r\n", rx_pin);

    ir_cmd_rx_begin(rx_pin);

    uint16_t recorded = 0;
    while (recorded < MAX_RECORDED) {
        printf("\r\nSignal %u: waiting for IR (x to stop)...\r\n", recorded + 1);

        uint32_t pairs[IR_CMD_MAX_PAIRS];
        uint16_t count;
        float mod_freq;
        while (!ir_cmd_capture(pairs, &count, &mod_freq)) {
            char key;
            if (ir_cmd_key_available(&key) && key == 'x') {
                goto exit_record;
            }
        }

        ir_signal_t signal;
        memset(&signal, 0, sizeof(signal));
        snprintf(signal.name, sizeof(signal.name), "%s_%u", name, recorded);
        signal.freq_khz = (mod_freq > 0) ? (uint32_t)((mod_freq / 1000.0f) + 0.5f) : 38;

        int8_t proto;
        uint32_t dev, sub, cmd;
        if (ir_protocols_decode(pairs, count, &proto, &dev, &sub, &cmd) && dev != IR_REPEAT_FRAME) {
            signal.parsed = true;
            signal.proto = proto;
            signal.dev = dev;
            signal.sub = sub;
            signal.cmd = cmd;
            printf("Captured %s: dev 0x%lx, cmd 0x%lx\r\n",
                   ir_protocols_get((uint8_t)proto)->display,
                   (unsigned long)dev, (unsigned long)cmd);
        } else {
            signal.parsed = false;
            signal.pairs = count;
            memcpy(signal.data, pairs, count * sizeof(uint32_t));
            printf("Captured RAW: %u pairs, %lukHz\r\n", count, (unsigned long)signal.freq_khz);
        }

        printf("[s]ave, [d]iscard, x to stop > ");
        char option;
        while (true) {
            option = ir_cmd_wait_key();
            if (option == 's' || option == 'd' || option == 'x') {
                break;
            }
        }
        printf("%c\r\n", option);
        if (option == 'x') {
            goto exit_record;
        }
        if (option == 's') {
            if (ir_flipper_write_signal(&file_handle, &signal)) {
                recorded++;
                printf("Saved as %s\r\n", signal.name);
            } else {
                printf("Error writing signal\r\n");
                res->error = true;
                goto exit_record;
            }
        }
    }

exit_record:
    ir_cmd_rx_end();
    f_close(&file_handle);
    printf("Recorded %u signal(s) to %s\r\n", recorded, file);
}
