/* SubGHz protocol registry: decoder/encoder objects a la Flipper lib/subghz.
 * Copyright (c) 2026 Bus Pirate contributors. SPDX-License-Identifier: MIT
 */
#ifndef BP_SUBGHZ_PROTOCOLS_H
#define BP_SUBGHZ_PROTOCOLS_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "mode/subghz_blocks.h"

#define SUBGHZ_PROTOCOL_MAX_BITS 128
#define SUBGHZ_PROTOCOL_NAME_MAX 32

typedef enum {
    SUBGHZ_PROTOCOL_OK,
    SUBGHZ_PROTOCOL_SYNC,
    SUBGHZ_PROTOCOL_DONE,
    SUBGHZ_PROTOCOL_ERROR,
} subghz_protocol_status_t;

typedef struct {
    const char *name;
    const char *description;
    uint32_t frequency_default_khz;
    uint32_t te_min_us;
    uint32_t te_max_us;
    uint32_t bit_min;   /* Minimum valid payload bits (0 = any). */
    uint32_t bit_max;   /* Maximum valid payload bits (0 = any). */
    bool rolling;       /* Rolling/dynamic code: decode/display only, no TX. */
} subghz_protocol_info_t;

typedef struct subghz_protocol_decoder subghz_protocol_decoder_t;
typedef struct subghz_protocol_encoder subghz_protocol_encoder_t;

/* Decoder interface. */
typedef subghz_protocol_status_t (*subghz_decoder_feed_fn)(
    subghz_protocol_decoder_t *decoder, bool level, uint32_t duration_us);

typedef void (*subghz_decoder_reset_fn)(subghz_protocol_decoder_t *decoder);

typedef const char *(*subghz_decoder_get_data_fn)(
    subghz_protocol_decoder_t *decoder, char *buffer, size_t buffer_size);

/* Detailed human-readable rendering (Flipper get_string equivalent). */
typedef const char *(*subghz_decoder_get_string_fn)(
    subghz_protocol_decoder_t *decoder, char *buffer, size_t buffer_size);

typedef struct {
    subghz_decoder_feed_fn feed;
    subghz_decoder_reset_fn reset;
    subghz_decoder_get_data_fn get_data;
    subghz_decoder_get_string_fn get_string;
} subghz_decoder_ops_t;

struct subghz_protocol_decoder {
    const subghz_decoder_ops_t *ops;
    const subghz_protocol_info_t *info;
    uint64_t decoded_data;
    uint32_t decoded_bits;
    uint32_t te_last;
    uint32_t te_us;
    uint64_t hash;
    uint32_t state;
    void *priv;
};

/* Encoder interface (Flipper encoder equivalent). */
typedef subghz_protocol_status_t (*subghz_encoder_feed_fn)(
    subghz_protocol_encoder_t *encoder, uint64_t data, uint32_t bits);

typedef bool (*subghz_encoder_yield_fn)(
    subghz_protocol_encoder_t *encoder, bool *level, uint32_t *duration_us);

typedef void (*subghz_encoder_reset_fn)(subghz_protocol_encoder_t *encoder);

typedef struct {
    subghz_encoder_feed_fn feed;
    subghz_encoder_yield_fn yield;
    subghz_encoder_reset_fn reset;
} subghz_encoder_ops_t;

struct subghz_protocol_encoder {
    const subghz_encoder_ops_t *ops;
    const subghz_protocol_info_t *info;
    subghz_upload_t upload;
    uint32_t te_us;
    void *priv;
};

/* RcSwitch encoder private config (proto 1, 2, or 11). */
typedef struct {
    uint32_t te_us;
    uint8_t proto;
} rcswitch_enc_priv_t;

bool subghz_rcswitch_set_proto(subghz_protocol_encoder_t *encoder, uint8_t proto);

/* Protocol registry. */
typedef struct {
    const subghz_protocol_info_t *info;
    const subghz_decoder_ops_t *decoder_ops;
    const subghz_encoder_ops_t *encoder_ops;
    size_t decoder_size;
    size_t encoder_size;
} subghz_protocol_entry_t;

const subghz_protocol_entry_t *subghz_protocol_find(const char *name);
const subghz_protocol_entry_t *subghz_protocol_find_by_index(uint32_t index);
uint32_t subghz_protocol_count(void);
uint32_t subghz_protocol_count_static(void); /* Non-rolling protocols (TX capable). */

subghz_protocol_decoder_t *subghz_decoder_alloc(const subghz_protocol_entry_t *entry);
void subghz_decoder_free(subghz_protocol_decoder_t *decoder);
subghz_protocol_status_t subghz_decoder_feed(subghz_protocol_decoder_t *decoder, bool level, uint32_t duration_us);
void subghz_decoder_reset(subghz_protocol_decoder_t *decoder);
const char *subghz_decoder_get_data(subghz_protocol_decoder_t *decoder, char *buffer, size_t size);
const char *subghz_decoder_get_string(subghz_protocol_decoder_t *decoder, char *buffer, size_t size);

subghz_protocol_encoder_t *subghz_encoder_alloc(const subghz_protocol_entry_t *entry);
void subghz_encoder_free(subghz_protocol_encoder_t *encoder);
subghz_protocol_status_t subghz_encoder_feed(subghz_protocol_encoder_t *encoder, uint64_t data, uint32_t bits);
bool subghz_encoder_yield(subghz_protocol_encoder_t *encoder, bool *level, uint32_t *duration_us);
void subghz_encoder_reset(subghz_protocol_encoder_t *encoder);

void subghz_protocols_init(void);

/* KeeLoq block cipher (public domain algorithm) for keystore decrypt. */
uint32_t subghz_keeloq_decrypt(uint32_t data, uint64_t key);
uint32_t subghz_keeloq_encrypt(uint32_t data, uint64_t key);

/* Keystore hook consumed by the KeeLoq decoder (set by the mode layer). */
const void *subghz_keeloq_keystore_get(void);
void subghz_keeloq_keystore_set(const void *ks);

/* Last KeeLoq analysis (manufacturer / counter) for file saving. */
const char *subghz_keeloq_last_manufacture(void);
uint16_t subghz_keeloq_last_counter(void);

#endif
