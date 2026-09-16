/* SubGHz shared protocol blocks (Flipper lib/subghz/blocks equivalent).
 *
 * Portable C: no Pico SDK dependencies so host unit tests can compile it.
 * Copyright (c) 2026 Bus Pirate contributors. Modified by Bus Pirate contributors.
 * SPDX-License-Identifier: MIT
 */
#ifndef BP_SUBGHZ_BLOCKS_H
#define BP_SUBGHZ_BLOCKS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Absolute-difference duration match with tolerance (Flipper DURATION_DIFF). */
static inline bool subghz_duration_match(uint32_t value, uint32_t base, uint32_t delta) {
    uint32_t diff = value > base ? value - base : base - value;
    return diff <= delta;
}

/* Round a duration to a TE multiple, minimum 1. Returns 0 if te_us is 0. */
static inline uint32_t subghz_te_count(uint32_t duration_us, uint32_t te_us) {
    if (!te_us) {
        return 0;
    }
    uint32_t count = duration_us / te_us;
    uint32_t rem = duration_us % te_us;
    if (rem >= (te_us + 1u) / 2u && count != UINT32_MAX) {
        count++;
    }
    return count ? count : 1u;
}

/* 64-bit FNV-1a hash over data bits (MSB first), Flipper hash_data equivalent. */
uint64_t subghz_hash_data(const uint8_t *data, size_t bit_count);

/* XOR (even) parity over the low <bits> bits. */
uint8_t subghz_even_parity(uint64_t data, uint32_t bits);

/* Hash a raw integer payload of <bits> bits (MSB first). */
uint64_t subghz_hash_uint64(uint64_t data, uint32_t bits);

/* Simple MSB-first bit buffer. Storage holds 64 bits; the counter saturates
 * at 64 so bits and data can never disagree. No in-tree protocol exceeds
 * 64 bits of payload (KeeLoq frame max). */
typedef struct {
    uint64_t data;
    uint32_t bits;
} subghz_bit_buffer_t;

static inline void subghz_bits_reset(subghz_bit_buffer_t *b) {
    if (b) {
        b->data = 0;
        b->bits = 0;
    }
}

static inline void subghz_bits_add(subghz_bit_buffer_t *b, uint8_t bit) {
    if (!b) {
        return;
    }
    if (b->bits >= 64u) {
        return;
    }
    b->data = (b->data << 1) | (bit & 1u);
    b->bits++;
}

/* Upload item: one (level, duration) pair for encoders / TX. */
typedef struct {
    bool level;
    uint32_t duration_us;
} subghz_upload_item_t;

#define SUBGHZ_UPLOAD_MAX 512u

typedef struct {
    subghz_upload_item_t items[SUBGHZ_UPLOAD_MAX];
    size_t count;
    size_t pos;
} subghz_upload_t;

static inline void subghz_upload_reset(subghz_upload_t *u) {
    u->count = 0;
    u->pos = 0;
}

static inline bool subghz_upload_add(subghz_upload_t *u, bool level, uint32_t duration_us) {
    if (u->count >= SUBGHZ_UPLOAD_MAX) {
        return false;
    }
    u->items[u->count].level = level;
    u->items[u->count].duration_us = duration_us;
    u->count++;
    return true;
}

static inline bool subghz_upload_yield(subghz_upload_t *u, bool *level, uint32_t *duration_us) {
    if (u->pos >= u->count) {
        return false;
    }
    *level = u->items[u->pos].level;
    *duration_us = u->items[u->pos].duration_us;
    u->pos++;
    return true;
}

#ifdef __cplusplus
}
#endif

#endif
