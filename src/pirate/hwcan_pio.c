/**
 * @file hwcan_pio.c
 * @brief PIO-based CAN 2.0B driver using can2040 for Bus Pirate
 */

#include <stdio.h>
#include <string.h>
#include "pirate.h"
#include "system_config.h"
#include "kernel/bp_resource.h"
#include "pirate/bio.h"
#include "pirate/hwcan_pio.h"

// Ring buffer for received frames
static hwcan_frame_t s_rx_fifo[HWCAN_FIFO_DEPTH];
static volatile uint32_t s_rx_head = 0;
static volatile uint32_t s_rx_tail = 0;

// Filter configuration
static uint32_t s_filter_id = 0;
static uint32_t s_filter_mask = 0;
static bool s_filter_enabled = false;

// Statistics
static uint32_t s_rx_count = 0;
static uint32_t s_tx_count = 0;
static uint32_t s_err_count = 0;
static bool s_initialized = false;

#if !defined(BP_HOST_TEST)
#include "pico/stdlib.h"
#include "hardware/irq.h"
#include "hardware/clocks.h"
#include "lib/can2040/can2040.h"

static struct can2040 s_can2040;
static uint8_t s_current_bio_rx = 0;
static uint8_t s_current_bio_tx = 0;

static void hwcan_irq_handler(void) {
    can2040_pio_irq_handler(&s_can2040);
}

static void hwcan_rx_callback(struct can2040 *cd, uint32_t notify, struct can2040_msg *msg) {
    (void)cd;
    if (notify & CAN2040_NOTIFY_RX) {
        if (!msg) return;

        bool is_ext = (msg->id & CAN2040_ID_EFF) != 0;
        bool is_rtr = (msg->id & CAN2040_ID_RTR) != 0;
        uint32_t raw_id = msg->id & ~(CAN2040_ID_EFF | CAN2040_ID_RTR);

        // Apply hardware/software filter if active
        if (s_filter_enabled) {
            if ((raw_id & s_filter_mask) != (s_filter_id & s_filter_mask)) {
                return;
            }
        }

        uint32_t next_head = (s_rx_head + 1) % HWCAN_FIFO_DEPTH;
        if (next_head != s_rx_tail) {
            hwcan_frame_t *slot = &s_rx_fifo[s_rx_head];
            slot->id = raw_id;
            slot->dlc = (msg->dlc > 8) ? 8 : (uint8_t)msg->dlc;
            slot->is_extended = is_ext;
            slot->is_rtr = is_rtr;
            slot->timestamp_us = to_us_since_boot(get_absolute_time());
            memcpy(slot->data, msg->data, slot->dlc);

            s_rx_head = next_head;
            s_rx_count++;
        } else {
            // Buffer overflow
            s_err_count++;
        }
    } else if (notify & CAN2040_NOTIFY_TX) {
        s_tx_count++;
    } else if (notify & CAN2040_NOTIFY_ERROR) {
        s_err_count++;
    }
}

bool hwcan_init(uint32_t bitrate, uint8_t bio_rx, uint8_t bio_tx) {
    if (s_initialized) {
        hwcan_cleanup();
    }

    if (bitrate == 0 || bio_rx >= BIO_MAX_PINS || bio_tx >= BIO_MAX_PINS || bio_rx == bio_tx) {
        return false;
    }

    // Claim 4 state machines on PIO1 for can2040
    uint32_t sm0, sm1, sm2, sm3;
    uint32_t owner = BP_RES_OWNER_MODE_BASE + system_config.mode;
    if (bp_res_claim_unused_pio_sm(1, owner, "can_sync", &sm0) != BP_STATUS_OK ||
        bp_res_claim_unused_pio_sm(1, owner, "can_rx", &sm1) != BP_STATUS_OK ||
        bp_res_claim_unused_pio_sm(1, owner, "can_match", &sm2) != BP_STATUS_OK ||
        bp_res_claim_unused_pio_sm(1, owner, "can_tx", &sm3) != BP_STATUS_OK) {
        bp_res_release_all(owner);
        return false;
    }

    s_current_bio_rx = bio_rx;
    s_current_bio_tx = bio_tx;

    // Buffer direction configuration
    bio_buf_input(bio_rx);
    bio_buf_output(bio_tx);

    s_rx_head = 0;
    s_rx_tail = 0;

    can2040_setup(&s_can2040, 1);
    can2040_callback_config(&s_can2040, hwcan_rx_callback);

    irq_set_exclusive_handler(PIO1_IRQ_0, hwcan_irq_handler);
    irq_set_priority(PIO1_IRQ_0, 0);
    irq_set_enabled(PIO1_IRQ_0, true);

    uint32_t sys_clk = clock_get_hz(clk_sys);
    can2040_start(&s_can2040, sys_clk, bitrate, bio2bufiopin[bio_rx], bio2bufiopin[bio_tx]);

    s_initialized = true;
    return true;
}

