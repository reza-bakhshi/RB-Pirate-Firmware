/* SubGHz keystore implementation.
 * Copyright (c) 2026 Bus Pirate contributors. SPDX-License-Identifier: MIT
 */
#include "mode/subghz_keystore.h"
#include <string.h>
#include <stdio.h>

/* KeeLoq decrypt lives in subghz_protocols.c. */
uint32_t subghz_keeloq_decrypt(uint32_t data, uint64_t key);

void subghz_keystore_init(subghz_keystore_t *ks) {
    if (ks) {
        memset(ks, 0, sizeof(*ks));
    }
}

void subghz_keystore_clear(subghz_keystore_t *ks) {
    if (ks) {
        memset(ks, 0, sizeof(*ks));
    }
}

bool subghz_keystore_add(subghz_keystore_t *ks, const char *name, uint64_t key, uint8_t type) {
    if (!ks || !name || type > SUBGHZ_KEY_TYPE_MAGIC_S3 || ks->count >= SUBGHZ_KEYSTORE_MAX_KEYS) {
        return false;
    }
    subghz_key_t *k = &ks->keys[ks->count++];
    size_t i = 0;
    while (name[i] && i + 1 < sizeof(k->name)) {
        /* Strip characters that would break the text format. */
        char c = name[i];
        k->name[i] = (c == ',' || c == '\r' || c == '\n') ? '_' : c;
        i++;
    }
    k->name[i] = '\0';
    k->key = key;
    k->type = type;
    return true;
}

