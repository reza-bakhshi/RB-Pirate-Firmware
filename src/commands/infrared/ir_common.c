/**
 * @file ir_common.c
 * @brief Shared helpers for INFRARED mode commands.
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
#include "command_struct.h"
#include "mode/infrared-struct.h"
#include "pirate/irio_pio.h"
#include "mode/infrared.h"
#include "usb_rx.h"
#include "commands/infrared/ir_common.h"

static bool tx_active;
static bool rx_active;
static uint8_t rx_pin_used;

bool ir_cmd_tx_begin(uint32_t mod_freq_hz) {
    if (!tx_active && !rx_active) {
        infrared_cleanup_temp(); // tear down the mode PIO programs
    }
    if (!tx_active) {
        irio_pio_tx_init(bio2bufiopin[infrared_get_tx_pin()], mod_freq_hz);
        tx_active = true;
    }
    return true;
}

void ir_cmd_tx_end(void) {
    if (!tx_active) {
        return;
    }
    irio_pio_tx_deinit(bio2bufiopin[infrared_get_tx_pin()]);
    tx_active = false;
    if (!rx_active) {
        infrared_setup_resume(); // restore the mode PIO programs
    }
}

bool ir_cmd_rx_begin(uint8_t rx_bio) {
    if (!tx_active && !rx_active) {
        infrared_cleanup_temp(); // tear down the mode PIO programs
    }
    if (!rx_active) {
        irio_pio_rx_init(bio2bufiopin[rx_bio]);
        rx_active = true;
        rx_pin_used = rx_bio;
    }
    return true;
}

void ir_cmd_rx_end(void) {
    if (!rx_active) {
        return;
    }
    irio_pio_rx_deinit(bio2bufiopin[rx_pin_used]);
    rx_active = false;
    if (!tx_active) {
        infrared_setup_resume(); // restore the mode PIO programs
    }
}

bool ir_cmd_hw_active(void) {
    return tx_active || rx_active;
}

bool ir_cmd_capture(uint32_t *pairs, uint16_t *count, float *mod_freq) {
    uint16_t us;
    uint16_t pairs_read;
    uint32_t buffer[IR_CMD_MAX_PAIRS];
    if (!irio_pio_rx_frame_buf(mod_freq, &us, &pairs_read, buffer, IR_CMD_MAX_PAIRS)) {
        return false;
    }
    if (pairs_read > IR_CMD_MAX_PAIRS) {
        pairs_read = IR_CMD_MAX_PAIRS;
    }
    memcpy(pairs, buffer, pairs_read * sizeof(uint32_t));
    *count = pairs_read;
    return true;
}

bool ir_cmd_key_available(char *c) {
    return rx_fifo_try_get(c);
}

char ir_cmd_wait_key(void) {
    char c;
    while (!rx_fifo_try_get(&c)) {
        tight_loop_contents();
    }
    return c;
}

int16_t ir_cmd_read_choice(uint16_t max) {
    uint16_t choice = 0;
    bool have_digit = false;
    while (true) {
        char c = ir_cmd_wait_key();
        if (c == 'x' || c == 'X') {
            printf("%c\r\n", c);
            return -1;
        }
        if (c == '\r' || c == '\n') {
            printf("\r\n");
            return (have_digit && choice >= 1 && choice <= max) ? (int16_t)(choice - 1) : -1;
        }
        if (c == '\b' || c == 0x7f) {
            printf("\b \b");
            choice /= 10;
            have_digit = choice != 0;
        } else if (c >= '0' && c <= '9' && choice < 1000) {
            printf("%c", c);
            choice = choice * 10 + (c - '0');
            have_digit = true;
        }
    }
}