void hwcan_cleanup(void) {
    if (!s_initialized) return;

    irq_set_enabled(PIO1_IRQ_0, false);
    irq_remove_handler(PIO1_IRQ_0, hwcan_irq_handler);

    can2040_stop(&s_can2040);

    uint32_t owner = BP_RES_OWNER_MODE_BASE + system_config.mode;
    bp_res_release_all(owner);

    bio_init();

    s_initialized = false;
}

bool hwcan_tx_frame(uint32_t id, uint8_t dlc, const uint8_t *data, bool is_extended, bool is_rtr) {
    if (!s_initialized) return false;

    struct can2040_msg msg;
    memset(&msg, 0, sizeof(msg));

    msg.id = id;
    if (is_extended) msg.id |= CAN2040_ID_EFF;
    if (is_rtr)      msg.id |= CAN2040_ID_RTR;

    msg.dlc = (dlc > 8) ? 8 : dlc;
    if (data && !is_rtr) {
        memcpy(msg.data, data, msg.dlc);
    }

    int rc = can2040_transmit(&s_can2040, &msg);
    return (rc == 0);
}

#else
// Host test simulated mock implementation

bool hwcan_init(uint32_t bitrate, uint8_t bio_rx, uint8_t bio_tx) {
    if (bitrate == 0 || bio_rx >= BIO_MAX_PINS || bio_tx >= BIO_MAX_PINS || bio_rx == bio_tx) {
        return false;
    }
    s_rx_head = 0;
    s_rx_tail = 0;
    s_rx_count = 0;
    s_tx_count = 0;
    s_err_count = 0;
    s_initialized = true;
    return true;
}

void hwcan_cleanup(void) {
    s_initialized = false;
}

bool hwcan_tx_frame(uint32_t id, uint8_t dlc, const uint8_t *data, bool is_extended, bool is_rtr) {
    if (!s_initialized) return false;

    s_tx_count++;

    // Simulated loopback to RX FIFO for unit tests
    if (s_filter_enabled) {
        if ((id & s_filter_mask) != (s_filter_id & s_filter_mask)) {
            return true;
        }
    }

    uint32_t next_head = (s_rx_head + 1) % HWCAN_FIFO_DEPTH;
    if (next_head != s_rx_tail) {
        hwcan_frame_t *slot = &s_rx_fifo[s_rx_head];
        slot->id = id;
        slot->dlc = (dlc > 8) ? 8 : dlc;
        slot->is_extended = is_extended;
        slot->is_rtr = is_rtr;
        slot->timestamp_us = 1000;
        if (data && !is_rtr) {
            memcpy(slot->data, data, slot->dlc);
        } else {
            memset(slot->data, 0, sizeof(slot->data));
        }
        s_rx_head = next_head;
        s_rx_count++;
    } else {
        s_err_count++;
    }

    return true;
}

#endif // !BP_HOST_TEST

bool hwcan_pop_frame(hwcan_frame_t *out_frame) {
    if (!out_frame) return false;

    if (s_rx_head == s_rx_tail) {
        return false;
    }

    *out_frame = s_rx_fifo[s_rx_tail];
    s_rx_tail = (s_rx_tail + 1) % HWCAN_FIFO_DEPTH;
    return true;
}

uint32_t hwcan_rx_available(void) {
    if (s_rx_head >= s_rx_tail) {
        return s_rx_head - s_rx_tail;
    }
    return HWCAN_FIFO_DEPTH - (s_rx_tail - s_rx_head);
}

void hwcan_set_filter(uint32_t id, uint32_t mask, bool enabled) {
    s_filter_id = id;
    s_filter_mask = mask;
    s_filter_enabled = enabled;
}

void hwcan_get_filter(uint32_t *id, uint32_t *mask, bool *enabled) {
    if (id) *id = s_filter_id;
    if (mask) *mask = s_filter_mask;
    if (enabled) *enabled = s_filter_enabled;
}

void hwcan_get_stats(uint32_t *rx_count, uint32_t *tx_count, uint32_t *error_count) {
    if (rx_count) *rx_count = s_rx_count;
    if (tx_count) *tx_count = s_tx_count;
    if (error_count) *error_count = s_err_count;
}

void hwcan_reset_stats(void) {
    s_rx_count = 0;
    s_tx_count = 0;
    s_err_count = 0;
}
