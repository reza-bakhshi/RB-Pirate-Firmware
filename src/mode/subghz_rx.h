/* SubGHz multi-protocol receiver (Flipper receiver.c equivalent).
 *
 * Feeds one (level, duration) stream into every registered decoder,
 * reports each new decode once and suppresses consecutive duplicates
 * via (protocol, hash) tracking.
 *
 * Portable C: no Pico SDK dependencies.
 *
 * Copyright (c) 2026 Bus Pirate contributors. SPDX-License-Identifier: MIT
 */
#ifndef BP_SUBGHZ_RX_H
#define BP_SUBGHZ_RX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "mode/subghz_protocols.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SUBGHZ_RX_MAX_DECODERS 32u

typedef void (*subghz_rx_callback_t)(const subghz_protocol_entry_t *entry,
                                     subghz_protocol_decoder_t *decoder, uint32_t repeats,
                                     void *context);

typedef struct {
    subghz_protocol_decoder_t *decoder;
    const subghz_protocol_entry_t *entry;
    uint64_t last_hash;
    uint32_t repeats;
    bool has_result;
} subghz_rx_slot_t;

typedef struct {
    subghz_rx_slot_t slots[SUBGHZ_RX_MAX_DECODERS];
    uint32_t slot_count;
} subghz_rx_t;

/* Allocate and reset a decoder for every registered protocol. */
bool subghz_rx_init(subghz_rx_t *rx);
void subghz_rx_reset(subghz_rx_t *rx);
void subghz_rx_free(subghz_rx_t *rx);

/* Feed one pulse. Invokes cb for each newly decoded frame. */
void subghz_rx_feed(subghz_rx_t *rx, bool level, uint32_t duration_us, subghz_rx_callback_t cb,
                    void *context);

#ifdef __cplusplus
}
#endif

#endif
