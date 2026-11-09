/**
 * @file ir_load.c
 * @brief INFRARED mode `load` command.
 * @details Lists the .ir files stored on the Bus Pirate flash file system
 *          (from the Flipper-IRDB database, for example), lets the user pick
 *          a file and a signal, and sends the selected signal.
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
#include "mode/infrared-struct.h"
#include "pirate/irio_pio.h"
#include "commands/infrared/ir_common.h"
#include "commands/infrared/ir_flipper.h"
#include "commands/infrared/ir_load.h"

#define MAX_SIGNALS 48

static const char* const usage[] = {
    "load [file]",
    "Pick a .ir file interactively:%s load",
    "Open a file directly:%s load rem.ir",
    "Get .ir files from:%s https://github.com/Lucaslhm/Flipper-IRDB",
    "Upload files to the flash file system with 'ls'/'edit' or the USB mass storage",
};

static const bp_command_positional_t positionals[] = {
    { "file", "file", T_HELP_IR_LOAD_FILE, false },
    { 0 }
};

const bp_command_def_t ir_load_def = {
    .name         = "load",
    .description  = T_IR_CMD_LOAD,
    .actions      = NULL,
    .action_count = 0,
    .opts         = NULL,
    .positionals      = positionals,
    .positional_count = 1,
    .usage        = usage,
    .usage_count  = count_of(usage),
};

static bool has_ir_extension(const char *name) {
    uint16_t len = strlen(name);
    return (len > 3 && strcasecmp(&name[len - 3], ".ir") == 0);
}

// list .ir files in the current directory, returns the count
static uint16_t list_ir_files(char files[][64], uint16_t max) {
    DIR dir;
    FILINFO fno;
    uint16_t count = 0;
    if (f_opendir(&dir, "") != FR_OK) {
        return 0;
    }
    while (count < max) {
        if (f_readdir(&dir, &fno) != FR_OK || fno.fname[0] == 0) {
            break;
        }
        if (fno.fattrib & AM_DIR) {
            continue;
        }
        if (!has_ir_extension(fno.fname)) {
            continue;
        }
        if (strlen(fno.fname) >= 64) continue; // skip names too long for the list
        strncpy(files[count], fno.fname, 63);
        files[count][63] = 0;
        count++;
    }
    f_closedir(&dir);
    return count;
}

// send one signal, returns false on error
static bool send_signal(ir_signal_t *signal) {
    if (signal->parsed) {
        if (signal->proto < 0) {
            printf("Unsupported protocol, cannot send\r\n");
            return false;
        }
        const ir_protocol_def_t *p = ir_protocols_get((uint8_t)signal->proto);
        printf("%sSending %s: %s (%ukHz)...%s ", ui_term_color_info(), signal->name, p->display,
               (unsigned)(p->carrier_hz / 1000), ui_term_color_reset());
        ir_cmd_tx_begin(p->carrier_hz);
        bool ok = ir_protocols_send((uint8_t)signal->proto, signal->dev, signal->sub, signal->cmd, 0);
        ir_cmd_tx_end();
        printf("%s\r\n", ok ? "done" : "error");
        return ok;
    }
    uint32_t freq_khz = (signal->freq_khz >= 10 && signal->freq_khz <= 100) ? signal->freq_khz : 38;
    printf("%sSending %s: RAW %u pairs (%lukHz)...%s ", ui_term_color_info(), signal->name,
           signal->pairs, (unsigned long)freq_khz, ui_term_color_reset());
    ir_cmd_tx_begin(freq_khz * 1000);
    irio_pio_tx_frame_write((float)(freq_khz * 1000), signal->pairs, signal->data);
    ir_cmd_tx_end();
    printf("done\r\n");
    return true;
}

void ir_load_handler(struct command_result *res) {
    if (bp_cmd_help_check(&ir_load_def, res->help_flag)) {
        return;
    }

    char file[64];
    char chosen[64];
    bool have_file = false;

    if (bp_cmd_get_positional_string(&ir_load_def, 1, chosen, sizeof(chosen))) {
        have_file = true;
    } else {
        static char files[16][64];
        uint16_t count = list_ir_files(files, 16);
        if (count == 0) {
            printf("No .ir files found on the flash file system\r\n");
            printf("Get remote files from https://github.com/Lucaslhm/Flipper-IRDB\r\n");
            return;
        }
        printf("%s.ir files:%s\r\n", ui_term_color_info(), ui_term_color_reset());
        for (uint16_t i = 0; i < count; i++) {
            printf(" %d. %s\r\n", i + 1, files[i]);
        }
        printf("Choice 1-%d then ENTER, x to exit > ", count);
        int16_t choice = ir_cmd_read_choice(count);
        if (choice < 0) return;
        strncpy(chosen, files[choice], sizeof(chosen));
        have_file = true;
    }

    if (!have_file) {
        return;
    }

    FIL file_handle;
    FRESULT fr = f_open(&file_handle, chosen, FA_READ);
    if (fr != FR_OK) {
        printf("Error opening file %s\r\n", chosen);
        res->error = true;
        return;
    }

    static uint32_t offsets[MAX_SIGNALS];
    static char names[MAX_SIGNALS][33];
    uint16_t count = ir_flipper_scan(&file_handle, offsets, names, MAX_SIGNALS);
    if (count == 0) {
        printf("No signals found in %s\r\n", chosen);
        f_close(&file_handle);
        res->error = true;
        return;
    }

    printf("%s: %u signals\r\n", chosen, count);
    while (true) {
        for (uint16_t i = 0; i < count; i++) {
            printf(" %d. %s\r\n", i + 1, names[i]);
        }
        printf("Send signal 1-%d then ENTER, x to exit > ", count);
        int16_t choice = ir_cmd_read_choice(count);
        if (choice < 0) break;
        ir_signal_t signal;
        if (!ir_flipper_read_signal(&file_handle, offsets[choice], &signal)) {
            printf("Error reading signal\r\n");
            continue;
        }
        send_signal(&signal);
    }

    f_close(&file_handle);
    printf("Exiting...\r\n");
}
