/* SubGHz file field parsers implementation.
 * Copyright (c) 2026 Bus Pirate contributors. SPDX-License-Identifier: MIT
 */
#include "mode/subghz_file.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static bool valid_band_khz(uint32_t khz) {
    return (khz >= 300000u && khz <= 348000u) || (khz >= 387000u && khz <= 464000u) ||
           (khz >= 779000u && khz <= 928000u);
}

bool subghz_field_value_equals(const char *line, size_t prefix_length, const char *expected) {
    if (!line || !expected) {
        return false;
    }
    if (prefix_length > strlen(line)) {
        return false;
    }
    const char *value = line + prefix_length;
    while (*value == ' ' || *value == '\t') {
        value++;
    }
    size_t length = strcspn(value, "\r\n");
    return strlen(expected) == length && memcmp(value, expected, length) == 0;
}

bool subghz_parse_frequency_khz(const char *line, uint32_t *frequency_khz) {
    if (!line || !frequency_khz) {
        return false;
    }
    const char *colon = strchr(line, ':');
    if (!colon) {
        return false;
    }
    const char *value = colon + 1;
    char *end = NULL;
    errno = 0;
    unsigned long frequency_hz = strtoul(value, &end, 10);
    if (errno == ERANGE || end == value || frequency_hz > UINT32_MAX || frequency_hz % 1000u) {
        return false;
    }
    while (*end == ' ' || *end == '\t') {
        end++;
    }
    if (*end != '\r' && *end != '\n' && *end != '\0') {
        return false;
    }
    uint32_t khz = (uint32_t)(frequency_hz / 1000u);
    if (!valid_band_khz(khz)) {
        return false;
    }
    *frequency_khz = khz;
    return true;
}

bool subghz_parse_bit_field(const char *line, uint32_t *bits) {
    if (!line || !bits) {
        return false;
    }
    const char *colon = strchr(line, ':');
    if (!colon) {
        return false;
    }
    char *end = NULL;
    errno = 0;
    unsigned long v = strtoul(colon + 1, &end, 10);
    if (errno == ERANGE || end == colon + 1 || v == 0 || v > 128u) {
        return false;
    }
    while (*end == ' ' || *end == '\t') {
        end++;
    }
    if (*end != '\r' && *end != '\n' && *end != '\0') {
        return false;
    }
    *bits = (uint32_t)v;
    return true;
}

static int hex_nibble(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static bool parse_hex_byte(const char **cursor, uint8_t *out) {
    if (!cursor || !*cursor || !out) return false;
    while (**cursor == ' ' || **cursor == '\t') (*cursor)++;
    if (!**cursor || **cursor == '\r' || **cursor == '\n') return false;
    int hi = hex_nibble(*(*cursor)++);
    if (!**cursor || **cursor == '\r' || **cursor == '\n') return false;
    int lo = hex_nibble(*(*cursor)++);
    if (hi < 0 || lo < 0) return false;
    *out = (uint8_t)((hi << 4) | lo);
    return true;
}

bool subghz_parse_key_field(const char *line, uint64_t *key, uint32_t *key_bits) {
    if (!line || !key) {
        return false;
    }
    const char *colon = strchr(line, ':');
    if (!colon) {
        return false;
    }
    const char *cursor = colon + 1;
    uint64_t value = 0;
    uint32_t count = 0;
    while (*cursor) {
        while (*cursor == ' ' || *cursor == '\t') {
            cursor++;
        }
        if (*cursor == '\r' || *cursor == '\n' || *cursor == '\0') {
            break;
        }
        int hi = hex_nibble(*cursor++);
        int lo = hex_nibble(*cursor++);
        if (hi < 0 || lo < 0) {
            return false;
        }
        if (count >= 8u) {
            return false;
        }
        value = (value << 8) | (uint8_t)((hi << 4) | lo);
        count++;
    }
    if (!count) {
        return false;
    }
    *key = value;
    if (key_bits) {
        *key_bits = count * 8u;
    }
    return true;
}

bool subghz_parse_te_field(const char *line, uint32_t *te_us) {
    if (!line || !te_us) {
        return false;
    }
    const char *colon = strchr(line, ':');
    if (!colon) {
        return false;
    }
    char *end = NULL;
    errno = 0;
    unsigned long v = strtoul(colon + 1, &end, 10);
    if (errno == ERANGE || end == colon + 1 || v < 50u || v > 100000u) {
        return false;
    }
    while (*end == ' ' || *end == '\t') {
        end++;
    }
    if (*end != '\r' && *end != '\n' && *end != '\0') {
        return false;
    }
    *te_us = (uint32_t)v;
    return true;
}

bool subghz_parse_custom_preset(const char *line, uint8_t *pairs, size_t *pair_count,
                                size_t max_pairs) {
    if (!line || !pairs || !pair_count || !max_pairs) {
        return false;
    }
    const char *colon = strchr(line, ':');
    if (!colon) {
        return false;
    }
    const char *cursor = colon + 1;
    size_t nbytes = 0;
    while (*cursor) {
        while (*cursor == ' ' || *cursor == '\t') {
            cursor++;
        }
        if (*cursor == '\r' || *cursor == '\n' || *cursor == '\0') {
            break;
        }
        if (nbytes >= max_pairs * 2u) {
            return false;
        }
        uint8_t address;
        if (!parse_hex_byte(&cursor, &address)) return false;
        pairs[nbytes++] = address;
        if (address > 0x2eu && address != 0x3eu) {
            return false;
        }
        /* The value byte is consumed below; address/value pairs are the only
         * form accepted by the CC1101 custom-register loader. */
        uint8_t value;
        if (!parse_hex_byte(&cursor, &value)) return false;
        pairs[nbytes++] = value;
    }
    if (!nbytes || (nbytes % 2u)) {
        return false;
    }
    *pair_count = nbytes / 2u;
    return true;
}

bool subghz_parse_manufacture(const char *line, char *out, size_t out_size) {
    if (!line || !out || !out_size) {
        return false;
    }
    const char *colon = strchr(line, ':');
    if (!colon) {
        return false;
    }
    const char *v = colon + 1;
    while (*v == ' ' || *v == '\t') {
        v++;
    }
    size_t n = strcspn(v, "\r\n");
    if (!n || n >= out_size) {
        return false;
    }
    memcpy(out, v, n);
    out[n] = '\0';
    return true;
}
