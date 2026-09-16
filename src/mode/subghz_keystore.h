/* SubGHz manufacturer keystore (Flipper subghz_keystore equivalent).
 *
 * Portable C: storage is a memory table; file import/export of text lines
 * is done by the caller (FatFS on device, stdio on host).
 *
 * Line format (Flipper keeloq_mfcodes compatible subset):
 *   Name,0123456789ABCDEF,Type
 * Type: 0 = unknown (tried as simple), 1 = simple learning.
 * Normal/secure/magic learning types are parsed but flagged unsupported.
 *
 * Copyright (c) 2026 Bus Pirate contributors. SPDX-License-Identifier: MIT
 */
#ifndef BP_SUBGHZ_KEYSTORE_H
#define BP_SUBGHZ_KEYSTORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SUBGHZ_KEYSTORE_MAX_KEYS 32
#define SUBGHZ_KEYSTORE_NAME_MAX 24

/* Learning types (Flipper KEELOQ_LEARNING_* numbers). */
#define SUBGHZ_KEY_TYPE_UNKNOWN 0u
#define SUBGHZ_KEY_TYPE_SIMPLE 1u
#define SUBGHZ_KEY_TYPE_NORMAL 2u
#define SUBGHZ_KEY_TYPE_SECURE 3u
#define SUBGHZ_KEY_TYPE_MAGIC_XOR 4u
#define SUBGHZ_KEY_TYPE_MAGIC_S1 5u
#define SUBGHZ_KEY_TYPE_MAGIC_S2 6u
#define SUBGHZ_KEY_TYPE_MAGIC_S3 7u

typedef struct {
    char name[SUBGHZ_KEYSTORE_NAME_MAX];
    uint64_t key;
    uint8_t type;
} subghz_key_t;

typedef struct {
    subghz_key_t keys[SUBGHZ_KEYSTORE_MAX_KEYS];
    uint32_t count;
} subghz_keystore_t;

void subghz_keystore_init(subghz_keystore_t *ks);
void subghz_keystore_clear(subghz_keystore_t *ks);
bool subghz_keystore_add(subghz_keystore_t *ks, const char *name, uint64_t key, uint8_t type);

/* Parse one "Name,HEXKEY,Type" line. Returns false on malformed input. */
bool subghz_keystore_parse_line(subghz_keystore_t *ks, const char *line);

/* Format entry <index> as a text line. Returns false if index invalid. */
bool subghz_keystore_format_line(const subghz_keystore_t *ks, uint32_t index, char *out,
                                 size_t out_size);

/* Simple-learning decrypt attempt (Flipper check_decrypt equivalent).
 * fix = 32-bit fixed part (btn|serial), hop = 32-bit hopping part.
 * Tries the key's learning type (unknown tries simple, byte-mirrored
 * simple, normal, secure, and magic-xor variants like Flipper).
 * On success sets *mfname (points into keystore) and *cnt, returns true. */
bool subghz_keystore_simple_decrypt(const subghz_keystore_t *ks, uint32_t fix, uint32_t hop,
                                    const char **mfname, uint16_t *cnt);

/* Key derivation (Flipper keeloq_common equivalents). */
uint64_t subghz_keeloq_normal_learning(uint32_t fix28, uint64_t master);
uint64_t subghz_keeloq_secure_learning(uint32_t fix28, uint32_t seed, uint64_t master);
uint64_t subghz_keeloq_magic_xor(uint32_t fix28, uint64_t magic);
uint64_t subghz_keeloq_magic_s1(uint32_t fix28, uint64_t magic);
uint64_t subghz_keeloq_magic_s2(uint32_t fixbtn32, uint64_t magic);
uint64_t subghz_keeloq_magic_s3(uint32_t serial24, uint64_t magic);

#ifdef __cplusplus
}
#endif

#endif
