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
    bool have_pa = false;
    uint8_t pa[SUBGHZ_CUSTOM_PA_SIZE];
    return subghz_parse_custom_preset_full(line, pairs, pair_count, max_pairs, pa, &have_pa);
}

bool subghz_parse_custom_preset_full(const char *line, uint8_t *pairs, size_t *pair_count,
                                     size_t max_pairs, uint8_t *pa_table, bool *have_pa) {
    if (!line || !pairs || !pair_count || !max_pairs) {
        return false;
    }
    const char *colon = strchr(line, ':');
    if (!colon) {
        return false;
    }
    const char *cursor = colon + 1;
    /* Collect raw hex bytes first so the "00 00" register-block terminator
     * (Flipper format: pairs... 00 00 + 8 PA bytes) is not mistaken for a
     * register pair. Cap: max register bytes + terminator + PA table. */
    uint8_t raw[48u * 2u + 2u + SUBGHZ_CUSTOM_PA_SIZE];
    size_t nbytes = 0;
    const size_t raw_cap = sizeof(raw) < (max_pairs * 2u + 2u + SUBGHZ_CUSTOM_PA_SIZE)
                               ? sizeof(raw)
                               : (max_pairs * 2u + 2u + SUBGHZ_CUSTOM_PA_SIZE);
    while (*cursor) {
        while (*cursor == ' ' || *cursor == '\t') {
            cursor++;
        }
        if (*cursor == '\r' || *cursor == '\n' || *cursor == '\0') {
            break;
        }
        if (nbytes >= raw_cap) {
            return false; /* Oversized. */
        }
        uint8_t byte;
        if (!parse_hex_byte(&cursor, &byte)) {
            return false; /* Malformed / odd-length token. */
        }
        raw[nbytes++] = byte;
    }
    if (!nbytes) {
        return false;
    }
    /* Find the first consecutive 00 00 terminator. */
    size_t term = nbytes; /* nbytes = no terminator (legacy pairs-only). */
    for (size_t i = 0; i + 1 < nbytes; i++) {
        if (raw[i] == 0x00u && raw[i + 1u] == 0x00u) {
            term = i;
            break;
        }
    }
    size_t reg_bytes;
    bool pa_present = false;
    if (term < nbytes) {
        reg_bytes = term;
        size_t pa_bytes = nbytes - (term + 2u);
        if (pa_bytes != SUBGHZ_CUSTOM_PA_SIZE) {
            return false; /* Truncated or trailing garbage after terminator. */
        }
        pa_present = true;
    } else {
        reg_bytes = nbytes;
    }
    if (!reg_bytes || (reg_bytes % 2u)) {
        return false;
    }
    size_t npairs = reg_bytes / 2u;
    if (npairs > max_pairs) {
        return false;
    }
    for (size_t i = 0; i < npairs; i++) {
        uint8_t address = raw[i * 2u];
        if (address > 0x2eu && address != 0x3eu) {
            return false; /* Invalid CC1101 config address. */
        }
        pairs[i * 2u] = address;
        pairs[i * 2u + 1u] = raw[i * 2u + 1u];
    }
    *pair_count = npairs;
    if (have_pa) {
        *have_pa = pa_present;
    }
    if (pa_present && pa_table) {
        memcpy(pa_table, raw + term + 2u, SUBGHZ_CUSTOM_PA_SIZE);
    }
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

bool subghz_parse_bit_raw_field(const char *line, uint32_t *bits) {
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
    if (errno == ERANGE || end == colon + 1 || v == 0 || v > SUBGHZ_BINRAW_MAX_BITS) {
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

bool subghz_parse_data_raw_field(const char *line, uint8_t *bytes, size_t *byte_count,
                                 size_t max_bytes) {
    if (!line || !bytes || !byte_count || !max_bytes) {
        return false;
    }
    const char *colon = strchr(line, ':');
    if (!colon) {
        return false;
    }
    const char *cursor = colon + 1;
    size_t n = 0;
    while (*cursor) {
        while (*cursor == ' ' || *cursor == '\t') {
            cursor++;
        }
        if (*cursor == '\r' || *cursor == '\n' || *cursor == '\0') {
            break;
        }
        if (n >= max_bytes) {
            return false; /* Oversized. */
        }
        uint8_t byte;
        if (!parse_hex_byte(&cursor, &byte)) {
            return false; /* Malformed / odd-length token. */
        }
        bytes[n++] = byte;
    }
    if (!n) {
        return false;
    }
    *byte_count = n;
    return true;
}

bool subghz_binraw_expand(uint32_t te_us, const uint8_t *bytes, uint32_t bit_len,
                          int32_t *pulses, uint32_t *pulse_count, uint32_t capacity) {
    if (!bytes || !pulses || !pulse_count || !capacity) {
        return false;
    }
    if (!te_us || te_us < 50u || te_us > 100000u) {
        return false;
    }
    if (!bit_len || bit_len > SUBGHZ_BINRAW_MAX_BITS) {
        return false;
    }
    if (((bit_len + 7u) / 8u) * 8u < bit_len) {
        return false;
    }
    uint32_t idx = *pulse_count;
    bool have_run = false;
    bool run_level = false;
    uint64_t run_te = 0;
    for (uint32_t i = 0; i < bit_len; i++) {
        uint8_t byte = bytes[i / 8u];
        bool bit = ((byte >> (7u - (i % 8u))) & 1u) != 0;
        if (!have_run) {
            run_level = bit;
            run_te = 1;
            have_run = true;
        } else if (bit == run_level) {
            run_te++;
        } else {
            uint64_t dur = run_te * (uint64_t)te_us;
            if (dur > 60000000ull || idx >= capacity) {
                return false;
            }
            pulses[idx++] = run_level ? (int32_t)dur : -(int32_t)dur;
            run_level = bit;
            run_te = 1;
        }
    }
    if (have_run) {
        uint64_t dur = run_te * (uint64_t)te_us;
        if (dur > 60000000ull || idx >= capacity) {
            return false;
        }
        pulses[idx++] = run_level ? (int32_t)dur : -(int32_t)dur;
    }
    *pulse_count = idx;
    return true;
}
