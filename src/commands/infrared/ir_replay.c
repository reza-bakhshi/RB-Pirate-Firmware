/**
 * @file ir_replay.c
 * @brief INFRARED mode `replay` command.
 * @details Records up to 64 IR frames (x to stop) and replays them with the
 *          original inter-frame delays. The replay runs until ENTER is
 *          pressed or the optional count is exhausted.
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
#include "mode/infrared-struct.h"
#include "pirate/ir_protocols.h"
#include "pirate/irio_pio.h"
#include "command_struct.h"
#include "mode/infrared.h"
#include "lib/bp_args/bp_cmd.h"
#include "ui/ui_term.h"
#include "commands/infrared/ir_common.h"
#include "commands/infrared/ir_replay.h"

#define MAX_REPLAY_FRAMES 64


static const char* const usage[] = {
    "replay [count]",
    "Record frames then replay until ENTER:%s replay",
    "Replay 10 times:%s replay 10",
};

static const bp_val_constraint_t count_range = {
    .type = BP_VAL_UINT32,
    .u = { .min = 1, .max = 10000, .def = 0 }, // 0 = until ENTER
};

static const bp_command_positional_t positionals[] = {
    { "count", "count", T_HELP_IR_REPLAY_COUNT, false, &count_range },
    { 0 }
};

const bp_command_def_t ir_replay_def = {
    .name         = "replay",
    .description  = T_IR_CMD_REPLAY,
    .actions      = NULL,
    .action_count = 0,
    .opts         = NULL,
    .positionals      = positionals,
    .positional_count = 1,
    .usage        = usage,
    .usage_count  = count_of(usage),
};

// recorded frames and their inter-frame delays
static struct _replay_frame {
    uint32_t data[IR_CMD_MAX_REPLAY_PAIRS];
    uint16_t pairs;
    uint32_t freq_khz;
    uint32_t delay_ms; // delay after this frame, before the next one
} replay_frames[MAX_REPLAY_FRAMES];

void ir_replay_handler(struct command_result *res) {
    if (bp_cmd_help_check(&ir_replay_def, res->help_flag)) {
        return;
    }

    uint32_t count = 0; // 0 = replay until ENTER
    if (bp_cmd_get_positional_uint32(&ir_replay_def, 1, &count)) {
        if (count < 1 || count > 10000) {
            printf("Replay count must be 1-10000\r\n");
            res->error = true;
            return;
        }
    }

    uint8_t rx_pin = infrared_get_rx_pin();
    printf("Record up to %d IR frames, x to stop and replay\r\n", MAX_REPLAY_FRAMES);
    printf("Point the remote at the RX sensor (BIO%u)\r\n", rx_pin);

    ir_cmd_rx_begin(rx_pin);

    uint16_t frames = 0;
    uint64_t last_capture_us = 0;
    while (frames < MAX_REPLAY_FRAMES) {
        printf("\rFrame %u/%d (x to stop)", frames + 1, MAX_REPLAY_FRAMES);

        uint32_t pairs[IR_CMD_MAX_PAIRS];
        uint16_t pairs_count;
        float mod_freq;
        while (!ir_cmd_capture(pairs, &pairs_count, &mod_freq)) {
            char key;
            if (ir_cmd_key_available(&key) && key == 'x') {
                goto start_replay;
            }
        }

        struct _replay_frame *f = &replay_frames[frames];
        f->pairs = (pairs_count > IR_CMD_MAX_REPLAY_PAIRS) ? IR_CMD_MAX_REPLAY_PAIRS : pairs_count;
        memcpy(f->data, pairs, f->pairs * sizeof(uint32_t));
        f->freq_khz = (mod_freq > 0) ? (uint32_t)((mod_freq / 1000.0f) + 0.5f) : 38;
        if (pairs_count > IR_CMD_MAX_REPLAY_PAIRS) {
            printf("\r\nFrame too long, truncated to %d pairs\r\n", IR_CMD_MAX_REPLAY_PAIRS);
        }

        // measure the gap since the previous captured frame
        uint64_t now = time_us_64();
        if (frames > 0) {
            uint64_t gap_us = now - last_capture_us;
            // subtract the previous frame duration (approximate)
            uint32_t prev_frame_us = 0;
            for (uint16_t p = 0; p < replay_frames[frames - 1].pairs; p++) {
                prev_frame_us += (replay_frames[frames - 1].data[p] >> 16) + (replay_frames[frames - 1].data[p] & 0xffff);
            }
            f->delay_ms = (gap_us > prev_frame_us) ? (uint32_t)((gap_us - prev_frame_us) / 1000) : 0;
            if (f->delay_ms > 5000) {
                f->delay_ms = 5000; // cap long pauses
            }
        } else {
            f->delay_ms = 0;
        }
        last_capture_us = now;
        frames++;
    }

start_replay:
    ir_cmd_rx_end();
    printf("\r\n");

    if (frames == 0) {
        printf("Nothing recorded\r\n");
        return;
    }

    printf("Recorded %u frames, ENTER to start the replay, x to cancel\r\n", frames);
    char c;
    do {
        c = ir_cmd_wait_key();
    } while (c != '\r' && c != '\n' && c != 'x');
    if (c == 'x') {
        printf("Cancelled\r\n");
        return;
    }
    // drain any leftover keypresses (e.g. the LF after the starting CR)
    while (ir_cmd_key_available(&c)) {
    }

    ir_cmd_tx_begin(38000);
    bool running = true;
    for (uint32_t pass = 0; running && (count == 0 || pass < count); pass++) {
        printf("Replay pass %lu\r\n", (unsigned long)(pass + 1));
        for (uint16_t i = 0; i < frames; i++) {
            irio_pio_tx_frame_write((float)(replay_frames[i].freq_khz * 1000),
                                    replay_frames[i].pairs, replay_frames[i].data);
            busy_wait_us((uint64_t)replay_frames[i].delay_ms * 1000);
            char key;
            if (count == 0 && ir_cmd_key_available(&key) && (key == '\r' || key == '\n')) {
                running = false;
                break;
            }
        }
    }
    ir_cmd_tx_end();
    printf("Replay finished\r\n");
}
