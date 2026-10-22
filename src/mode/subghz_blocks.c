/* SubGHz shared protocol blocks implementation.
 * Copyright (c) 2026 Bus Pirate contributors. SPDX-License-Identifier: MIT
 */
#include "mode/subghz_blocks.h"

uint64_t subghz_hash_data(const uint8_t *data, size_t bit_count) {
    /* FNV-1a 64-bit over whole bytes then remaining bits MSB-first. */
    uint64_t hash = 14695981039346656037ull;
    if (!data && bit_count != 0) {
        return 0;
    }
    size_t full_bytes = bit_count / 8u;
    for (size_t i = 0; i < full_bytes; i++) {
        hash ^= data[i];
        hash *= 1099511628211ull;
    }
    size_t rem = bit_count % 8u;
    if (rem && data) {
        uint8_t last = data[full_bytes];
        for (size_t i = 0; i < rem; i++) {
            uint8_t bit = (last >> (7u - i)) & 1u;
            hash ^= bit;
            hash *= 1099511628211ull;
        }
    }
    return hash;
}

uint64_t subghz_hash_uint64(uint64_t data, uint32_t bits) {
    uint64_t hash = 14695981039346656037ull;
    if (bits > 64u) {
        bits = 64u;
    }
    for (uint32_t i = 0; i < bits; i++) {
        uint8_t bit = (data >> (bits - 1u - i)) & 1u;
        hash ^= bit;
        hash *= 1099511628211ull;
    }
    return hash;
}

uint8_t subghz_even_parity(uint64_t data, uint32_t bits) {
    uint8_t parity = 0;
    for (uint32_t i = 0; i < bits && i < 64u; i++) {
        parity ^= (data >> i) & 1u;
    }
    return parity;
}
