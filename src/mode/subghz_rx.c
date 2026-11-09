/* SubGHz receiver implementation.
 * Copyright (c) 2026 Bus Pirate contributors. SPDX-License-Identifier: MIT
 */
#include "mode/subghz_rx.h"
#include <stdlib.h>
#include <string.h>

bool subghz_rx_init(subghz_rx_t *rx) {
    if (!rx) {
        return false;
    }
    memset(rx, 0, sizeof(*rx));
    uint32_t count = subghz_protocol_count();
    if (count > SUBGHZ_RX_MAX_DECODERS) {
        return false;
    }
    for (uint32_t i = 0; i < count; i++) {
        const subghz_protocol_entry_t *entry = subghz_protocol_find_by_index(i);
        subghz_protocol_decoder_t *dec = subghz_decoder_alloc(entry);
        if (!dec) {
            subghz_rx_free(rx);
            return false;
        }
        rx->slots[rx->slot_count].decoder = dec;
        rx->slots[rx->slot_count].entry = entry;
        rx->slot_count++;
    }
    return rx->slot_count > 0;
}

void subghz_rx_reset(subghz_rx_t *rx) {
    if (!rx) {
        return;
    }
    for (uint32_t i = 0; i < rx->slot_count; i++) {
        subghz_decoder_reset(rx->slots[i].decoder);
        rx->slots[i].last_hash = 0;
        rx->slots[i].repeats = 0;
        rx->slots[i].has_result = false;
    }
}

void subghz_rx_free(subghz_rx_t *rx) {
    if (!rx) {
        return;
    }
    for (uint32_t i = 0; i < rx->slot_count; i++) {
        subghz_decoder_free(rx->slots[i].decoder);
        rx->slots[i].decoder = NULL;
    }
    rx->slot_count = 0;
}

void subghz_rx_feed(subghz_rx_t *rx, bool level, uint32_t duration_us, subghz_rx_callback_t cb,
                    void *context) {
    if (!rx) {
        return;
    }
    for (uint32_t i = 0; i < rx->slot_count; i++) {
        subghz_rx_slot_t *slot = &rx->slots[i];
        subghz_protocol_status_t st = subghz_decoder_feed(slot->decoder, level, duration_us);
        if (st == SUBGHZ_PROTOCOL_DONE) {
            uint64_t hash = slot->decoder->hash;
            if (!hash) {
                hash = subghz_hash_uint64(slot->decoder->decoded_data,
                                          slot->decoder->decoded_bits);
            }
            if (slot->has_result && hash == slot->last_hash) {
                slot->repeats++;
            } else {
                slot->last_hash = hash;
                slot->repeats = 1;
                slot->has_result = true;
                if (cb) {
                    cb(slot->entry, slot->decoder, slot->repeats, context);
                }
            }
            subghz_decoder_reset(slot->decoder);
        }
    }
}