bool subghz_keystore_parse_line(subghz_keystore_t *ks, const char *line) {
    if (!ks || !line) {
        return false;
    }
    while (*line == ' ' || *line == '\t') {
        line++;
    }
    if (!*line || *line == '#') {
        return false;
    }
    const char *comma1 = strchr(line, ',');
    if (!comma1 || (size_t)(comma1 - line) == 0 ||
        (size_t)(comma1 - line) >= SUBGHZ_KEYSTORE_NAME_MAX) {
        return false;
    }
    char name[SUBGHZ_KEYSTORE_NAME_MAX];
    memcpy(name, line, (size_t)(comma1 - line));
    name[comma1 - line] = '\0';
    /* Trim trailing spaces/tabs so "Foo ,..." and "Foo,..." match. */
    size_t namelen = (size_t)(comma1 - line);
    while (namelen > 0 && (name[namelen - 1] == ' ' || name[namelen - 1] == '\t')) {
        name[--namelen] = '\0';
    }
    if (!namelen) {
        return false;
    }

    const char *hexstart = comma1 + 1;
    const char *comma2 = strchr(hexstart, ',');
    size_t hexlen = comma2 ? (size_t)(comma2 - hexstart) : strcspn(hexstart, "\r\n");
    if (hexlen == 0 || hexlen > 16u) {
        return false;
    }
    uint64_t key = 0;
    for (size_t i = 0; i < hexlen; i++) {
        char c = hexstart[i];
        uint8_t v;
        if (c >= '0' && c <= '9') {
            v = (uint8_t)(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            v = (uint8_t)(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            v = (uint8_t)(c - 'A' + 10);
        } else {
            return false;
        }
        key = (key << 4) | v;
    }

    uint8_t type = SUBGHZ_KEY_TYPE_SIMPLE;
    if (comma2) {
        const char *t = comma2 + 1;
        unsigned long tv = 0;
        while (*t == ' ') {
            t++;
        }
        while (*t >= '0' && *t <= '9') {
            tv = tv * 10u + (unsigned)(*t - '0');
            t++;
        }
        while (*t == ' ' || *t == '\t') {
            t++;
        }
        if (tv > 255u || (*t != '\r' && *t != '\n' && *t != '\0')) {
            return false;
        }
        type = (uint8_t)tv;
    }
    return subghz_keystore_add(ks, name, key, type);
}

bool subghz_keystore_format_line(const subghz_keystore_t *ks, uint32_t index, char *out,
                                 size_t out_size) {
    if (!ks || index >= ks->count || !out || !out_size) {
        return false;
    }
    const subghz_key_t *k = &ks->keys[index];
    int n = snprintf(out, out_size, "%s,%08lX%08lX,%u", k->name,
                     (unsigned long)(k->key >> 32), (unsigned long)k->key, k->type);
    return n > 0 && (size_t)n < out_size;
}

bool subghz_keystore_simple_decrypt(const subghz_keystore_t *ks, uint32_t fix, uint32_t hop,
                                    const char **mfname, uint16_t *cnt) {
    if (!ks) {
        return false;
    }
    uint8_t btn = (uint8_t)(fix >> 28);
    uint16_t end_serial = (uint16_t)(fix & 0xffu);
    for (uint32_t i = 0; i < ks->count; i++) {
        const subghz_key_t *k = &ks->keys[i];
        uint64_t mirror = 0;
        for (unsigned b = 0; b < 8u; b++) {
            mirror = (mirror << 8) | ((k->key >> (b * 8u)) & 0xffu);
        }
        const uint64_t variants[][2] = { { k->key, (uint64_t)k->type },
                                         { mirror, (uint64_t)k->type } };
        for (unsigned v = 0; v < 2u; v++) {
            uint64_t mkey = variants[v][0];
            uint8_t type = (uint8_t)variants[v][1];
            /* Mirrored keys only apply to unknown-type entries. */
            if (v && type != SUBGHZ_KEY_TYPE_UNKNOWN) {
                continue;
            }
            bool try_simple = (type == SUBGHZ_KEY_TYPE_SIMPLE ||
                               type == SUBGHZ_KEY_TYPE_UNKNOWN);
            bool try_normal = (type == SUBGHZ_KEY_TYPE_NORMAL ||
                               type == SUBGHZ_KEY_TYPE_UNKNOWN);
            bool try_secure = (type == SUBGHZ_KEY_TYPE_SECURE ||
                               type == SUBGHZ_KEY_TYPE_UNKNOWN);
            bool try_xor = (type == SUBGHZ_KEY_TYPE_MAGIC_XOR ||
                            type == SUBGHZ_KEY_TYPE_UNKNOWN);
            bool try_s1 = (type == SUBGHZ_KEY_TYPE_MAGIC_S1);
            bool try_s2 = (type == SUBGHZ_KEY_TYPE_MAGIC_S2);
            bool try_s3 = (type == SUBGHZ_KEY_TYPE_MAGIC_S3);
            uint64_t derived[7];
            unsigned nderived = 0;
            if (try_simple) {
                derived[nderived++] = mkey;
            }
            if (try_normal) {
                derived[nderived++] = subghz_keeloq_normal_learning(fix, mkey);
            }
            if (try_secure) {
                derived[nderived++] = subghz_keeloq_secure_learning(fix, 0, mkey);
            }
            if (try_xor) {
                derived[nderived++] = subghz_keeloq_magic_xor(fix, mkey);
            }
            if (try_s1) {
                derived[nderived++] = subghz_keeloq_magic_s1(fix, mkey);
            }
            if (try_s2) {
                derived[nderived++] = subghz_keeloq_magic_s2(fix, mkey);
            }
            if (try_s3) {
                derived[nderived++] = subghz_keeloq_magic_s3(fix & 0xffffffu, mkey);
            }
            for (unsigned d = 0; d < nderived; d++) {
                uint32_t decrypt = subghz_keeloq_decrypt(hop, derived[d]);
                bool disc_ok = (((decrypt >> 16) & 0xffu) == end_serial) ||
                               (((decrypt >> 16) & 0xffu) == 0u);
                bool centurion = (((decrypt >> 16) & 0x3ffu) == 0x1ceu);
                if ((decrypt >> 28) == btn && (disc_ok || centurion)) {
                    if (mfname) {
                        *mfname = k->name;
                    }
                    if (cnt) {
                        *cnt = (uint16_t)(decrypt & 0xffffu);
                    }
                    return true;
                }
            }
        }
    }
    return false;
}

uint64_t subghz_keeloq_normal_learning(uint32_t fix28, uint64_t master) {
    uint32_t d1 = (fix28 & 0x0ffffffful) | 0x20000000ul;
    uint32_t k1 = subghz_keeloq_decrypt(d1, master);
    uint32_t d2 = (fix28 & 0x0ffffffful) | 0x60000000ul;
    uint32_t k2 = subghz_keeloq_decrypt(d2, master);
    return ((uint64_t)k2 << 32) | k1;
}

uint64_t subghz_keeloq_secure_learning(uint32_t fix28, uint32_t seed, uint64_t master) {
    uint32_t k1 = subghz_keeloq_decrypt(fix28 & 0x0ffffffful, master);
    uint32_t k2 = subghz_keeloq_decrypt(seed, master);
    return ((uint64_t)k1 << 32) | k2;
}

uint64_t subghz_keeloq_magic_xor(uint32_t fix28, uint64_t magic) {
    uint64_t d = fix28 & 0x0ffffffful;
    return ((d << 32) | d) ^ magic;
}

uint64_t subghz_keeloq_magic_s1(uint32_t fix28, uint64_t magic) {
    uint32_t d = fix28 & 0x0ffffffful;
    return (magic & 0xffffffffull) | ((uint64_t)d << 40) |
           ((uint64_t)(((d & 0xffu) + ((d >> 8) & 0xffu)) & 0xffu) << 32);
}

uint64_t subghz_keeloq_magic_s2(uint32_t fixbtn32, uint64_t magic) {
    /* Little-endian byte graft of fix into the top half (Flipper ptr version:
     * m[7]=p[0]..m[4]=p[3], i.e. byte-reversed fix on top). */
    uint32_t rev = ((fixbtn32 & 0xffu) << 24) | ((fixbtn32 & 0xff00u) << 8) |
                   ((fixbtn32 & 0xff0000u) >> 8) | ((fixbtn32 >> 24) & 0xffu);
    return (magic & 0xffffffffull) | ((uint64_t)rev << 32);
}

uint64_t subghz_keeloq_magic_s3(uint32_t serial24, uint64_t magic) {
    return (magic & 0xffffffffff000000ull) | (serial24 & 0xffffffull);
}
