/**
 * @file ir_jam.c
 * @brief INFRARED mode `jam` command.
 * @details Transmits infrared bursts to evaluate how well an infrared receiver
 *          handles noise. Three modes like the Bit-Pirate original: carrier
 *          (continuous bursts at a fixed frequency), sweep (cycles through
 *          the carrier list) and random (random burst timings, occasionally
 *          hopping the carrier). Density controls how many bursts are sent
 *          per cycle. ENTER stops.
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
#include <stdlib.h>
#include "pico/stdlib.h"
#include "pirate.h"
#include "pirate/bio.h"
#include "mode/infrared-struct.h"
#include "pirate/irio_pio.h"
#include "command_struct.h"
#include "mode/infrared.h"
#include "lib/bp_args/bp_cmd.h"
#include "ui/ui_term.h"
#include "commands/infrared/ir_common.h"
#include "commands/infrared/ir_jam.h"

static const char* const usage[] = {
    "jam",
    "Spam infrared noise:%s jam",
    "Prompts:%s mode (1=carrier, 2=sweep, 3=random), carrier kHz, density (1-20)",
    "Press ENTER to stop",
};

// carrier frequency sweep list, from the Bit-Pirate InfraredService
static const uint16_t jam_carriers_khz[] = { 30, 33, 36, 38, 40, 42, 56 };

const bp_command_def_t ir_jam_def = {
    .name         = "jam",
    .description  = T_IR_CMD_JAM,
    .actions      = NULL,
    .action_count = 0,
    .opts         = NULL,
    .usage        = usage,
    .usage_count  = count_of(usage),
};

// prompt for a number in an inclusive range
static bool prompt_range(const char *label, uint32_t min, uint32_t max, uint32_t def, uint32_t *out) {
    printf("%s (%lu-%lu, ENTER=%lu): ", label, (unsigned long)min, (unsigned long)max, (unsigned long)def);
    char buf[8];
    uint8_t len = 0;
    while (len < (sizeof(buf) - 1)) {
        char c = ir_cmd_wait_key();
        if (c == '\r' || c == '\n') {
            break;
        }
        if (c == 0x7f || c == 0x08) { // backspace
            if (len > 0) {
                len--;
                printf("\b \b");
            }
            continue;
        }
        if (c < '0' || c > '9') {
            continue;
        }
        printf("%c", c);
        buf[len++] = c;
    }
    buf[len] = 0;
    printf("\r\n");
    uint32_t value = (len == 0) ? def : (uint32_t)atoi(buf);
    if (value < min || value > max) {
        printf("Value out of range\r\n");
        return false;
    }
    *out = value;
    return true;
}

void ir_jam_handler(struct command_result *res) {
    if (bp_cmd_help_check(&ir_jam_def, res->help_flag)) {
        return;
    }

    uint32_t mode, khz, density;
    if (!prompt_range("Mode (1=carrier, 2=sweep, 3=random)", 1, 3, 1, &mode)) {
        res->error = true;
        return;
    }
    if (mode == 2) {
        khz = jam_carriers_khz[0];
        printf("Sweeping carriers: 30/33/36/38/40/42/56 kHz\r\n");
    } else if (!prompt_range("Carrier frequency (kHz)", 20, 60, 38, &khz)) {
        res->error = true;
        return;
    }
    if (!prompt_range("Density (bursts per cycle, 1-20)", 1, 20, 10, &density)) {
        res->error = true;
        return;
    }

    printf("Jamming, ENTER to stop\r\n");

    srand((unsigned int)time_us_32());
    ir_cmd_tx_begin(khz * 1000);

    uint32_t sweep_index = 0;
    bool running = true;
    while (running) {
        for (uint32_t b = 0; b < density; b++) {
            uint16_t f_khz = khz;
            uint32_t pairs[16];
            uint16_t count = 16;

            if (mode == 2) { // sweep: cycle the carrier list with a half-period pattern
                f_khz = jam_carriers_khz[sweep_index++ % count_of(jam_carriers_khz)];
                uint16_t half_us = 500 / f_khz;
                if (half_us < 1) {
                    half_us = 1;
                }
                for (uint8_t i = 0; i < count; i++) {
                    pairs[i] = ((uint32_t)half_us << 16) | half_us;
                }
            } else if (mode == 3) { // random: random burst timings, occasional carrier hop
                if ((rand() % 10) < 3) {
                    f_khz = jam_carriers_khz[rand() % count_of(jam_carriers_khz)];
                }
                for (uint8_t i = 0; i < count; i++) {
                    uint16_t mark = 5 + (rand() % 996); // 5-1000us
                    uint16_t space = 5 + (rand() % 996);
                    pairs[i] = ((uint32_t)mark << 16) | space;
                }
            } else { // carrier: continuous bursts at the chosen frequency
                pairs[0] = (0xfffful << 16) | 0; // maximum length mark
                count = 1;
            }

            irio_pio_tx_frame_write((float)(f_khz * 1000), count, pairs);
            busy_wait_us(100 / density + 2); // pacing shrinks as density grows

            char c;
            if (ir_cmd_key_available(&c) && (c == '\r' || c == '\n')) {
                running = false;
                break;
            }
        }
    }

    ir_cmd_tx_end();
    printf("\r\nStopped\r\n");
}
