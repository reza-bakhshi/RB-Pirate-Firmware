/* SubGHz protocol library: decoders + encoders for static OOK protocols
 * plus decode-only Keeloq dynamic support.
 *
 * Timing constants verified against Flipper Zero firmware
 * lib/subghz/protocols (dev branch): princeton, nice_flo, came,
 * holtek_ht12x, linear, intertechno_v3, gate_tx, dooya, smc5326,
 * ansonic, keeloq.
 *
 * Copyright (c) 2026 Bus Pirate contributors. SPDX-License-Identifier: MIT
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "mode/subghz_protocols.h"
#include "mode/subghz_keystore.h"

#ifndef COUNT_OF
#define COUNT_OF(x) (sizeof(x) / sizeof((x)[0]))
#endif

/* ============================================================
 * Common helpers.
 * ============================================================ */

static void decoder_add_bit(subghz_protocol_decoder_t *d, uint8_t bit) {
    if (d->decoded_bits >= 64u) {
        return; /* Storage is 64 bits; keep counter and data consistent. */
    }
    d->decoded_data = (d->decoded_data << 1) | (bit & 1u);
    d->decoded_bits++;
}

/* Reverse bit order of the low <bits> bits (Flipper reverse_key). */
static uint64_t reverse_key(uint64_t data, uint32_t bits) {
    uint64_t out = 0;
    for (uint32_t i = 0; i < bits && i < 64u; i++) {
        out = (out << 1) | ((data >> i) & 1ull);
    }
    return out;
}

static void decoder_finish(subghz_protocol_decoder_t *d) {
    d->hash = subghz_hash_uint64(d->decoded_data, d->decoded_bits);
}

/* Generic encoder reset/yield over the upload buffer. */
static void encoder_reset_generic(subghz_protocol_encoder_t *e) {
    e->upload.pos = 0;
}

static bool encoder_yield_generic(
    subghz_protocol_encoder_t *e, bool *level, uint32_t *duration_us) {
    return subghz_upload_yield(&e->upload, level, duration_us);
}

/* ============================================================
 * KeeLoq cipher (public domain, cf. Wikipedia "KeeLoq").
 * Used for simple-learning decrypt with user-supplied keys.
 * ============================================================ */

#define KEELOQ_NLF 0x3a5c742eul

static uint32_t keeloq_bit(uint32_t x, unsigned n) {
    return (x >> n) & 1u;
}

static uint32_t keeloq_g5(uint32_t x, unsigned a, unsigned b, unsigned c, unsigned d, unsigned e) {
    return keeloq_bit(x, a) | (keeloq_bit(x, b) << 1) | (keeloq_bit(x, c) << 2) |
           (keeloq_bit(x, d) << 3) | (keeloq_bit(x, e) << 4);
}

uint32_t subghz_keeloq_decrypt(uint32_t data, uint64_t key) {
    uint32_t x = data;
    for (unsigned r = 0; r < 528u; r++) {
        uint32_t kl = (uint32_t)(key >> ((15u - r) & 63u)) & 1u;
        x = (x << 1) ^ keeloq_bit(x, 31) ^ keeloq_bit(x, 15) ^ kl ^
            keeloq_bit(KEELOQ_NLF, keeloq_g5(x, 0, 8, 19, 25, 30));
    }
    return x;
}

uint32_t subghz_keeloq_encrypt(uint32_t data, uint64_t key) {
    uint32_t x = data;
    for (unsigned r = 0; r < 528u; r++) {
        uint32_t kl = (uint32_t)(key >> (r & 63u)) & 1u;
        x = (x >> 1) ^
            ((keeloq_bit(x, 0) ^ keeloq_bit(x, 16) ^ kl ^
              keeloq_bit(KEELOQ_NLF, keeloq_g5(x, 1, 9, 20, 26, 31)))
             << 31);
    }
    return x;
}

/* ============================================================
 * PRINCETON (PT2260/PT2262/EV1527): 24-bit, TE 100-1000us auto.
 * 1 = 3TE high + TE low, 0 = TE high + 3TE low, sync = TE + 31TE.
 * ============================================================ */

typedef struct {
    uint32_t te_us;
    uint32_t low_te;
    bool have_high;
    uint32_t high_te;
    bool decoding;
} princeton_priv_t;

static subghz_protocol_status_t princeton_feed(
    subghz_protocol_decoder_t *d, bool level, uint32_t dur) {
    princeton_priv_t *p = d->priv;
    if (level) {
        p->high_te = dur;
        p->have_high = true;
        if (!p->te_us && dur >= 100u && dur <= 1000u) {
            p->te_us = dur; /* First guess; refined below. */
        }
        return SUBGHZ_PROTOCOL_SYNC;
    }
    /* Low pulse completes a high+low pair. */
    if (!p->have_high) {
        return SUBGHZ_PROTOCOL_SYNC;
    }
    p->have_high = false;
    if (!p->decoding) {
        /* Sync: short high (100-1500us) + very long low (>= 20x high). */
        if (p->high_te >= 100u && p->high_te <= 1500u && dur >= p->high_te * 20u &&
            dur <= 100000u) {
            p->decoding = true;
            p->te_us = p->high_te;
            d->decoded_data = 0;
            d->decoded_bits = 0;
            d->te_us = p->te_us;
        }
        return SUBGHZ_PROTOCOL_SYNC;
    }
    if (!p->te_us) {
        return SUBGHZ_PROTOCOL_SYNC;
    }
    uint32_t h = subghz_te_count(p->high_te, p->te_us);
    uint32_t l = subghz_te_count(dur, p->te_us);
    if (h <= 2u && l >= 20u) {
        /* Guard/sync between frames: frame complete. */
        if (d->decoded_bits >= 20u) {
            p->decoding = false;
            decoder_finish(d);
            return SUBGHZ_PROTOCOL_DONE;
        }
        d->decoded_data = 0;
        d->decoded_bits = 0;
        if (p->high_te >= 100u && p->high_te <= 1500u) {
            p->te_us = p->high_te;
            d->te_us = p->te_us;
        }
        return SUBGHZ_PROTOCOL_SYNC;
    }
    if (h >= 2u && h <= 4u && l == 1u) {
        decoder_add_bit(d, 1);
    } else if (h == 1u && l >= 2u && l <= 4u) {
        decoder_add_bit(d, 0);
    } else {
        p->decoding = false;
        d->decoded_data = 0;
        d->decoded_bits = 0;
    }
    return SUBGHZ_PROTOCOL_SYNC;
}

static void princeton_reset(subghz_protocol_decoder_t *d) {
    memset(d->priv, 0, sizeof(princeton_priv_t));
    d->decoded_data = 0;
    d->decoded_bits = 0;
    d->te_us = 0;
}

static const char *princeton_get_data(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->decoded_bits < 20u) {
        return NULL;
    }
    uint32_t v = (uint32_t)(d->decoded_data & 0xfffffful);
    snprintf(b, s, "Princeton %ubit: 0x%06lx", d->decoded_bits, (unsigned long)v);
    return b;
}

static const char *princeton_get_string(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->decoded_bits < 20u) {
        return NULL;
    }
    uint32_t v = (uint32_t)(d->decoded_data & 0xfffffful);
    snprintf(b, s, "Princeton %ubit\r\nKey:0x%06lX\r\nSn:0x%05lX Btn:%lX\r\nTE:%luus",
             d->decoded_bits, (unsigned long)v, (unsigned long)(v >> 4), (unsigned long)(v & 0xf),
             (unsigned long)d->te_us);
    return b;
}

static const subghz_decoder_ops_t princeton_decoder_ops = {
    .feed = princeton_feed, .reset = princeton_reset, .get_data = princeton_get_data,
    .get_string = princeton_get_string,
};

typedef struct {
    uint32_t te_us;
} ook_enc_priv_t;

static subghz_protocol_status_t princeton_encode(
    subghz_protocol_encoder_t *e, uint64_t data, uint32_t bits) {
    ook_enc_priv_t *p = e->priv;
    uint32_t te = p->te_us ? p->te_us : 350u;
    if (!bits || bits > 64u) {
        return SUBGHZ_PROTOCOL_ERROR;
    }
    subghz_upload_reset(&e->upload);
    e->te_us = te;
    subghz_upload_add(&e->upload, true, te);
    subghz_upload_add(&e->upload, false, te * 31u);
    for (int i = (int)bits - 1; i >= 0; i--) {
        if ((data >> i) & 1ull) {
            subghz_upload_add(&e->upload, true, te * 3u);
            subghz_upload_add(&e->upload, false, te);
        } else {
            subghz_upload_add(&e->upload, true, te);
            subghz_upload_add(&e->upload, false, te * 3u);
        }
    }
    subghz_upload_add(&e->upload, true, te);
    subghz_upload_add(&e->upload, false, te * 31u);
    return SUBGHZ_PROTOCOL_DONE;
}

static const subghz_encoder_ops_t princeton_encoder_ops = {
    .feed = princeton_encode, .yield = encoder_yield_generic, .reset = encoder_reset_generic,
};

/* ============================================================
 * Shared state machine for header + H/L-pair PWM protocols:
 * GateTX, Dooya, Ansonic, Nice FLO, CAME, Holtek, Linear.
 * Bit shape per protocol configured in the const table.
 * ============================================================ */

typedef struct {
    uint8_t step;
    uint32_t te_last;
    uint32_t te_us; /* Measured/sampled TE for TE-carrying protocols. */
} pwm_priv_t;

/* Step ids shared by the PWM-family decoders. */
enum { PWM_RESET = 0, PWM_HEADER, PWM_HDRGAP, PWM_DATA, PWM_CHECK };

typedef struct {
    uint32_t te_short;
    uint32_t te_long;
    uint32_t te_delta;
    uint32_t header_mult;   /* Header length in te_short units (0 = none). */
    uint32_t header_abs_us; /* Absolute header length (0 = use header_mult). */
    uint32_t header_delta_mult;
    bool header_is_high;    /* Header is a HIGH pulse (Hormann-style). */
    uint32_t gap_mult;      /* Low gap after high header, te_short units. */
    uint32_t start_high;    /* Start-bit high in te_short units (0 = none). */
    uint32_t start_low;     /* Start-bit low in te_long units (0 = none). */
    uint32_t end_gap_mult;  /* Low gap that terminates a frame (0 = none). */
    uint32_t end_high_mult; /* High pulse that terminates a frame (0 = none). */
    uint32_t stop_high_mult; /* Trailing stop HIGH emitted on TX (0 = none). */
    uint32_t min_bits;
    uint32_t max_bits;
    uint8_t h_for_one;      /* High TE count that encodes bit 1. */
    uint8_t l_for_one;      /* Low TE count that encodes bit 1. */
    uint8_t h_for_zero;
    uint8_t l_for_zero;
    bool bit_from_high;     /* True: bit determined by high pulse (Dooya/SMC style). */
    bool invert_data;       /* True: swap 0/1 after decode (protocol polarity). */
} pwm_const_t;

static uint32_t pwm_header_len(const pwm_const_t *c) {
    if (c->header_abs_us) {
        return c->header_abs_us;
    }
    return c->te_short * c->header_mult;
}

/* Feed one (level, dur) sample into a PWM-family decoder. */
static subghz_protocol_status_t pwm_feed(
    subghz_protocol_decoder_t *d, const pwm_const_t *c, bool level, uint32_t dur) {
    pwm_priv_t *p = d->priv;
    switch (p->step) {
    case PWM_RESET:
        if (!level && c->header_mult &&
            subghz_duration_match(dur, pwm_header_len(c),
                                  c->te_delta * c->header_delta_mult)) {
            p->step = PWM_HEADER;
        }
        break;
    case PWM_HDRGAP:
        /* Low gap after a high header (Hormann-style). */
        if (!level && c->gap_mult &&
            subghz_duration_match(dur, c->te_short * c->gap_mult, c->te_delta * 3u)) {
            p->step = PWM_DATA;
            d->decoded_data = 0;
            d->decoded_bits = 0;
            d->te_us = c->te_short;
        } else {
            p->step = PWM_RESET;
        }
        break;
    case PWM_HEADER:
        if (c->start_high) {
            if (level && subghz_duration_match(dur, c->te_short * c->start_high,
                                               c->te_delta * 3u)) {
                if (!c->start_low) {
                    p->step = PWM_DATA;
                    d->decoded_data = 0;
                    d->decoded_bits = 0;
                } else {
                    p->step = PWM_CHECK; /* Reuse CHECK as "expect start low". */
                    p->te_last = 0xfffffffful;
                }
            } else if (!level && p->te_last == 0xfffffffful &&
                       subghz_duration_match(dur, c->te_long * c->start_low,
                                             c->te_delta * 3u)) {
                /* Dooya-style H(short*13)+L(long*2) start handled by caller. */
                p->step = PWM_DATA;
                d->decoded_data = 0;
                d->decoded_bits = 0;
            } else {
                p->step = PWM_RESET;
            }
        } else {
            p->step = PWM_RESET;
        }
        break;
    case PWM_DATA:
        if (c->bit_from_high) {
            if (level) {
                /* A long HIGH here terminates the frame (Hormann-style
                 * trailing stop); normal data highs fall through. */
                if (c->end_high_mult && dur >= c->te_short * c->end_high_mult) {
                    p->step = PWM_RESET;
                    if (d->decoded_bits >= c->min_bits &&
                        (!c->max_bits || d->decoded_bits <= c->max_bits)) {
                        decoder_finish(d);
                        return SUBGHZ_PROTOCOL_DONE;
                    }
                    d->decoded_data = 0;
                    d->decoded_bits = 0;
                    break;
                }
                p->te_last = dur;
                p->step = PWM_CHECK;
            } else {
                /* A low here follows the last bit low: a long one is the
                 * next frame header (or gap) terminating this frame. The
                 * last bit was already counted in CHECK. */
                uint32_t hdr_thr = c->te_short * (c->header_mult > 8u ? c->header_mult - 8u
                                                                      : c->header_mult);
                if (c->header_mult && dur >= hdr_thr) {
                    p->step = PWM_RESET;
                    if (d->decoded_bits >= c->min_bits &&
                        (!c->max_bits || d->decoded_bits <= c->max_bits)) {
                        decoder_finish(d);
                        return SUBGHZ_PROTOCOL_DONE;
                    }
                    d->decoded_data = 0;
                    d->decoded_bits = 0;
                    break;
                }
                p->step = PWM_RESET;
            }
        } else {
            if (!level) {
                if (c->end_gap_mult && dur >= c->te_short * c->end_gap_mult) {
                    p->step = PWM_HEADER;
                    if (d->decoded_bits >= c->min_bits) {
                        decoder_finish(d);
                        return SUBGHZ_PROTOCOL_DONE;
                    }
                    d->decoded_data = 0;
                    d->decoded_bits = 0;
                    break;
                }
                p->te_last = dur;
                p->step = PWM_CHECK;
            } else {
                p->step = PWM_RESET;
            }
        }
        break;
    case PWM_CHECK:
        if (p->te_last == 0xfffffffful) {
            /* Dooya-style start: high part seen, expect the long start low. */
            if (!level && c->start_low &&
                subghz_duration_match(dur, c->te_long * c->start_low, c->te_delta * 3u)) {
                p->step = PWM_DATA;
                d->decoded_data = 0;
                d->decoded_bits = 0;
                d->te_us = c->te_short;
            } else {
                p->step = PWM_RESET;
            }
            break;
        }
        if (c->bit_from_high) {
            if (!level) {
                /* Next-frame header merged with the last bit low: the low is
                 * far longer than any data pulse. Recover the last bit from
                 * the preceding high pulse alone. */
                uint32_t hdr_thr = c->te_short * (c->header_mult > 8u ? c->header_mult - 8u
                                                                      : c->header_mult);
                if (c->header_mult && dur >= hdr_thr) {
                    /* Fixed-length frame with a stop bit already counted:
                     * finish without adding (Bett/Clemsa-style). */
                    if (c->max_bits && d->decoded_bits >= c->max_bits) {
                        p->step = PWM_RESET;
                        if (d->decoded_bits == c->max_bits &&
                            d->decoded_bits >= c->min_bits) {
                            decoder_finish(d);
                            return SUBGHZ_PROTOCOL_DONE;
                        }
                        d->decoded_data = 0;
                        d->decoded_bits = 0;
                        break;
                    }
                    uint32_t h = subghz_te_count(p->te_last, c->te_short);
                    if (h == c->h_for_one) {
                        decoder_add_bit(d, c->invert_data ? 0 : 1);
                    } else if (h == c->h_for_zero) {
                        decoder_add_bit(d, c->invert_data ? 1 : 0);
                    } else {
                        p->step = PWM_RESET;
                        break;
                    }
                    p->step = PWM_RESET;
                    if (d->decoded_bits >= c->min_bits) {
                        decoder_finish(d);
                        return SUBGHZ_PROTOCOL_DONE;
                    }
                    d->decoded_data = 0;
                    d->decoded_bits = 0;
                    break;
                }
                uint32_t h = subghz_te_count(p->te_last, c->te_short);
                uint32_t l = subghz_te_count(dur, c->te_short);
                bool is_one = (h == c->h_for_one && l == c->l_for_one);
                bool is_zero = (h == c->h_for_zero && l == c->l_for_zero);
                if (c->end_gap_mult && dur >= c->te_long * 4u) {
                    /* Dooya last-bit + gap termination. */
                    if (subghz_duration_match(p->te_last, c->te_short, c->te_delta)) {
                        decoder_add_bit(d, c->invert_data ? 1 : 0);
                    } else if (subghz_duration_match(p->te_last, c->te_long,
                                                     c->te_delta * 2u)) {
                        decoder_add_bit(d, c->invert_data ? 0 : 1);
                    } else {
                        p->step = PWM_RESET;
                        break;
                    }
                    p->step = PWM_HEADER;
                    if (d->decoded_bits >= c->min_bits) {
                        decoder_finish(d);
                        return SUBGHZ_PROTOCOL_DONE;
                    }
                    d->decoded_data = 0;
                    d->decoded_bits = 0;
                    break;
                }
                if (is_one) {
                    decoder_add_bit(d, c->invert_data ? 0 : 1);
                    p->step = PWM_DATA;
                } else if (is_zero) {
                    decoder_add_bit(d, c->invert_data ? 1 : 0);
                    p->step = PWM_DATA;
                } else {
                    p->step = PWM_RESET;
                }
            } else {
                p->step = PWM_RESET;
            }
        } else {
            if (level) {
                uint32_t l = subghz_te_count(p->te_last, c->te_short);
                uint32_t h = subghz_te_count(dur, c->te_short);
                bool is_one = (l == c->l_for_one && h == c->h_for_one);
                bool is_zero = (l == c->l_for_zero && h == c->h_for_zero);
                if (is_one) {
                    decoder_add_bit(d, c->invert_data ? 0 : 1);
                    p->step = PWM_DATA;
                } else if (is_zero) {
                    decoder_add_bit(d, c->invert_data ? 1 : 0);
                    p->step = PWM_DATA;
                } else {
                    p->step = PWM_RESET;
                }
                if (d->decoded_bits > c->max_bits) {
                    p->step = PWM_RESET;
                    d->decoded_data = 0;
                    d->decoded_bits = 0;
                }
            } else {
                p->step = PWM_RESET;
            }
        }
        break;
    default:
        p->step = PWM_RESET;
        break;
    }
    return SUBGHZ_PROTOCOL_SYNC;
}

static void pwm_reset(subghz_protocol_decoder_t *d) {
    memset(d->priv, 0, sizeof(pwm_priv_t));
    d->decoded_data = 0;
    d->decoded_bits = 0;
    d->hash = 0;
}

/* Generic upload builder for H/L-pair PWM protocols. */
static subghz_protocol_status_t pwm_encode(
    subghz_protocol_encoder_t *e, const pwm_const_t *c, uint32_t fixed_te, uint64_t data,
    uint32_t bits, bool header_low_first) {
    ook_enc_priv_t *p = e->priv;
    uint32_t te = fixed_te ? fixed_te : p->te_us;
    (void)header_low_first;
    if (!te) {
        te = c->te_short;
    }
    if (!bits || bits > c->max_bits || bits < c->min_bits) {
        return SUBGHZ_PROTOCOL_ERROR;
    }
    subghz_upload_reset(&e->upload);
    e->te_us = te;
    if (c->header_mult) {
        uint32_t hdr = c->header_abs_us ? c->header_abs_us : te * c->header_mult;
        if (c->header_is_high) {
            subghz_upload_add(&e->upload, true, hdr);
            subghz_upload_add(&e->upload, false, te * (c->gap_mult ? c->gap_mult : 1u));
        } else if (header_low_first) {
            subghz_upload_add(&e->upload, false, hdr);
        } else {
            subghz_upload_add(&e->upload, true, hdr);
        }
    }
    if (c->start_high) {
        subghz_upload_add(&e->upload, true, te * c->start_high);
        if (c->start_low) {
            subghz_upload_add(&e->upload, false,
                              c->bit_from_high ? c->te_long * c->start_low
                                               : te * c->start_low);
        }
    }
    for (int i = (int)bits - 1; i >= 0; i--) {
        bool one = ((data >> i) & 1ull) != 0;
        if (c->invert_data) {
            one = !one;
        }
        if (c->bit_from_high) {
            subghz_upload_add(&e->upload, true, te * (one ? c->h_for_one : c->h_for_zero));
            subghz_upload_add(&e->upload, false, te * (one ? c->l_for_one : c->l_for_zero));
        } else {
            subghz_upload_add(&e->upload, false, te * (one ? c->l_for_one : c->l_for_zero));
            subghz_upload_add(&e->upload, true, te * (one ? c->h_for_one : c->h_for_zero));
        }
    }
    if (c->end_gap_mult) {
        /* NOTE: no trailing low gap is emitted. On air the next frame header
         * (or silence) terminates the frame; the decoders detect the long
         * low and recover the last bit from the preceding high pulse. */
    }
    if (c->stop_high_mult) {
        /* Trailing stop HIGH (Bett/Clemsa/Hormann-style). */
        subghz_upload_add(&e->upload, true, te * c->stop_high_mult);
    }
    return SUBGHZ_PROTOCOL_DONE;
}

/* ---- Per-protocol const tables (Flipper-verified where noted) ---- */

static const pwm_const_t gate_tx_const = {
    /* Flipper gate_tx: te 350/700/100, header L 47TE, start H 2TE. */
    .te_short = 350, .te_long = 700, .te_delta = 100,
    .header_mult = 47, .header_delta_mult = 47,
    .start_high = 2, .start_low = 0, .end_gap_mult = 10,
    .min_bits = 24, .max_bits = 24,
    .h_for_one = 1, .l_for_one = 2, .h_for_zero = 2, .l_for_zero = 1,
    .bit_from_high = false, .invert_data = false,
};

static const pwm_const_t ansonic_const = {
    /* Flipper ansonic: te 555/1111/120, header L 35TE, start H 1TE. */
    .te_short = 555, .te_long = 1111, .te_delta = 120,
    .header_mult = 35, .header_delta_mult = 35,
    .start_high = 1, .start_low = 0, .end_gap_mult = 4,
    .min_bits = 12, .max_bits = 32,
    .h_for_one = 2, .l_for_one = 1, .h_for_zero = 1, .l_for_zero = 2,
    .bit_from_high = false, .invert_data = false,
};

static const pwm_const_t dooya_const = {
    /* Flipper dooya: te 366/733/120, header L 12*long, start H 13TE + L 2*long. */
    .te_short = 366, .te_long = 733, .te_delta = 120,
    .header_mult = 24 /* approx L12*long in short units */, .header_delta_mult = 20,
    .start_high = 13, .start_low = 2, .end_gap_mult = 4 /* x long */,
    .min_bits = 40, .max_bits = 40,
    .h_for_one = 2, .l_for_one = 1, .h_for_zero = 1, .l_for_zero = 2,
    .bit_from_high = true, .invert_data = false,
};

static const pwm_const_t nice_flo_const = {
    /* Nice FLO: TE ~700, header L 36TE, 0 = H1+L2, 1 = H2+L1. */
    .te_short = 700, .te_long = 1400, .te_delta = 200,
    .header_mult = 36, .header_delta_mult = 12,
    .start_high = 0, .start_low = 0, .end_gap_mult = 25,
    .min_bits = 12, .max_bits = 64,
    .h_for_one = 2, .l_for_one = 1, .h_for_zero = 1, .l_for_zero = 2,
    .bit_from_high = true, .invert_data = false,
};

static const pwm_const_t came_const = {
    /* CAME: TE ~320, header L 36TE, 0 = H1+L2, 1 = H2+L1, 12/24 bit. */
    .te_short = 320, .te_long = 640, .te_delta = 120,
    .header_mult = 36, .header_delta_mult = 12,
    .start_high = 0, .start_low = 0, .end_gap_mult = 20,
    .min_bits = 12, .max_bits = 24,
    .h_for_one = 2, .l_for_one = 1, .h_for_zero = 1, .l_for_zero = 2,
    .bit_from_high = true, .invert_data = false,
};

static const pwm_const_t holtek_const = {
    /* Holtek HT12x: TE ~350, header L 36TE, 0 = H1+L3, 1 = H3+L1, 12 bit. */
    .te_short = 350, .te_long = 1050, .te_delta = 140,
    .header_mult = 36, .header_delta_mult = 12,
    .start_high = 0, .start_low = 0, .end_gap_mult = 20,
    .min_bits = 12, .max_bits = 12,
    .h_for_one = 3, .l_for_one = 1, .h_for_zero = 1, .l_for_zero = 3,
    .bit_from_high = true, .invert_data = false,
};

static const pwm_const_t linear_const = {
    /* Linear/MegaCode: TE ~500, header L 36TE, Manchester-ish H1/L1 vs H2/L2. */
    .te_short = 500, .te_long = 1000, .te_delta = 180,
    .header_mult = 36, .header_delta_mult = 12,
    .start_high = 0, .start_low = 0, .end_gap_mult = 20,
    .min_bits = 20, .max_bits = 24,
    .h_for_one = 2, .l_for_one = 2, .h_for_zero = 1, .l_for_zero = 1,
    .bit_from_high = true, .invert_data = false,
};

/* ---- Batch 2: Flipper-verified static protocols ---- */

static const pwm_const_t bett_const = {
    /* Flipper bett: te 340/2000/150, header L 44TE, 18 bits, stop H. */
    .te_short = 340, .te_long = 2000, .te_delta = 150,
    .header_mult = 44, .header_delta_mult = 25,
    .start_high = 0, .start_low = 0, .end_gap_mult = 0,
    .stop_high_mult = 6,
    .min_bits = 18, .max_bits = 18,
    .h_for_one = 6, .l_for_one = 1, .h_for_zero = 1, .l_for_zero = 6,
    .bit_from_high = true, .invert_data = false,
};

static const pwm_const_t clemsa_const = {
    /* Flipper clemsa: te 385/2695/150, header L 51TE, 18 bits, stop H. */
    .te_short = 385, .te_long = 2695, .te_delta = 150,
    .header_mult = 51, .header_delta_mult = 25,
    .start_high = 0, .start_low = 0, .end_gap_mult = 0,
    .stop_high_mult = 7,
    .min_bits = 18, .max_bits = 18,
    .h_for_one = 7, .l_for_one = 1, .h_for_zero = 1, .l_for_zero = 7,
    .bit_from_high = true, .invert_data = false,
};

static const pwm_const_t doitrand_const = {
    /* Flipper doitrand: te 400/1100/150, header L 62TE, start H 2TE, 37 bits. */
    .te_short = 400, .te_long = 1100, .te_delta = 150,
    .header_mult = 62, .header_delta_mult = 25,
    .start_high = 2, .start_low = 0, .end_gap_mult = 10,
    .min_bits = 37, .max_bits = 37,
    .h_for_one = 1, .l_for_one = 3, .h_for_zero = 3, .l_for_zero = 1,
    .bit_from_high = false, .invert_data = false,
};

static const pwm_const_t elplast_const = {
    /* Flipper elplast: te 230/1550/160, header L ~54TE, 18 bits. */
    .te_short = 230, .te_long = 1550, .te_delta = 160,
    .header_mult = 54, .header_delta_mult = 25,
    .start_high = 0, .start_low = 0, .end_gap_mult = 0,
    .min_bits = 18, .max_bits = 18,
    .h_for_one = 7, .l_for_one = 1, .h_for_zero = 1, .l_for_zero = 7,
    .bit_from_high = true, .invert_data = false,
};

static const pwm_const_t feron_const = {
    /* Flipper feron: te 350/750/150, header L ~13TE, 32 bits. */
    .te_short = 350, .te_long = 750, .te_delta = 150,
    .header_mult = 13, .header_delta_mult = 12,
    .start_high = 0, .start_low = 0, .end_gap_mult = 0,
    .min_bits = 32, .max_bits = 32,
    .h_for_one = 2, .l_for_one = 1, .h_for_zero = 1, .l_for_zero = 2,
    .bit_from_high = true, .invert_data = false,
};

static const pwm_const_t roger_const = {
    /* Flipper roger: te 500/1000/270, header/gap L 19TE, 28 bits. */
    .te_short = 500, .te_long = 1000, .te_delta = 270,
    .header_mult = 19, .header_delta_mult = 12,
    .start_high = 0, .start_low = 0, .end_gap_mult = 0,
    .min_bits = 28, .max_bits = 28,
    .h_for_one = 2, .l_for_one = 1, .h_for_zero = 1, .l_for_zero = 2,
    .bit_from_high = true, .invert_data = false,
};

static const pwm_const_t mastercode_const = {
    /* Flipper mastercode: te 1072/2145/150, header L 15TE, 36 bits. */
    .te_short = 1072, .te_long = 2145, .te_delta = 150,
    .header_mult = 15, .header_delta_mult = 12,
    .start_high = 0, .start_low = 0, .end_gap_mult = 0,
    .min_bits = 36, .max_bits = 36,
    .h_for_one = 2, .l_for_one = 1, .h_for_zero = 1, .l_for_zero = 2,
    .bit_from_high = true, .invert_data = false,
};

static const pwm_const_t marantec24_const = {
    /* Flipper marantec24: unit 800us, gap L 14400, 24 bits. */
    .te_short = 800, .te_long = 1600, .te_delta = 200,
    .header_mult = 18, .header_delta_mult = 6,
    .start_high = 0, .start_low = 0, .end_gap_mult = 0,
    .min_bits = 24, .max_bits = 24,
    .h_for_one = 1, .l_for_one = 4, .h_for_zero = 2, .l_for_zero = 3,
    .bit_from_high = true, .invert_data = false,
};

static const pwm_const_t holtek40_const = {
    /* Flipper holtek (40-bit doorbell): te 430/870/100, header L 36TE. */
    .te_short = 430, .te_long = 870, .te_delta = 100,
    .header_mult = 36, .header_delta_mult = 12,
    .start_high = 1, .start_low = 0, .end_gap_mult = 0,
    .min_bits = 40, .max_bits = 40,
    .h_for_one = 2, .l_for_one = 1, .h_for_zero = 1, .l_for_zero = 2,
    .bit_from_high = true, .invert_data = false,
};

static const pwm_const_t phoenix_v2_const = {
    /* Flipper phoenix_v2: te 427/853/100, header L 60TE, start H 6TE, 52 bits. */
    .te_short = 427, .te_long = 853, .te_delta = 100,
    .header_mult = 60, .header_delta_mult = 25,
    .start_high = 6, .start_low = 0, .end_gap_mult = 10,
    .min_bits = 52, .max_bits = 52,
    .h_for_one = 1, .l_for_one = 2, .h_for_zero = 2, .l_for_zero = 1,
    .bit_from_high = false, .invert_data = false,
};

static const pwm_const_t hormann_const = {
    /* Flipper hormann: te 500/1000/200, HIGH header 24TE, gap L 1TE, 44 bits. */
    .te_short = 500, .te_long = 1000, .te_delta = 200,
    .header_mult = 24, .header_delta_mult = 24,
    .header_is_high = true, .gap_mult = 1,
    .start_high = 0, .start_low = 0, .end_gap_mult = 0,
    .end_high_mult = 5, .stop_high_mult = 24,
    .min_bits = 44, .max_bits = 44,
    .h_for_one = 2, .l_for_one = 1, .h_for_zero = 1, .l_for_zero = 2,
    .bit_from_high = true, .invert_data = false,
};

static const pwm_const_t gangqi_const = {
    /* Flipper gangqi: te 500/1200/200, gap L 2400, 34 bits. */
    .te_short = 500, .te_long = 1200, .te_delta = 200,
    .header_mult = 5, .header_delta_mult = 5,
    .start_high = 0, .start_low = 0, .end_gap_mult = 0,
    .min_bits = 34, .max_bits = 34,
    .h_for_one = 2, .l_for_one = 1, .h_for_zero = 1, .l_for_zero = 2,
    .bit_from_high = true, .invert_data = false,
};

/* ---- Boilerplate generators for the PWM family ---- */
#define PWM_PROTOCOL_SIMPLE(lname, cst, disp_name, disp_desc, deffreq)                             \
    static subghz_protocol_status_t lname##_feed(                                                  \
        subghz_protocol_decoder_t *d, bool level, uint32_t dur) {                                  \
        const pwm_const_t *c = &cst;                                                              \
        pwm_priv_t *p = d->priv;                                                                  \
        if (p->step == PWM_RESET) {                                                               \
            if (!level && !c->header_is_high && c->header_mult &&                                     \
                subghz_duration_match(dur, pwm_header_len(c),                                         \
                                      c->te_delta * c->header_delta_mult)) {                          \
                if (c->start_high) {                                                              \
                    p->step = PWM_HEADER;                                                         \
                } else {                                                                          \
                    p->step = PWM_DATA;                                                           \
                    d->decoded_data = 0;                                                          \
                    d->decoded_bits = 0;                                                          \
                    d->te_us = c->te_short;                                                       \
                }                                                                                 \
                return SUBGHZ_PROTOCOL_SYNC;                                                      \
            }                                                                                     \
            if (level && c->header_is_high && c->header_mult &&                                       \
                subghz_duration_match(dur, pwm_header_len(c),                                         \
                                      c->te_delta * c->header_delta_mult)) {                          \
                p->step = PWM_HDRGAP;                                                             \
                return SUBGHZ_PROTOCOL_SYNC;                                                      \
            }                                                                                     \
            return SUBGHZ_PROTOCOL_SYNC;                                                          \
        }                                                                                         \
        if (p->step == PWM_HEADER) {                                                              \
            if (level &&                                                                           \
                subghz_duration_match(dur, c->te_short * c->start_high, c->te_delta * 5u)) {        \
                if (c->start_low && c->bit_from_high) {                                            \
                    /* Dooya-style: high part seen, wait for long low. */                          \
                    p->te_last = 0xfffffffful;                                                    \
                    p->step = PWM_CHECK;                                                          \
                } else {                                                                          \
                    p->step = PWM_DATA;                                                           \
                    d->decoded_data = 0;                                                          \
                    d->decoded_bits = 0;                                                          \
                    d->te_us = c->te_short;                                                       \
                }                                                                                 \
            } else if (!level && p->te_last == 0xfffffffful &&                                      \
                       subghz_duration_match(dur, c->te_long * c->start_low,                        \
                                             c->te_delta * 3u)) {                                  \
                p->step = PWM_DATA;                                                               \
                d->decoded_data = 0;                                                              \
                d->decoded_bits = 0;                                                              \
                d->te_us = c->te_short;                                                           \
            } else {                                                                              \
                p->step = PWM_RESET;                                                              \
            }                                                                                     \
            return SUBGHZ_PROTOCOL_SYNC;                                                          \
        }                                                                                         \
        return pwm_feed(d, c, level, dur);                                                         \
    }                                                                                             \
    static const char *lname##_get_data(subghz_protocol_decoder_t *d, char *b, size_t s) {          \
        const pwm_const_t *c = &cst;                                                              \
        if (d->decoded_bits < c->min_bits) {                                                      \
            return NULL;                                                                          \
        }                                                                                         \
        snprintf(b, s, "%s %ubit: 0x%08lx%08lx", disp_name, d->decoded_bits,                        \
                 (unsigned long)(d->decoded_data >> 32), (unsigned long)d->decoded_data);           \
        return b;                                                                                 \
    }

#define PWM_ENCODER_SIMPLE(lname, cst, fixed_te, low_first)                                        \
    static subghz_protocol_status_t lname##_encode(                                                \
        subghz_protocol_encoder_t *e, uint64_t data, uint32_t bits) {                              \
        return pwm_encode(e, &cst, fixed_te, data, bits, low_first);                               \
    }                                                                                             \
    static const subghz_encoder_ops_t lname##_encoder_ops = {                                      \
        .feed = lname##_encode, .yield = encoder_yield_generic, .reset = encoder_reset_generic,    \
    };

PWM_PROTOCOL_SIMPLE(gate_tx, gate_tx_const, "GateTX", "", 433920)
PWM_PROTOCOL_SIMPLE(ansonic, ansonic_const, "Ansonic", "", 433920)
PWM_PROTOCOL_SIMPLE(dooya, dooya_const, "Dooya", "", 433920)
PWM_PROTOCOL_SIMPLE(nice_flo, nice_flo_const, "Nice FLO", "", 433920)
PWM_PROTOCOL_SIMPLE(came, came_const, "CAME", "", 433920)
PWM_PROTOCOL_SIMPLE(holtek, holtek_const, "Holtek HT12x", "", 433920)
PWM_PROTOCOL_SIMPLE(linear, linear_const, "Linear", "", 315000)
PWM_PROTOCOL_SIMPLE(bett, bett_const, "BETT", "", 433920)
PWM_PROTOCOL_SIMPLE(clemsa, clemsa_const, "Clemsa", "", 433920)
PWM_PROTOCOL_SIMPLE(doitrand, doitrand_const, "Doitrand", "", 433920)
PWM_PROTOCOL_SIMPLE(elplast, elplast_const, "Elplast", "", 433920)
PWM_PROTOCOL_SIMPLE(feron, feron_const, "Feron", "", 433920)
PWM_PROTOCOL_SIMPLE(roger, roger_const, "Roger", "", 433920)
PWM_PROTOCOL_SIMPLE(mastercode, mastercode_const, "Mastercode", "", 433920)
PWM_PROTOCOL_SIMPLE(marantec24, marantec24_const, "Marantec24", "", 433920)
PWM_PROTOCOL_SIMPLE(holtek40, holtek40_const, "Holtek", "", 433920)
PWM_PROTOCOL_SIMPLE(phoenix_v2, phoenix_v2_const, "Phoenix V2", "", 433920)
PWM_PROTOCOL_SIMPLE(hormann, hormann_const, "Hormann", "", 433920)
PWM_PROTOCOL_SIMPLE(gangqi, gangqi_const, "Gangqi", "", 433920)

PWM_ENCODER_SIMPLE(gate_tx, gate_tx_const, 350, true)
PWM_ENCODER_SIMPLE(ansonic, ansonic_const, 555, true)
PWM_ENCODER_SIMPLE(dooya, dooya_const, 366, true)
PWM_ENCODER_SIMPLE(nice_flo, nice_flo_const, 700, true)
PWM_ENCODER_SIMPLE(came, came_const, 320, true)
PWM_ENCODER_SIMPLE(holtek, holtek_const, 350, true)
PWM_ENCODER_SIMPLE(linear, linear_const, 500, true)
PWM_ENCODER_SIMPLE(bett, bett_const, 340, true)
PWM_ENCODER_SIMPLE(clemsa, clemsa_const, 385, true)
PWM_ENCODER_SIMPLE(doitrand, doitrand_const, 400, true)
PWM_ENCODER_SIMPLE(elplast, elplast_const, 230, true)
PWM_ENCODER_SIMPLE(feron, feron_const, 350, true)
PWM_ENCODER_SIMPLE(roger, roger_const, 500, true)
PWM_ENCODER_SIMPLE(mastercode, mastercode_const, 1072, true)
PWM_ENCODER_SIMPLE(marantec24, marantec24_const, 800, true)
PWM_ENCODER_SIMPLE(holtek40, holtek40_const, 430, true)
PWM_ENCODER_SIMPLE(phoenix_v2, phoenix_v2_const, 427, true)
PWM_ENCODER_SIMPLE(hormann, hormann_const, 500, true)
PWM_ENCODER_SIMPLE(gangqi, gangqi_const, 500, true)

/* Detail strings. */
static const char *gate_tx_get_string(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->decoded_bits < 24u) return NULL;
    uint32_t v = (uint32_t)d->decoded_data;
    uint64_t rev = reverse_key(d->decoded_data, d->decoded_bits);
    uint32_t serial = ((uint32_t)rev & 0xffu) << 12 | (((uint32_t)rev >> 8) & 0xffu) << 4 |
                      (((uint32_t)rev >> 20) & 0xfu);
    snprintf(b, s, "GateTX 24bit\r\nKey:0x%06lX\r\nSn:0x%05lX Btn:%lX", (unsigned long)v,
             (unsigned long)serial, (unsigned long)((rev >> 16) & 0xfu));
    return b;
}

static const char *ansonic_get_string(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->decoded_bits < 12u) return NULL;
    snprintf(b, s, "Ansonic %ubit\r\nKey:0x%03lX\r\nBtn:%lX DIP:0x%03lX", d->decoded_bits,
             (unsigned long)(d->decoded_data & 0xfffu), (unsigned long)((d->decoded_data >> 1) & 3u),
             (unsigned long)(d->decoded_data & 0xfffu));
    return b;
}

static const char *dooya_get_string(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->decoded_bits < 40u) return NULL;
    uint64_t v = d->decoded_data;
    static const struct {
        uint8_t code;
        const char *name;
    } btns[] = { {0x11, "Up_Long"}, {0x1e, "Up_Short"}, {0x33, "Down_Long"},
                 {0x3c, "Down_Short"}, {0x55, "Stop"}, {0x79, "Up+Down"},
                 {0x80, "Up+Stop"}, {0x81, "Down+Stop"}, {0xcc, "P2"} };
    const char *btn = "Unknown";
    for (unsigned i = 0; i < COUNT_OF(btns); i++) {
        if ((v & 0xffu) == btns[i].code) {
            btn = btns[i].name;
            break;
        }
    }
    snprintf(b, s, "Dooya 40bit\r\nKey:0x%08lx%08lx\r\nSn:0x%06lx Btn:%s",
             (unsigned long)(v >> 32), (unsigned long)v, (unsigned long)(v >> 16u), btn);
    return b;
}

static const char *nice_flo_get_string(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->decoded_bits < 12u) return NULL;
    uint32_t v = (uint32_t)(d->decoded_data & 0xfffffful);
    snprintf(b, s, "Nice FLO %ubit\r\nKey:0x%06lX\r\nSn:0x%06lX Btn:%lu", d->decoded_bits,
             (unsigned long)v, (unsigned long)(v >> 2), (unsigned long)(v & 3u));
    return b;
}

static const char *came_get_string(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->decoded_bits != 12u && d->decoded_bits != 24u) return NULL;
    if (d->decoded_bits == 12u) {
        snprintf(b, s, "CAME 12bit\r\nKey:0x%03lX", (unsigned long)(d->decoded_data & 0xffful));
    } else {
        snprintf(b, s, "CAME 24bit\r\nKey:0x%06lX", (unsigned long)(d->decoded_data & 0xfffffful));
    }
    return b;
}

static const char *holtek_get_string(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->decoded_bits != 12u) return NULL;
    uint32_t v = (uint32_t)(d->decoded_data & 0xffful);
    snprintf(b, s, "Holtek HT12x 12bit\r\nKey:0x%03lX\r\nAddr:0x%02lX Cmd:0x%lX",
             (unsigned long)v, (unsigned long)(v >> 4), (unsigned long)(v & 0xfu));
    return b;
}

static const char *linear_get_string(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->decoded_bits < 20u) return NULL;
    snprintf(b, s, "Linear %ubit\r\nKey:0x%06lX", d->decoded_bits,
             (unsigned long)(d->decoded_data & 0xfffffful));
    return b;
}

static const char *bett_get_string(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->decoded_bits != 18u) return NULL;
    snprintf(b, s, "BETT 18bit\r\nKey:0x%05lX", (unsigned long)(d->decoded_data & 0x3fffful));
    return b;
}

static const char *clemsa_get_string(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->decoded_bits != 18u) return NULL;
    uint32_t v = (uint32_t)(d->decoded_data & 0x3fffful);
    snprintf(b, s, "Clemsa 18bit\r\nKey:0x%05lX Btn:%lX Sn:0x%04lX", (unsigned long)v,
             (unsigned long)(v & 3u), (unsigned long)((v >> 2) & 0xffffu));
    return b;
}

static const char *doitrand_get_string(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->decoded_bits != 37u) return NULL;
    uint64_t v = d->decoded_data;
    snprintf(b, s, "Doitrand 37bit\r\nKey:0x%08lx%08lx\r\nBtn:%llX",
             (unsigned long)(v >> 32), (unsigned long)v, (unsigned long long)(v & 0xffu));
    return b;
}

static const char *elplast_get_string(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->decoded_bits != 18u) return NULL;
    snprintf(b, s, "Elplast 18bit\r\nKey:0x%05lX", (unsigned long)(d->decoded_data & 0x3fffful));
    return b;
}

static const char *feron_get_string(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->decoded_bits != 32u) return NULL;
    uint32_t v = (uint32_t)d->decoded_data;
    snprintf(b, s, "Feron 32bit\r\nKey:0x%08lX\r\nSn:0x%04lX Cmd:0x%04lX", (unsigned long)v,
             (unsigned long)(v >> 16), (unsigned long)(v & 0xffffu));
    return b;
}

static const char *roger_get_string(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->decoded_bits != 28u) return NULL;
    uint32_t v = (uint32_t)(d->decoded_data & 0xffffffful);
    snprintf(b, s, "Roger 28bit\r\nKey:0x%07lX\r\nSn:0x%04lX Btn:%lX", (unsigned long)v,
             (unsigned long)(v >> 12), (unsigned long)((v >> 8) & 0xfu));
    return b;
}

static const char *mastercode_get_string(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->decoded_bits != 36u) return NULL;
    uint64_t v = d->decoded_data;
    snprintf(b, s, "Mastercode 36bit\r\nKey:0x%08lx%08lx\r\nBtn:%lu Sn:0x%04llX",
             (unsigned long)(v >> 32), (unsigned long)v, (unsigned long)((v >> 2) & 3u),
             (unsigned long long)((v >> 4) & 0xffffu));
    return b;
}

static const char *marantec24_get_string(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->decoded_bits != 24u) return NULL;
    uint32_t v = (uint32_t)(d->decoded_data & 0xfffffful);
    snprintf(b, s, "Marantec24 24bit\r\nKey:0x%06lX\r\nSn:0x%05lX Btn:%lX", (unsigned long)v,
             (unsigned long)(v >> 4), (unsigned long)(v & 0xfu));
    return b;
}

static const char *holtek40_get_string(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->decoded_bits != 40u) return NULL;
    uint64_t v = d->decoded_data;
    if (((v >> 32) & 0xf0u) == 0x50u) {
        uint64_t serial = reverse_key((v >> 16) & 0xffffful, 20);
        uint16_t btn = (uint16_t)(v & 0xffffu);
        uint8_t button = 0;
        if ((btn & 0xf) != 0xa) button = 0x10 | (btn & 0xf);
        else if (((btn >> 4) & 0xf) != 0xa) button = 0x20 | ((btn >> 4) & 0xf);
        else if (((btn >> 8) & 0xf) != 0xa) button = 0x30 | ((btn >> 8) & 0xf);
        else if (((btn >> 12) & 0xf) != 0xa) button = 0x40 | ((btn >> 12) & 0xf);
        snprintf(b, s, "Holtek 40bit\r\nKey:0x%08lx%08lx\r\nSn:0x%05llX Btn:%X",
                 (unsigned long)(v >> 32), (unsigned long)v, (unsigned long long)serial, button);
    } else {
        snprintf(b, s, "Holtek 40bit\r\nKey:0x%08lx%08lx", (unsigned long)(v >> 32),
                 (unsigned long)v);
    }
    return b;
}

static const char *phoenix_v2_get_string(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->decoded_bits != 52u) return NULL;
    uint64_t v = d->decoded_data;
    uint64_t rev = reverse_key(v, 56); /* 52 bits + 4 (Flipper quirk). */
    snprintf(b, s, "Phoenix V2 52bit\r\nKey:0x%08lx%08lx\r\nSn:0x%08lX Btn:%lX Cnt(enc):0x%04lX",
             (unsigned long)(v >> 32), (unsigned long)v, (unsigned long)(rev & 0xfffffffful),
             (unsigned long)((rev >> 32) & 0xful), (unsigned long)((rev >> 40) & 0xffffu));
    return b;
}

static const char *hormann_get_string(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->decoded_bits != 44u) return NULL;
    uint64_t v = d->decoded_data;
    snprintf(b, s, "Hormann 44bit\r\nKey:0x%08lx%08lx\r\nBtn:0x%lX",
             (unsigned long)(v >> 32), (unsigned long)v, (unsigned long)(v & 0xfu));
    return b;
}

static const char *gangqi_get_string(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->decoded_bits != 34u) return NULL;
    uint64_t v = d->decoded_data;
    snprintf(b, s, "Gangqi 34bit\r\nKey:0x%08lx%08lx", (unsigned long)(v >> 32),
             (unsigned long)v);
    return b;
}

#define PWM_DECODER_OPS(lname)                                                                    \
    static const subghz_decoder_ops_t lname##_decoder_ops = {                                      \
        .feed = lname##_feed, .reset = pwm_reset, .get_data = lname##_get_data,                    \
        .get_string = lname##_get_string,                                                          \
    };

PWM_DECODER_OPS(gate_tx)
PWM_DECODER_OPS(ansonic)
PWM_DECODER_OPS(dooya)
PWM_DECODER_OPS(nice_flo)
PWM_DECODER_OPS(came)
PWM_DECODER_OPS(holtek)
PWM_DECODER_OPS(linear)
PWM_DECODER_OPS(bett)
PWM_DECODER_OPS(clemsa)
PWM_DECODER_OPS(doitrand)
PWM_DECODER_OPS(elplast)
PWM_DECODER_OPS(feron)
PWM_DECODER_OPS(roger)
PWM_DECODER_OPS(mastercode)
PWM_DECODER_OPS(marantec24)
PWM_DECODER_OPS(holtek40)
PWM_DECODER_OPS(phoenix_v2)
PWM_DECODER_OPS(hormann)
PWM_DECODER_OPS(gangqi)

/* ============================================================
 * INTERTECHNO V3: te 275/1375/150, 32 or 36 bits.
 * Header H275 + L10450, sync H275 + L2750.
 * Bit: H275 + L275 + H275 + L(275|1375); dimm inserts H275+L275 pair.
 * ============================================================ */

typedef struct {
    uint8_t step;
    uint32_t te_last;
} itv3_priv_t;

enum { IT3_RESET = 0, IT3_SYNC, IT3_SYNCF, IT3_RUN, IT3_SAVE, IT3_CHECK, IT3_END };

#define IT3_TE 275u
#define IT3_LONG 1375u
#define IT3_D 150u

static subghz_protocol_status_t itv3_feed(
    subghz_protocol_decoder_t *d, bool level, uint32_t dur) {
    itv3_priv_t *p = d->priv;
    switch (p->step) {
    case IT3_RESET:
        if (!level && subghz_duration_match(dur, IT3_TE * 38u, IT3_D * 15u)) {
            p->step = IT3_SYNC;
        }
        break;
    case IT3_SYNC:
        if (level && subghz_duration_match(dur, IT3_TE, IT3_D)) {
            p->step = IT3_SYNCF;
        } else {
            p->step = IT3_RESET;
        }
        break;
    case IT3_SYNCF:
        if (!level && subghz_duration_match(dur, IT3_TE * 10u, IT3_D * 3u)) {
            p->step = IT3_RUN;
            d->decoded_data = 0;
            d->decoded_bits = 0;
            d->te_us = IT3_TE;
        } else {
            p->step = IT3_RESET;
        }
        break;
    case IT3_RUN:
        if (level && subghz_duration_match(dur, IT3_TE, IT3_D)) {
            p->step = IT3_SAVE;
        } else {
            p->step = IT3_RESET;
        }
        break;
    case IT3_SAVE:
        if (!level) {
            if (dur >= IT3_TE * 11u) {
                p->step = IT3_SYNC;
                if (d->decoded_bits == 32u || d->decoded_bits == 36u) {
                    decoder_finish(d);
                    return SUBGHZ_PROTOCOL_DONE;
                }
                d->decoded_data = 0;
                d->decoded_bits = 0;
                break;
            }
            p->te_last = dur;
            p->step = IT3_CHECK;
        } else {
            p->step = IT3_RESET;
        }
        break;
    case IT3_CHECK:
        if (level) {
            bool low_short = subghz_duration_match(p->te_last, IT3_TE, IT3_D);
            bool low_long = subghz_duration_match(p->te_last, IT3_LONG, IT3_D * 2u);
            bool high_short = subghz_duration_match(dur, IT3_TE, IT3_D);
            if (low_short && high_short) {
                decoder_add_bit(d, 0);
                p->step = IT3_END;
            } else if (low_long && high_short) {
                decoder_add_bit(d, 1);
                p->step = IT3_END;
            } else if (low_short && high_short && d->decoded_bits == 27u) {
                decoder_add_bit(d, 0); /* Dimm marker position. */
                p->step = IT3_END;
            } else {
                p->step = IT3_RESET;
            }
        } else {
            p->step = IT3_RESET;
        }
        break;
    case IT3_END:
        if (!level) {
            if (dur >= IT3_TE * 11u) {
                /* Stop bit (possibly merged with last data low). */
                p->step = IT3_SYNC;
                if (d->decoded_bits == 32u || d->decoded_bits == 36u) {
                    decoder_finish(d);
                    return SUBGHZ_PROTOCOL_DONE;
                }
                d->decoded_data = 0;
                d->decoded_bits = 0;
            } else if (
                subghz_duration_match(dur, IT3_TE, IT3_D) ||
                subghz_duration_match(dur, IT3_LONG, IT3_D * 2u)) {
                p->step = IT3_RUN;
            } else {
                p->step = IT3_RESET;
            }
        } else {
            p->step = IT3_RESET;
        }
        break;
    default:
        p->step = IT3_RESET;
        break;
    }
    return SUBGHZ_PROTOCOL_SYNC;
}

static void itv3_reset(subghz_protocol_decoder_t *d) {
    memset(d->priv, 0, sizeof(itv3_priv_t));
    d->decoded_data = 0;
    d->decoded_bits = 0;
    d->hash = 0;
}

static const char *itv3_get_data(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->decoded_bits != 32u && d->decoded_bits != 36u) {
        return NULL;
    }
    snprintf(b, s, "Intertechno V3 %ubit: 0x%08lx%08lx", d->decoded_bits,
             (unsigned long)(d->decoded_data >> 32), (unsigned long)d->decoded_data);
    return b;
}

static const char *itv3_get_string(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->decoded_bits != 32u && d->decoded_bits != 36u) {
        return NULL;
    }
    uint64_t v = d->decoded_data;
    if (d->decoded_bits == 32u) {
        uint32_t serial = (uint32_t)((v >> 6) & 0x3fffffful);
        bool all = ((v >> 5) & 1u) != 0;
        snprintf(b, s, "Intertechno V3 32bit\r\nKey:0x%08lX\r\nSn:0x%07lX %s Btn:%s",
                 (unsigned long)(uint32_t)v, (unsigned long)serial, all ? "Ch:All" : "Ch:Sel",
                 ((v >> 4) & 1u) ? "On" : "Off");
    } else {
        uint32_t serial = (uint32_t)((v >> 10) & 0x3fffffful);
        snprintf(b, s, "Intertechno V3 36bit\r\nKey:0x%08lx%08lx\r\nSn:0x%07lX Dimm:%lu%%",
                 (unsigned long)(v >> 32), (unsigned long)v,
                 (unsigned long)serial, (unsigned long)((v & 0xfu) * 667u / 100u));
    }
    return b;
}

static const subghz_decoder_ops_t itv3_decoder_ops = {
    .feed = itv3_feed, .reset = itv3_reset, .get_data = itv3_get_data,
    .get_string = itv3_get_string,
};

static subghz_protocol_status_t itv3_encode(
    subghz_protocol_encoder_t *e, uint64_t data, uint32_t bits) {
    if (bits != 32u && bits != 36u) {
        return SUBGHZ_PROTOCOL_ERROR;
    }
    subghz_upload_reset(&e->upload);
    e->te_us = IT3_TE;
    subghz_upload_add(&e->upload, true, IT3_TE);
    subghz_upload_add(&e->upload, false, IT3_TE * 38u);
    subghz_upload_add(&e->upload, true, IT3_TE);
    subghz_upload_add(&e->upload, false, IT3_TE * 10u);
    for (int i = (int)bits - 1; i >= 0; i--) {
        bool one = ((data >> i) & 1ull) != 0;
        if (bits == 36u && i == 9) {
            /* Dimm marker: short pair (matches Flipper encoder). */
            subghz_upload_add(&e->upload, true, IT3_TE);
            subghz_upload_add(&e->upload, false, IT3_TE);
            subghz_upload_add(&e->upload, true, IT3_TE);
            subghz_upload_add(&e->upload, false, IT3_TE);
            continue;
        }
        subghz_upload_add(&e->upload, true, IT3_TE);
        subghz_upload_add(&e->upload, false, one ? IT3_LONG : IT3_TE);
        subghz_upload_add(&e->upload, true, IT3_TE);
        subghz_upload_add(&e->upload, false, one ? IT3_TE : IT3_LONG);
    }
    return SUBGHZ_PROTOCOL_DONE;
}

static const subghz_encoder_ops_t itv3_encoder_ops = {
    .feed = itv3_encode, .yield = encoder_yield_generic, .reset = encoder_reset_generic,
};

/* ============================================================
 * SMC5326: te 300/900/200, 25 bits, preamble L 24TE, double-frame
 * confirm, TE averaged over the frame.
 * ============================================================ */

typedef struct {
    uint8_t step;
    uint32_t te_last;
    uint32_t te_sum;
    uint64_t last_data;
} smc_priv_t;

enum { SMC_RESET = 0, SMC_SAVE, SMC_CHECK };

#define SMC_TE 300u
#define SMC_LONG 900u
#define SMC_D 200u

static subghz_protocol_status_t smc_feed(
    subghz_protocol_decoder_t *d, bool level, uint32_t dur) {
    smc_priv_t *p = d->priv;
    switch (p->step) {
    case SMC_RESET:
        if (!level && subghz_duration_match(dur, SMC_TE * 24u, SMC_D * 12u)) {
            p->step = SMC_SAVE;
            d->decoded_data = 0;
            d->decoded_bits = 0;
            p->te_sum = 0;
            d->te_us = SMC_TE;
        }
        break;
    case SMC_SAVE:
        if (level) {
            p->te_last = dur;
            p->te_sum += dur;
            p->step = SMC_CHECK;
        }
        break;
    case SMC_CHECK:
        if (!level) {
            if (dur >= SMC_LONG * 2u) {
                p->step = SMC_SAVE;
                if (d->decoded_bits == 25u) {
                    if (p->last_data == d->decoded_data && p->last_data) {
                        d->te_us = p->te_sum / (25u * 4u + 1u);
                        decoder_finish(d);
                        p->last_data = 0;
                        return SUBGHZ_PROTOCOL_DONE;
                    }
                    p->last_data = d->decoded_data;
                }
                d->decoded_data = 0;
                d->decoded_bits = 0;
                p->te_sum = 0;
                break;
            }
            p->te_sum += dur;
            bool hs = subghz_duration_match(p->te_last, SMC_TE, SMC_D);
            bool hl = subghz_duration_match(p->te_last, SMC_LONG, SMC_D * 3u);
            bool ls = subghz_duration_match(dur, SMC_TE, SMC_D);
            bool ll = subghz_duration_match(dur, SMC_LONG, SMC_D * 3u);
            if (hs && ll) {
                decoder_add_bit(d, 0);
                p->step = SMC_SAVE;
            } else if (hl && ls) {
                decoder_add_bit(d, 1);
                p->step = SMC_SAVE;
            } else {
                p->step = SMC_RESET;
            }
        } else {
            p->step = SMC_RESET;
        }
        break;
    default:
        p->step = SMC_RESET;
        break;
    }
    return SUBGHZ_PROTOCOL_SYNC;
}

static void smc_reset(subghz_protocol_decoder_t *d) {
    smc_priv_t *p = d->priv;
    memset(p, 0, sizeof(*p));
    d->decoded_data = 0;
    d->decoded_bits = 0;
    d->hash = 0;
}

static const char *smc_get_data(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->hash == 0) {
        return NULL;
    }
    snprintf(b, s, "SMC5326 25bit: 0x%07lx", (unsigned long)(d->decoded_data & 0x1fffffful));
    return b;
}

static const char *smc_get_string(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->hash == 0) {
        return NULL;
    }
    uint32_t v = (uint32_t)(d->decoded_data & 0x1fffffful);
    snprintf(b, s, "SMC5326 25bit\r\nKey:0x%07lX\r\nDIP:0x%04lX Btn:0x%lX TE:%luus",
             (unsigned long)v, (unsigned long)((v >> 9) & 0xffffu), (unsigned long)((v >> 1) & 0xffu),
             (unsigned long)d->te_us);
    return b;
}

static const subghz_decoder_ops_t smc_decoder_ops = {
    .feed = smc_feed, .reset = smc_reset, .get_data = smc_get_data,
    .get_string = smc_get_string,
};

static subghz_protocol_status_t smc_encode(
    subghz_protocol_encoder_t *e, uint64_t data, uint32_t bits) {
    ook_enc_priv_t *p = e->priv;
    uint32_t te = p->te_us ? p->te_us : SMC_TE;
    if (bits != 25u) {
        return SUBGHZ_PROTOCOL_ERROR;
    }
    subghz_upload_reset(&e->upload);
    e->te_us = te;
    /* Preamble wakes the receiver AGC (previous frame guard on air). */
    subghz_upload_add(&e->upload, false, te * 24u);
    for (int i = 24; i >= 0; i--) {
        if ((data >> i) & 1ull) {
            subghz_upload_add(&e->upload, true, te * 3u);
            subghz_upload_add(&e->upload, false, te);
        } else {
            subghz_upload_add(&e->upload, true, te);
            subghz_upload_add(&e->upload, false, te * 3u);
        }
    }
    subghz_upload_add(&e->upload, true, te);
    subghz_upload_add(&e->upload, false, te * 25u);
    return SUBGHZ_PROTOCOL_DONE;
}

static const subghz_encoder_ops_t smc_encoder_ops = {
    .feed = smc_encode, .yield = encoder_yield_generic, .reset = encoder_reset_generic,
};

/* ============================================================
 * KEELOQ dynamic: te 400/800/140, 64-bit frames.
 * Decode-only display of Fix/Hop/Serial/Btn; optional
 * simple-learning decrypt against user keystore keys.
 * TX is refused (rolling code safety).
 * ============================================================ */

typedef struct {
    uint8_t step;
    uint16_t header_count;
    uint32_t te_last;
} keeloq_priv_t;

enum { KL_RESET = 0, KL_PREAMBLE, KL_SAVE, KL_CHECK };

#define KL_TE 400u
#define KL_LONG 800u
#define KL_D 140u

/* Keystore hook: set by subghz_keystore.c (NULL = no keys). */
const void *subghz_keeloq_keystore_get(void);
void subghz_keeloq_keystore_set(const void *ks);

/* Decrypt attempt results (filled by keeloq_analyze on DONE). */
typedef struct {
    bool known;
    const char *manufacturer;
    uint16_t counter;
} keeloq_analysis_t;

static uint64_t keeloq_last_raw;
static keeloq_analysis_t keeloq_last_analysis;
static keeloq_analysis_t keeloq_analyze(uint64_t raw);

static subghz_protocol_status_t keeloq_feed(
    subghz_protocol_decoder_t *d, bool level, uint32_t dur) {
    keeloq_priv_t *p = d->priv;
    switch (p->step) {
    case KL_RESET:
        if (level && subghz_duration_match(dur, KL_TE, KL_D)) {
            p->step = KL_PREAMBLE;
            p->header_count++;
        }
        break;
    case KL_PREAMBLE:
        if (!level && subghz_duration_match(dur, KL_TE, KL_D)) {
            p->step = KL_RESET;
            break;
        }
        if (p->header_count > 2u &&
            subghz_duration_match(dur, KL_TE * 10u, KL_D * 10u)) {
            p->step = KL_SAVE;
            d->decoded_data = 0;
            d->decoded_bits = 0;
            d->te_us = KL_TE;
        } else {
            p->step = KL_RESET;
            p->header_count = 0;
        }
        break;
    case KL_SAVE:
        if (level) {
            p->te_last = dur;
            p->step = KL_CHECK;
        }
        break;
    case KL_CHECK:
        if (!level) {
            if (dur >= KL_TE * 2u + KL_D) {
                p->step = KL_RESET;
                if (d->decoded_bits >= 64u && d->decoded_bits <= 66u) {
                    d->decoded_bits = 64;
                    decoder_finish(d);
                    keeloq_last_raw = d->decoded_data;
                    keeloq_last_analysis = keeloq_analyze(d->decoded_data);
                    p->header_count = 0;
                    return SUBGHZ_PROTOCOL_DONE;
                }
                d->decoded_data = 0;
                d->decoded_bits = 0;
                p->header_count = 0;
                break;
            }
            bool hs = subghz_duration_match(p->te_last, KL_TE, KL_D);
            bool hl = subghz_duration_match(p->te_last, KL_LONG, KL_D * 2u);
            bool ls = subghz_duration_match(dur, KL_TE, KL_D);
            bool ll = subghz_duration_match(dur, KL_LONG, KL_D * 2u);
            if (hs && ll) {
                if (d->decoded_bits < 64u) {
                    decoder_add_bit(d, 1);
                } else {
                    d->decoded_bits++;
                }
                p->step = KL_SAVE;
            } else if (hl && ls) {
                if (d->decoded_bits < 64u) {
                    decoder_add_bit(d, 0);
                } else {
                    d->decoded_bits++;
                }
                p->step = KL_SAVE;
            } else {
                p->step = KL_RESET;
                p->header_count = 0;
            }
        } else {
            p->step = KL_RESET;
            p->header_count = 0;
        }
        break;
    default:
        p->step = KL_RESET;
        break;
    }
    return SUBGHZ_PROTOCOL_SYNC;
}

static void keeloq_reset(subghz_protocol_decoder_t *d) {
    memset(d->priv, 0, sizeof(keeloq_priv_t));
    d->decoded_data = 0;
    d->decoded_bits = 0;
    d->hash = 0;
}

/* Decrypt attempt results. */
static keeloq_analysis_t keeloq_analyze(uint64_t raw) {
    keeloq_analysis_t a = { .known = false, .manufacturer = "Unknown", .counter = 0 };
    uint64_t rev = reverse_key(raw, 64);
    uint32_t fix = (uint32_t)(rev >> 32);
    uint32_t hop = (uint32_t)rev;
    /* AN-Motors / HCS101 fixed-pattern checks (Flipper keeloq.c). */
    if (((hop >> 24) == ((hop >> 16) & 0xffu)) && ((fix >> 28) == ((hop >> 12) & 0xfu)) &&
        ((hop & 0xfffu) == 0x404u)) {
        a.known = true;
        a.manufacturer = "AN-Motors";
        a.counter = (uint16_t)(hop >> 16);
        return a;
    }
    if (((hop & 0xfffu) == 0u) && ((fix >> 28) == ((hop >> 12) & 0xfu))) {
        a.known = true;
        a.manufacturer = "HCS101";
        a.counter = (uint16_t)(hop >> 16);
        return a;
    }
    /* Simple-learning decrypt against keystore keys. */
    const void *ks = subghz_keeloq_keystore_get();
    if (ks) {
        extern bool subghz_keystore_simple_decrypt(const subghz_keystore_t *ks, uint32_t fix, uint32_t hop,
                                                   const char **mfname, uint16_t *cnt);
        const char *mf = NULL;
        uint16_t cnt = 0;
        if (subghz_keystore_simple_decrypt(ks, fix, hop, &mf, &cnt)) {
            a.known = true;
            a.manufacturer = mf;
            a.counter = cnt;
        }
    }
    return a;
}

static const char *keeloq_get_data(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->hash == 0) {
        return NULL;
    }
    snprintf(b, s, "KeeLoq 64bit: 0x%08lx%08lx", (unsigned long)(keeloq_last_raw >> 32),
             (unsigned long)keeloq_last_raw);
    return b;
}

static const char *keeloq_get_string(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->hash == 0) {
        return NULL;
    }
    uint64_t rev = reverse_key(keeloq_last_raw, 64);
    uint32_t fix = (uint32_t)(rev >> 32);
    uint32_t hop = (uint32_t)rev;
    uint32_t serial = fix & 0x0ffffffful;
    uint8_t btn = (uint8_t)(fix >> 28);
    if (keeloq_last_analysis.known) {
        snprintf(b, s,
                 "KeeLoq 64bit\r\nFix:0x%08lX Cnt:%04X\r\nHop:0x%08lX Btn:%X\r\nMF:%s\r\nSn:0x%07lX",
                 (unsigned long)fix, keeloq_last_analysis.counter, (unsigned long)hop, btn,
                 keeloq_last_analysis.manufacturer, (unsigned long)serial);
    } else {
        snprintf(b, s,
                 "KeeLoq 64bit (rolling, no key)\r\nFix:0x%08lX\r\nHop:0x%08lX Btn:%X\r\nSn:0x%07lX",
                 (unsigned long)fix, (unsigned long)hop, btn, (unsigned long)serial);
    }
    return b;
}

static const subghz_decoder_ops_t keeloq_decoder_ops = {
    .feed = keeloq_feed, .reset = keeloq_reset, .get_data = keeloq_get_data,
    .get_string = keeloq_get_string,
};

/* ============================================================
 * NERO RADIO: te 200/400/80, 49x preamble, start H800, 56 bits,
 * last bit carries the long L7400 stop gap.
 * ============================================================ */

typedef struct {
    uint8_t step;
    uint16_t preamble;
    uint32_t te_last;
} nero_priv_t;

enum { NERO_RESET = 0, NERO_PREAMBLE, NERO_SAVE, NERO_CHECK };

#define NERO_TE 200u
#define NERO_LONG 400u
#define NERO_D 80u

static subghz_protocol_status_t nero_radio_feed(
    subghz_protocol_decoder_t *d, bool level, uint32_t dur) {
    nero_priv_t *p = d->priv;
    switch (p->step) {
    case NERO_RESET:
        if (level && subghz_duration_match(dur, NERO_TE, NERO_D)) {
            p->step = NERO_PREAMBLE;
            p->te_last = dur;
            p->preamble = 0;
        }
        break;
    case NERO_PREAMBLE:
        if (level) {
            if (subghz_duration_match(dur, NERO_TE, NERO_D) ||
                subghz_duration_match(dur, NERO_TE * 4u, NERO_D)) {
                p->te_last = dur;
            } else {
                p->step = NERO_RESET;
            }
        } else if (subghz_duration_match(dur, NERO_TE, NERO_D)) {
            if (subghz_duration_match(p->te_last, NERO_TE, NERO_D)) {
                p->preamble++;
            } else if (subghz_duration_match(p->te_last, NERO_TE * 4u, NERO_D)) {
                if (p->preamble > 40u) {
                    p->step = NERO_SAVE;
                    d->decoded_data = 0;
                    d->decoded_bits = 0;
                    d->te_us = NERO_TE;
                } else {
                    p->step = NERO_RESET;
                }
            } else {
                p->step = NERO_RESET;
            }
        } else {
            p->step = NERO_RESET;
        }
        break;
    case NERO_SAVE:
        if (level) {
            p->te_last = dur;
            p->step = NERO_CHECK;
        } else {
            p->step = NERO_RESET;
        }
        break;
    case NERO_CHECK:
        if (!level) {
            if (dur >= NERO_TE * 10u + NERO_D * 2u) {
                /* Stop gap: recover the last bit from the high. */
                if (subghz_duration_match(p->te_last, NERO_TE, NERO_D)) {
                    decoder_add_bit(d, 0);
                } else if (subghz_duration_match(p->te_last, NERO_LONG, NERO_D)) {
                    decoder_add_bit(d, 1);
                }
                p->step = NERO_RESET;
                if (d->decoded_bits == 56u) {
                    decoder_finish(d);
                    return SUBGHZ_PROTOCOL_DONE;
                }
                d->decoded_data = 0;
                d->decoded_bits = 0;
                break;
            }
            bool hs = subghz_duration_match(p->te_last, NERO_TE, NERO_D);
            bool hl = subghz_duration_match(p->te_last, NERO_LONG, NERO_D);
            bool ls = subghz_duration_match(dur, NERO_TE, NERO_D);
            bool ll = subghz_duration_match(dur, NERO_LONG, NERO_D);
            if (hs && ll) {
                decoder_add_bit(d, 0);
                p->step = NERO_SAVE;
            } else if (hl && ls) {
                decoder_add_bit(d, 1);
                p->step = NERO_SAVE;
            } else {
                p->step = NERO_RESET;
            }
        } else {
            p->step = NERO_RESET;
        }
        break;
    default:
        p->step = NERO_RESET;
        break;
    }
    return SUBGHZ_PROTOCOL_SYNC;
}

static void nero_reset(subghz_protocol_decoder_t *d) {
    memset(d->priv, 0, sizeof(nero_priv_t));
    d->decoded_data = 0;
    d->decoded_bits = 0;
    d->hash = 0;
}

static const char *nero_radio_get_data(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->hash == 0) {
        return NULL;
    }
    snprintf(b, s, "Nero Radio 56bit: 0x%08lx%08lx", (unsigned long)(d->decoded_data >> 32),
             (unsigned long)d->decoded_data);
    return b;
}

static const char *nero_radio_get_string(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->hash == 0) {
        return NULL;
    }
    snprintf(b, s, "Nero Radio 56bit\r\nKey:0x%08lx%08lx\r\nSn:0x%06llX Btn:%llX",
             (unsigned long)(d->decoded_data >> 32), (unsigned long)d->decoded_data,
             (unsigned long long)((d->decoded_data >> 8) & 0xffffffull),
             (unsigned long long)(d->decoded_data & 0xffu));
    return b;
}

static const subghz_decoder_ops_t nero_radio_decoder_ops = {
    .feed = nero_radio_feed, .reset = nero_reset, .get_data = nero_radio_get_data,
    .get_string = nero_radio_get_string,
};

static subghz_protocol_status_t nero_radio_encode(
    subghz_protocol_encoder_t *e, uint64_t data, uint32_t bits) {
    if (bits != 56u) {
        return SUBGHZ_PROTOCOL_ERROR;
    }
    subghz_upload_reset(&e->upload);
    e->te_us = NERO_TE;
    for (int i = 0; i < 49; i++) {
        subghz_upload_add(&e->upload, true, NERO_TE);
        subghz_upload_add(&e->upload, false, NERO_TE);
    }
    subghz_upload_add(&e->upload, true, NERO_TE * 4u);
    subghz_upload_add(&e->upload, false, NERO_TE);
    for (int i = 55; i >= 1; i--) {
        if ((data >> i) & 1ull) {
            subghz_upload_add(&e->upload, true, NERO_LONG);
            subghz_upload_add(&e->upload, false, NERO_TE);
        } else {
            subghz_upload_add(&e->upload, true, NERO_TE);
            subghz_upload_add(&e->upload, false, NERO_LONG);
        }
    }
    if (data & 1ull) {
        subghz_upload_add(&e->upload, true, NERO_LONG);
    } else {
        subghz_upload_add(&e->upload, true, NERO_TE);
    }
    subghz_upload_add(&e->upload, false, NERO_TE * 37u);
    return SUBGHZ_PROTOCOL_DONE;
}

static const subghz_encoder_ops_t nero_radio_encoder_ops = {
    .feed = nero_radio_encode, .yield = encoder_yield_generic, .reset = encoder_reset_generic,
};

/* ============================================================
 * NERO SKETCH: te 330/660/150, 47x preamble, start H1320, 40 bits,
 * stop H990.
 * ============================================================ */

typedef struct {
    uint8_t step;
    uint16_t preamble;
    uint32_t te_last;
} nsketch_priv_t;

#define NSK_TE 330u
#define NSK_LONG 660u
#define NSK_D 150u

static subghz_protocol_status_t nero_sketch_feed(
    subghz_protocol_decoder_t *d, bool level, uint32_t dur) {
    nsketch_priv_t *p = d->priv;
    switch (p->step) {
    case NERO_RESET:
        if (level && subghz_duration_match(dur, NSK_TE, NSK_D)) {
            p->step = NERO_PREAMBLE;
            p->te_last = dur;
            p->preamble = 0;
        }
        break;
    case NERO_PREAMBLE:
        if (level) {
            if (subghz_duration_match(dur, NSK_TE, NSK_D) ||
                subghz_duration_match(dur, NSK_TE * 4u, NSK_D)) {
                p->te_last = dur;
            } else {
                p->step = NERO_RESET;
            }
        } else if (subghz_duration_match(dur, NSK_TE, NSK_D)) {
            if (subghz_duration_match(p->te_last, NSK_TE, NSK_D)) {
                p->preamble++;
            } else if (subghz_duration_match(p->te_last, NSK_TE * 4u, NSK_D)) {
                if (p->preamble > 40u) {
                    p->step = NERO_SAVE;
                    d->decoded_data = 0;
                    d->decoded_bits = 0;
                    d->te_us = NSK_TE;
                } else {
                    p->step = NERO_RESET;
                }
            } else {
                p->step = NERO_RESET;
            }
        } else {
            p->step = NERO_RESET;
        }
        break;
    case NERO_SAVE:
        if (level) {
            if (dur >= NSK_TE * 2u + NSK_D * 2u) {
                /* Stop bit. */
                p->step = NERO_RESET;
                if (d->decoded_bits == 40u) {
                    decoder_finish(d);
                    return SUBGHZ_PROTOCOL_DONE;
                }
                d->decoded_data = 0;
                d->decoded_bits = 0;
                break;
            }
            p->te_last = dur;
            p->step = NERO_CHECK;
        } else {
            p->step = NERO_RESET;
        }
        break;
    case NERO_CHECK:
        if (!level) {
            bool hs = subghz_duration_match(p->te_last, NSK_TE, NSK_D);
            bool hl = subghz_duration_match(p->te_last, NSK_LONG, NSK_D);
            bool ls = subghz_duration_match(dur, NSK_TE, NSK_D);
            bool ll = subghz_duration_match(dur, NSK_LONG, NSK_D);
            if (hs && ll) {
                decoder_add_bit(d, 0);
                p->step = NERO_SAVE;
            } else if (hl && ls) {
                decoder_add_bit(d, 1);
                p->step = NERO_SAVE;
            } else {
                p->step = NERO_RESET;
            }
        } else {
            p->step = NERO_RESET;
        }
        break;
    default:
        p->step = NERO_RESET;
        break;
    }
    return SUBGHZ_PROTOCOL_SYNC;
}

static const char *nero_sketch_get_data(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->hash == 0) {
        return NULL;
    }
    snprintf(b, s, "Nero Sketch 40bit: 0x%08lx%08lx", (unsigned long)(d->decoded_data >> 32),
             (unsigned long)d->decoded_data);
    return b;
}

static const char *nero_sketch_get_string(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->hash == 0) {
        return NULL;
    }
    snprintf(b, s, "Nero Sketch 40bit\r\nKey:0x%08lx%08lx", (unsigned long)(d->decoded_data >> 32),
             (unsigned long)d->decoded_data);
    return b;
}

static const subghz_decoder_ops_t nero_sketch_decoder_ops = {
    .feed = nero_sketch_feed, .reset = nero_reset, .get_data = nero_sketch_get_data,
    .get_string = nero_sketch_get_string,
};

static subghz_protocol_status_t nero_sketch_encode(
    subghz_protocol_encoder_t *e, uint64_t data, uint32_t bits) {
    if (bits != 40u) {
        return SUBGHZ_PROTOCOL_ERROR;
    }
    subghz_upload_reset(&e->upload);
    e->te_us = NSK_TE;
    for (int i = 0; i < 47; i++) {
        subghz_upload_add(&e->upload, true, NSK_TE);
        subghz_upload_add(&e->upload, false, NSK_TE);
    }
    subghz_upload_add(&e->upload, true, NSK_TE * 4u);
    subghz_upload_add(&e->upload, false, NSK_TE);
    for (int i = 39; i >= 0; i--) {
        if ((data >> i) & 1ull) {
            subghz_upload_add(&e->upload, true, NSK_LONG);
            subghz_upload_add(&e->upload, false, NSK_TE);
        } else {
            subghz_upload_add(&e->upload, true, NSK_TE);
            subghz_upload_add(&e->upload, false, NSK_LONG);
        }
    }
    subghz_upload_add(&e->upload, true, NSK_TE * 3u);
    subghz_upload_add(&e->upload, false, NSK_TE);
    return SUBGHZ_PROTOCOL_DONE;
}

static const subghz_encoder_ops_t nero_sketch_encoder_ops = {
    .feed = nero_sketch_encode, .yield = encoder_yield_generic, .reset = encoder_reset_generic,
};

/* ============================================================
 * MEGACODE: te 1000, header L 13TE, start H (= ref bit 1), then
 * state-dependent lows, 24 bits. Decoder ported from Flipper;
 * encoder keys lows off the previous bit per the decoder model.
 * ============================================================ */

typedef struct {
    uint8_t step;
    uint8_t last_bit;
    uint32_t te_last;
} megacode_priv_t;

enum { MEGA_RESET = 0, MEGA_START, MEGA_SAVE, MEGA_CHECK };

#define MEGA_TE 1000u
#define MEGA_D 200u

static subghz_protocol_status_t megacode_feed(
    subghz_protocol_decoder_t *d, bool level, uint32_t dur) {
    megacode_priv_t *p = d->priv;
    switch (p->step) {
    case MEGA_RESET:
        if (!level && dur >= MEGA_TE * 10u && dur <= MEGA_TE * 16u + MEGA_D * 17u) {
            p->step = MEGA_START;
        }
        break;
    case MEGA_START:
        if (level && subghz_duration_match(dur, MEGA_TE, MEGA_D)) {
            p->step = MEGA_SAVE;
            d->decoded_data = 0;
            d->decoded_bits = 0;
            d->te_us = MEGA_TE;
            decoder_add_bit(d, 1);
            p->last_bit = 1;
        } else {
            p->step = MEGA_RESET;
        }
        break;
    case MEGA_SAVE:
        if (!level) {
            if (dur >= MEGA_TE * 10u) {
                p->step = MEGA_RESET;
                if (d->decoded_bits == 24u) {
                    decoder_finish(d);
                    return SUBGHZ_PROTOCOL_DONE;
                }
                d->decoded_data = 0;
                d->decoded_bits = 0;
                break;
            }
            if (!p->last_bit) {
                p->te_last = dur >= MEGA_TE * 3u ? dur - MEGA_TE * 3u : 0;
            } else {
                p->te_last = dur;
            }
            p->step = MEGA_CHECK;
        } else {
            p->step = MEGA_RESET;
        }
        break;
    case MEGA_CHECK:
        if (level) {
            if (subghz_duration_match(p->te_last, MEGA_TE * 5u, MEGA_D * 5u) &&
                subghz_duration_match(dur, MEGA_TE, MEGA_D)) {
                decoder_add_bit(d, 1);
                p->last_bit = 1;
                p->step = MEGA_SAVE;
            } else if (
                subghz_duration_match(p->te_last, MEGA_TE * 2u, MEGA_D * 2u) &&
                subghz_duration_match(dur, MEGA_TE, MEGA_D)) {
                decoder_add_bit(d, 0);
                p->last_bit = 0;
                p->step = MEGA_SAVE;
            } else {
                p->step = MEGA_RESET;
            }
        } else {
            p->step = MEGA_RESET;
        }
        break;
    default:
        p->step = MEGA_RESET;
        break;
    }
    return SUBGHZ_PROTOCOL_SYNC;
}

static void megacode_reset(subghz_protocol_decoder_t *d) {
    memset(d->priv, 0, sizeof(megacode_priv_t));
    d->decoded_data = 0;
    d->decoded_bits = 0;
    d->hash = 0;
}

static const char *megacode_get_data(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->hash == 0) {
        return NULL;
    }
    snprintf(b, s, "MegaCode 24bit: 0x%06lx", (unsigned long)(d->decoded_data & 0xfffffful));
    return b;
}

static const char *megacode_get_string(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->hash == 0) {
        return NULL;
    }
    uint32_t v = (uint32_t)(d->decoded_data & 0xfffffful);
    snprintf(b, s, "MegaCode 24bit\r\nKey:0x%06lX\r\nSn:0x%04lX Facility:%lX Btn:%X",
             (unsigned long)v, (unsigned long)(v & 0xffffu), (unsigned long)((v >> 16) & 0xfu),
             (unsigned)((v >> 20) & 0xfu));
    return b;
}

static const subghz_decoder_ops_t megacode_decoder_ops = {
    .feed = megacode_feed, .reset = megacode_reset, .get_data = megacode_get_data,
    .get_string = megacode_get_string,
};

static subghz_protocol_status_t megacode_encode(
    subghz_protocol_encoder_t *e, uint64_t data, uint32_t bits) {
    if (bits != 24u) {
        return SUBGHZ_PROTOCOL_ERROR;
    }
    /* NOTE: Flipper builds this upload backwards keyed off the NEXT bit,
     * which disagrees with its own decoder except for all-ones keys.
     * We key each low off the PREVIOUSLY transmitted bit (like the decoder
     * expects), which also matches real remotes:
     * prev=1: bit1 -> 5TE, bit0 -> 2TE; prev=0: bit1 -> 8TE, bit0 -> 5TE. */
    subghz_upload_reset(&e->upload);
    e->te_us = MEGA_TE;
    subghz_upload_add(&e->upload, false, (data & 1ull) ? MEGA_TE * 11u : MEGA_TE * 14u);
    subghz_upload_add(&e->upload, true, MEGA_TE); /* Start (= ref 1). */
    uint8_t prev = 1;
    for (int i = 22; i >= 0; i--) {
        uint8_t bit = ((data >> i) & 1ull) ? 1 : 0;
        uint32_t low;
        if (bit) {
            low = prev ? MEGA_TE * 5u : MEGA_TE * 8u;
        } else {
            low = prev ? MEGA_TE * 2u : MEGA_TE * 5u;
        }
        subghz_upload_add(&e->upload, false, low);
        subghz_upload_add(&e->upload, true, MEGA_TE);
        prev = bit;
    }
    return SUBGHZ_PROTOCOL_DONE;
}

static const subghz_encoder_ops_t megacode_encoder_ops = {
    .feed = megacode_encode, .yield = encoder_yield_generic, .reset = encoder_reset_generic,
};

/* ============================================================
 * LEGRAND: learned TE, header L 16TE, first bit HIGH-only, then
 * low-first pairs, double-frame confirm. Ported from Flipper.
 * ============================================================ */

typedef struct {
    uint8_t step;
    uint32_t te;
    uint32_t te_last;
    uint64_t last_data;
} legrand_priv_t;

enum { LEG_RESET = 0, LEG_FIRST, LEG_SAVE, LEG_CHECK };

/* Fixed match windows (Flipper uses constants for matching; instance TE
 * is only averaged for display). */
#define LEG_TE 375u
#define LEG_TEL 1125u
#define LEG_D 150u
#define LEG_DL 450u

static subghz_protocol_status_t legrand_feed(
    subghz_protocol_decoder_t *d, bool level, uint32_t dur) {
    legrand_priv_t *p = d->priv;
    switch (p->step) {
    case LEG_RESET:
        if (!level && dur >= LEG_TE * 16u - 1200u && dur <= LEG_TE * 16u + 1200u) {
            p->step = LEG_FIRST;
            d->decoded_data = 0;
            d->decoded_bits = 0;
            p->te = 0;
        }
        break;
    case LEG_FIRST:
        if (level) {
            if (dur >= LEG_TE - LEG_D && dur <= LEG_TE + LEG_D) {
                decoder_add_bit(d, 0);
                p->te += dur * 4u;
            } else if (dur >= LEG_TEL - LEG_DL && dur <= LEG_TEL + LEG_DL) {
                decoder_add_bit(d, 1);
                p->te += dur / 3u * 4u;
            }
            if (d->decoded_bits > 0) {
                p->step = LEG_SAVE;
                break;
            }
        }
        p->step = LEG_RESET;
        break;
    case LEG_SAVE:
        if (!level) {
            p->te_last = dur;
            p->te += dur;
            p->step = LEG_CHECK;
            break;
        }
        p->step = LEG_RESET;
        break;
    case LEG_CHECK:
        if (level) {
            bool found = false;
            if (p->te_last >= LEG_TEL - LEG_DL && p->te_last <= LEG_TEL + LEG_DL &&
                dur >= LEG_TE - LEG_D && dur <= LEG_TE + LEG_D) {
                decoder_add_bit(d, 0);
                found = true;
            }
            if (p->te_last >= LEG_TE - LEG_D && p->te_last <= LEG_TE + LEG_D &&
                dur >= LEG_TEL - LEG_DL && dur <= LEG_TEL + LEG_DL) {
                decoder_add_bit(d, 1);
                found = true;
            }
            if (found) {
                p->te += dur;
                if (d->decoded_bits < 18u) {
                    p->step = LEG_SAVE;
                    break;
                }
                if (p->last_data && p->last_data == d->decoded_data) {
                    d->te_us = p->te / (d->decoded_bits * 4u);
                    decoder_finish(d);
                    p->last_data = 0;
                    return SUBGHZ_PROTOCOL_DONE;
                }
                p->last_data = d->decoded_data;
            }
        }
        p->step = LEG_RESET;
        break;
    default:
        p->step = LEG_RESET;
        break;
    }
    return SUBGHZ_PROTOCOL_SYNC;
}

static void legrand_reset(subghz_protocol_decoder_t *d) {
    legrand_priv_t *p = d->priv;
    uint64_t keep = p->last_data;
    memset(p, 0, sizeof(*p));
    p->last_data = keep;
    d->decoded_data = 0;
    d->decoded_bits = 0;
    d->hash = 0;
}

static const char *legrand_get_data(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->hash == 0) {
        return NULL;
    }
    snprintf(b, s, "Legrand %ubit: 0x%05lx", d->decoded_bits,
             (unsigned long)(d->decoded_data & 0xffffful));
    return b;
}

static const char *legrand_get_string(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->hash == 0) {
        return NULL;
    }
    snprintf(b, s, "Legrand %ubit\r\nKey:0x%05lX Te:%luus", d->decoded_bits,
             (unsigned long)(d->decoded_data & 0xffffful), (unsigned long)d->te_us);
    return b;
}

static const subghz_decoder_ops_t legrand_decoder_ops = {
    .feed = legrand_feed, .reset = legrand_reset, .get_data = legrand_get_data,
    .get_string = legrand_get_string,
};

static subghz_protocol_status_t legrand_encode(
    subghz_protocol_encoder_t *e, uint64_t data, uint32_t bits) {
    ook_enc_priv_t *p = e->priv;
    uint32_t te = p->te_us ? p->te_us : 375u;
    if (bits != 18u) {
        return SUBGHZ_PROTOCOL_ERROR;
    }
    subghz_upload_reset(&e->upload);
    e->te_us = te;
    subghz_upload_add(&e->upload, false, te * 16u);
    for (int i = (int)bits - 1; i >= 0; i--) {
        bool one = ((data >> i) & 1ull) != 0;
        if (i == (int)bits - 1) {
            /* First bit is HIGH-only. */
            subghz_upload_add(&e->upload, true, one ? te * 3u : te);
        } else if (one) {
            subghz_upload_add(&e->upload, false, te);
            subghz_upload_add(&e->upload, true, te * 3u);
        } else {
            subghz_upload_add(&e->upload, false, te * 3u);
            subghz_upload_add(&e->upload, true, te);
        }
    }
    return SUBGHZ_PROTOCOL_DONE;
}

static const subghz_encoder_ops_t legrand_encoder_ops = {
    .feed = legrand_encode, .yield = encoder_yield_generic, .reset = encoder_reset_generic,
};

/* ============================================================
 * HOLLARM: te 200/1000/200, gap L 12TE, H200-based bits
 * (0 = L1000, 1 = L1600), 42 bits + byte checksum, data >> 2.
 * ============================================================ */

typedef struct {
    uint8_t step;
    uint32_t te_last;
} hollarm_priv_t;

enum { HOL_RESET = 0, HOL_SAVE, HOL_CHECK };

#define HOL_TE 200u
#define HOL_LONG 1000u
#define HOL_D 200u

static subghz_protocol_status_t hollarm_feed(
    subghz_protocol_decoder_t *d, bool level, uint32_t dur) {
    hollarm_priv_t *p = d->priv;
    switch (p->step) {
    case HOL_RESET:
        if (!level && subghz_duration_match(dur, HOL_TE * 12u, HOL_D * 2u)) {
            p->step = HOL_SAVE;
            d->decoded_data = 0;
            d->decoded_bits = 0;
            d->te_us = HOL_TE;
        }
        break;
    case HOL_SAVE:
        if (level) {
            p->te_last = dur;
            p->step = HOL_CHECK;
        } else {
            p->step = HOL_RESET;
        }
        break;
    case HOL_CHECK:
        if (!level) {
            if (subghz_duration_match(p->te_last, HOL_TE, HOL_D) &&
                subghz_duration_match(dur, HOL_LONG, HOL_D)) {
                decoder_add_bit(d, 0);
                p->step = HOL_SAVE;
            } else if (
                subghz_duration_match(p->te_last, HOL_TE, HOL_D) &&
                subghz_duration_match(dur, HOL_TE * 8u, HOL_D)) {
                decoder_add_bit(d, 1);
                p->step = HOL_SAVE;
            } else if (subghz_duration_match(dur, HOL_TE * 12u, HOL_D)) {
                decoder_add_bit(d, 0);
                if (d->decoded_bits == 42u) {
                    uint64_t v = d->decoded_data >> 2;
                    uint8_t sum = ((v >> 32) & 0xff) + ((v >> 24) & 0xff) +
                                  ((v >> 16) & 0xff) + ((v >> 8) & 0xff);
                    if (sum == (v & 0xff)) {
                        d->decoded_data = v;
                        d->decoded_bits = 40;
                        decoder_finish(d);
                        p->step = HOL_RESET;
                        return SUBGHZ_PROTOCOL_DONE;
                    }
                }
                d->decoded_data = 0;
                d->decoded_bits = 0;
                p->step = HOL_RESET;
            } else {
                p->step = HOL_RESET;
            }
        } else {
            p->step = HOL_RESET;
        }
        break;
    default:
        p->step = HOL_RESET;
        break;
    }
    return SUBGHZ_PROTOCOL_SYNC;
}

static void hollarm_reset(subghz_protocol_decoder_t *d) {
    memset(d->priv, 0, sizeof(hollarm_priv_t));
    d->decoded_data = 0;
    d->decoded_bits = 0;
    d->hash = 0;
}

static const char *hollarm_get_data(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->hash == 0) {
        return NULL;
    }
    snprintf(b, s, "Hollarm 40bit: 0x%08lx%08lx", (unsigned long)(d->decoded_data >> 32),
             (unsigned long)d->decoded_data);
    return b;
}

static const char *hollarm_get_string(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->hash == 0) {
        return NULL;
    }
    snprintf(b, s, "Hollarm 40bit\r\nKey:0x%08lx%08lx", (unsigned long)(d->decoded_data >> 32),
             (unsigned long)d->decoded_data);
    return b;
}

static const subghz_decoder_ops_t hollarm_decoder_ops = {
    .feed = hollarm_feed, .reset = hollarm_reset, .get_data = hollarm_get_data,
    .get_string = hollarm_get_string,
};

static subghz_protocol_status_t hollarm_encode(
    subghz_protocol_encoder_t *e, uint64_t data, uint32_t bits) {
    if (bits != 40u) {
        return SUBGHZ_PROTOCOL_ERROR;
    }
    /* Air format mirrors Flipper: 42 pairs of (data << 2), the last pair's
     * low replaced by the inter-parcel gap. Separation between repeats
     * comes from that gap (plus the TX loop inter-frame gap). */
    uint64_t air = (data << 2) & 0x3ffffffffffull;
    subghz_upload_reset(&e->upload);
    e->te_us = HOL_TE;
    for (int i = 41; i >= 0; i--) {
        subghz_upload_add(&e->upload, true, HOL_TE);
        if (i == 0) {
            subghz_upload_add(&e->upload, false, HOL_TE * 12u);
        } else if ((air >> i) & 1ull) {
            subghz_upload_add(&e->upload, false, HOL_TE * 8u);
        } else {
            subghz_upload_add(&e->upload, false, HOL_LONG);
        }
    }
    return SUBGHZ_PROTOCOL_DONE;
}

static const subghz_encoder_ops_t hollarm_encoder_ops = {
    .feed = hollarm_encode, .yield = encoder_yield_generic, .reset = encoder_reset_generic,
};

/* ============================================================
 * HONEYWELL WDB (doorbell): te 160/320/60, header L 3TE,
 * (H,L) pairs 0 = H160+L320 / 1 = H320+L160, end H 3TE + parity.
 * ============================================================ */

typedef struct {
    uint8_t step;
    uint32_t te_last;
} honeywell_priv_t;

enum { HW_RESET = 0, HW_SAVE, HW_CHECK };

#define HW_TE 160u
#define HW_LONG 320u
#define HW_D 60u

static subghz_protocol_status_t honeywell_feed(
    subghz_protocol_decoder_t *d, bool level, uint32_t dur) {
    honeywell_priv_t *p = d->priv;
    switch (p->step) {
    case HW_RESET:
        if (!level && subghz_duration_match(dur, HW_TE * 3u, HW_D)) {
            p->step = HW_SAVE;
            d->decoded_data = 0;
            d->decoded_bits = 0;
            d->te_us = HW_TE;
        }
        break;
    case HW_SAVE:
        if (level) {
            if (subghz_duration_match(dur, HW_TE * 3u, HW_D)) {
                p->step = HW_RESET;
                if (d->decoded_bits == 48u &&
                    (d->decoded_data & 1u) == subghz_even_parity(d->decoded_data >> 1, 47)) {
                    decoder_finish(d);
                    return SUBGHZ_PROTOCOL_DONE;
                }
                d->decoded_data = 0;
                d->decoded_bits = 0;
                break;
            }
            p->te_last = dur;
            p->step = HW_CHECK;
        } else {
            p->step = HW_RESET;
        }
        break;
    case HW_CHECK:
        if (!level) {
            if (subghz_duration_match(p->te_last, HW_TE, HW_D) &&
                subghz_duration_match(dur, HW_LONG, HW_D)) {
                decoder_add_bit(d, 0);
                p->step = HW_SAVE;
            } else if (
                subghz_duration_match(p->te_last, HW_LONG, HW_D) &&
                subghz_duration_match(dur, HW_TE, HW_D)) {
                decoder_add_bit(d, 1);
                p->step = HW_SAVE;
            } else {
                p->step = HW_RESET;
            }
        } else {
            p->step = HW_RESET;
        }
        break;
    default:
        p->step = HW_RESET;
        break;
    }
    return SUBGHZ_PROTOCOL_SYNC;
}

static void honeywell_reset(subghz_protocol_decoder_t *d) {
    memset(d->priv, 0, sizeof(honeywell_priv_t));
    d->decoded_data = 0;
    d->decoded_bits = 0;
    d->hash = 0;
}

static const char *honeywell_get_data(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->hash == 0) {
        return NULL;
    }
    snprintf(b, s, "Honeywell WDB 48bit: 0x%08lx%08lx", (unsigned long)(d->decoded_data >> 32),
             (unsigned long)d->decoded_data);
    return b;
}

static const char *honeywell_get_string(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->hash == 0) {
        return NULL;
    }
    uint64_t v = d->decoded_data;
    snprintf(b, s, "Honeywell WDB 48bit\r\nKey:0x%08lx%08lx\r\nSn:0x%05llX",
             (unsigned long)(v >> 32), (unsigned long)v,
             (unsigned long long)((v >> 8) & 0xffffful));
    return b;
}

static const subghz_decoder_ops_t honeywell_decoder_ops = {
    .feed = honeywell_feed, .reset = honeywell_reset, .get_data = honeywell_get_data,
    .get_string = honeywell_get_string,
};

static subghz_protocol_status_t honeywell_encode(
    subghz_protocol_encoder_t *e, uint64_t data, uint32_t bits) {
    if (bits != 48u) {
        return SUBGHZ_PROTOCOL_ERROR;
    }
    /* Fix even parity in the last bit like real devices. */
    if (((data & 1u) != subghz_even_parity(data >> 1, 47))) {
        data ^= 1u;
    }
    subghz_upload_reset(&e->upload);
    e->te_us = HW_TE;
    subghz_upload_add(&e->upload, false, HW_TE * 3u);
    for (int i = 47; i >= 0; i--) {
        if ((data >> i) & 1ull) {
            subghz_upload_add(&e->upload, true, HW_LONG);
            subghz_upload_add(&e->upload, false, HW_TE);
        } else {
            subghz_upload_add(&e->upload, true, HW_TE);
            subghz_upload_add(&e->upload, false, HW_LONG);
        }
    }
    subghz_upload_add(&e->upload, true, HW_TE * 3u);
    return SUBGHZ_PROTOCOL_DONE;
}

static const subghz_encoder_ops_t honeywell_encoder_ops = {
    .feed = honeywell_encode, .yield = encoder_yield_generic, .reset = encoder_reset_generic,
};

/* ============================================================
 * Registry.
 * ============================================================ */

static const subghz_protocol_info_t princeton_info = {
    .name = "Princeton", .description = "Princeton/PT2260/PT2262/EV1527 OOK",
    .frequency_default_khz = 433920, .te_min_us = 100, .te_max_us = 1000,
    .bit_min = 20, .bit_max = 64, .rolling = false,
};
static const subghz_protocol_info_t nice_flo_info = {
    .name = "Nice FLO", .description = "Nice FLO OOK",
    .frequency_default_khz = 433920, .te_min_us = 500, .te_max_us = 900,
    .bit_min = 12, .bit_max = 64, .rolling = false,
};
static const subghz_protocol_info_t came_info = {
    .name = "CAME", .description = "CAME 12/24-bit OOK",
    .frequency_default_khz = 433920, .te_min_us = 200, .te_max_us = 500,
    .bit_min = 12, .bit_max = 24, .rolling = false,
};
static const subghz_protocol_info_t holtek_info = {
    .name = "Holtek HT12x", .description = "Holtek HT12E/HT12A/HT12D 12-bit OOK",
    .frequency_default_khz = 433920, .te_min_us = 200, .te_max_us = 500,
    .bit_min = 12, .bit_max = 12, .rolling = false,
};
static const subghz_protocol_info_t linear_info = {
    .name = "Linear", .description = "Linear Access/MegaCode OOK",
    .frequency_default_khz = 315000, .te_min_us = 300, .te_max_us = 700,
    .bit_min = 20, .bit_max = 24, .rolling = false,
};
static const subghz_protocol_info_t itv3_info = {
    .name = "Intertechno V3", .description = "Intertechno V3 32/36-bit OOK",
    .frequency_default_khz = 433920, .te_min_us = 200, .te_max_us = 400,
    .bit_min = 32, .bit_max = 36, .rolling = false,
};
static const subghz_protocol_info_t gate_tx_info = {
    .name = "GateTX", .description = "Gate TX 24-bit OOK",
    .frequency_default_khz = 433920, .te_min_us = 250, .te_max_us = 450,
    .bit_min = 24, .bit_max = 24, .rolling = false,
};
static const subghz_protocol_info_t ansonic_info = {
    .name = "Ansonic", .description = "Ansonic 12-bit OOK",
    .frequency_default_khz = 433920, .te_min_us = 400, .te_max_us = 700,
    .bit_min = 12, .bit_max = 32, .rolling = false,
};
static const subghz_protocol_info_t dooya_info = {
    .name = "Dooya", .description = "Dooya 40-bit OOK blinds",
    .frequency_default_khz = 433920, .te_min_us = 250, .te_max_us = 500,
    .bit_min = 40, .bit_max = 40, .rolling = false,
};
static const subghz_protocol_info_t smc_info = {
    .name = "SMC5326", .description = "SMC5326/PT2240 25-bit OOK",
    .frequency_default_khz = 433920, .te_min_us = 200, .te_max_us = 500,
    .bit_min = 25, .bit_max = 25, .rolling = false,
};
static const subghz_protocol_info_t keeloq_info = {
    .name = "KeeLoq", .description = "KeeLoq dynamic (decode only, no TX)",
    .frequency_default_khz = 433920, .te_min_us = 300, .te_max_us = 500,
    .bit_min = 64, .bit_max = 66, .rolling = true,
};

static const subghz_protocol_info_t bett_info = {
    .name = "BETT", .description = "BETT 18-bit OOK",
    .frequency_default_khz = 433920, .te_min_us = 250, .te_max_us = 450,
    .bit_min = 18, .bit_max = 18, .rolling = false,
};
static const subghz_protocol_info_t clemsa_info = {
    .name = "Clemsa", .description = "Clemsa 18-bit OOK",
    .frequency_default_khz = 433920, .te_min_us = 280, .te_max_us = 500,
    .bit_min = 18, .bit_max = 18, .rolling = false,
};
static const subghz_protocol_info_t doitrand_info = {
    .name = "Doitrand", .description = "Doitrand 37-bit OOK",
    .frequency_default_khz = 433920, .te_min_us = 300, .te_max_us = 500,
    .bit_min = 37, .bit_max = 37, .rolling = false,
};
static const subghz_protocol_info_t elplast_info = {
    .name = "Elplast", .description = "Elplast 18-bit OOK",
    .frequency_default_khz = 433920, .te_min_us = 150, .te_max_us = 320,
    .bit_min = 18, .bit_max = 18, .rolling = false,
};
static const subghz_protocol_info_t feron_info = {
    .name = "Feron", .description = "Feron 32-bit OOK",
    .frequency_default_khz = 433920, .te_min_us = 250, .te_max_us = 450,
    .bit_min = 32, .bit_max = 32, .rolling = false,
};
static const subghz_protocol_info_t roger_info = {
    .name = "Roger", .description = "Roger 28-bit OOK",
    .frequency_default_khz = 433920, .te_min_us = 350, .te_max_us = 650,
    .bit_min = 28, .bit_max = 28, .rolling = false,
};
static const subghz_protocol_info_t mastercode_info = {
    .name = "Mastercode", .description = "Mastercode 36-bit OOK",
    .frequency_default_khz = 433920, .te_min_us = 900, .te_max_us = 1250,
    .bit_min = 36, .bit_max = 36, .rolling = false,
};
static const subghz_protocol_info_t marantec24_info = {
    .name = "Marantec24", .description = "Marantec24 24-bit OOK",
    .frequency_default_khz = 433920, .te_min_us = 650, .te_max_us = 950,
    .bit_min = 24, .bit_max = 24, .rolling = false,
};
static const subghz_protocol_info_t holtek40_info = {
    .name = "Holtek", .description = "Holtek 40-bit OOK doorbell",
    .frequency_default_khz = 433920, .te_min_us = 330, .te_max_us = 530,
    .bit_min = 40, .bit_max = 40, .rolling = false,
};
static const subghz_protocol_info_t phoenix_v2_info = {
    .name = "Phoenix V2", .description = "Phoenix V2 52-bit OOK",
    .frequency_default_khz = 433920, .te_min_us = 330, .te_max_us = 530,
    .bit_min = 52, .bit_max = 52, .rolling = false,
};
static const subghz_protocol_info_t hormann_info = {
    .name = "Hormann", .description = "Hormann 44-bit OOK",
    .frequency_default_khz = 433920, .te_min_us = 400, .te_max_us = 600,
    .bit_min = 44, .bit_max = 44, .rolling = false,
};
static const subghz_protocol_info_t gangqi_info = {
    .name = "Gangqi", .description = "Gangqi 34-bit OOK",
    .frequency_default_khz = 433920, .te_min_us = 400, .te_max_us = 600,
    .bit_min = 34, .bit_max = 34, .rolling = false,
};
static const subghz_protocol_info_t nero_radio_info = {
    .name = "Nero Radio", .description = "Nero Radio 56-bit OOK",
    .frequency_default_khz = 433920, .te_min_us = 150, .te_max_us = 280,
    .bit_min = 56, .bit_max = 56, .rolling = false,
};
static const subghz_protocol_info_t nero_sketch_info = {
    .name = "Nero Sketch", .description = "Nero Sketch 40-bit OOK",
    .frequency_default_khz = 433920, .te_min_us = 250, .te_max_us = 420,
    .bit_min = 40, .bit_max = 40, .rolling = false,
};
static const subghz_protocol_info_t megacode_info = {
    .name = "MegaCode", .description = "Linear MegaCode 24-bit OOK",
    .frequency_default_khz = 315000, .te_min_us = 850, .te_max_us = 1150,
    .bit_min = 24, .bit_max = 24, .rolling = false,
};
static const subghz_protocol_info_t legrand_info = {
    .name = "Legrand", .description = "Legrand 18-bit OOK",
    .frequency_default_khz = 433920, .te_min_us = 280, .te_max_us = 470,
    .bit_min = 18, .bit_max = 18, .rolling = false,
};
static const subghz_protocol_info_t hollarm_info = {
    .name = "Hollarm", .description = "Hollarm 40-bit OOK",
    .frequency_default_khz = 433920, .te_min_us = 150, .te_max_us = 280,
    .bit_min = 40, .bit_max = 40, .rolling = false,
};
static const subghz_protocol_info_t honeywell_info = {
    .name = "Honeywell WDB", .description = "Honeywell wireless doorbell 48-bit OOK",
    .frequency_default_khz = 433920, .te_min_us = 120, .te_max_us = 220,
    .bit_min = 48, .bit_max = 48, .rolling = false,
};

static const subghz_protocol_entry_t protocol_registry[] = {
    { .info = &princeton_info, .decoder_ops = &princeton_decoder_ops,
      .encoder_ops = &princeton_encoder_ops, .decoder_size = sizeof(princeton_priv_t),
      .encoder_size = sizeof(ook_enc_priv_t) },
    { .info = &nice_flo_info, .decoder_ops = &nice_flo_decoder_ops,
      .encoder_ops = &nice_flo_encoder_ops, .decoder_size = sizeof(pwm_priv_t),
      .encoder_size = sizeof(ook_enc_priv_t) },
    { .info = &came_info, .decoder_ops = &came_decoder_ops,
      .encoder_ops = &came_encoder_ops, .decoder_size = sizeof(pwm_priv_t),
      .encoder_size = sizeof(ook_enc_priv_t) },
    { .info = &holtek_info, .decoder_ops = &holtek_decoder_ops,
      .encoder_ops = &holtek_encoder_ops, .decoder_size = sizeof(pwm_priv_t),
      .encoder_size = sizeof(ook_enc_priv_t) },
    { .info = &linear_info, .decoder_ops = &linear_decoder_ops,
      .encoder_ops = &linear_encoder_ops, .decoder_size = sizeof(pwm_priv_t),
      .encoder_size = sizeof(ook_enc_priv_t) },
    { .info = &itv3_info, .decoder_ops = &itv3_decoder_ops,
      .encoder_ops = &itv3_encoder_ops, .decoder_size = sizeof(itv3_priv_t),
      .encoder_size = sizeof(ook_enc_priv_t) },
    { .info = &gate_tx_info, .decoder_ops = &gate_tx_decoder_ops,
      .encoder_ops = &gate_tx_encoder_ops, .decoder_size = sizeof(pwm_priv_t),
      .encoder_size = sizeof(ook_enc_priv_t) },
    { .info = &ansonic_info, .decoder_ops = &ansonic_decoder_ops,
      .encoder_ops = &ansonic_encoder_ops, .decoder_size = sizeof(pwm_priv_t),
      .encoder_size = sizeof(ook_enc_priv_t) },
    { .info = &dooya_info, .decoder_ops = &dooya_decoder_ops,
      .encoder_ops = &dooya_encoder_ops, .decoder_size = sizeof(pwm_priv_t),
      .encoder_size = sizeof(ook_enc_priv_t) },
    { .info = &smc_info, .decoder_ops = &smc_decoder_ops,
      .encoder_ops = &smc_encoder_ops, .decoder_size = sizeof(smc_priv_t),
      .encoder_size = sizeof(ook_enc_priv_t) },
    { .info = &keeloq_info, .decoder_ops = &keeloq_decoder_ops,
      .encoder_ops = NULL, .decoder_size = sizeof(keeloq_priv_t), .encoder_size = 0 },
    { .info = &bett_info, .decoder_ops = &bett_decoder_ops,
      .encoder_ops = &bett_encoder_ops, .decoder_size = sizeof(pwm_priv_t),
      .encoder_size = sizeof(ook_enc_priv_t) },
    { .info = &clemsa_info, .decoder_ops = &clemsa_decoder_ops,
      .encoder_ops = &clemsa_encoder_ops, .decoder_size = sizeof(pwm_priv_t),
      .encoder_size = sizeof(ook_enc_priv_t) },
    { .info = &doitrand_info, .decoder_ops = &doitrand_decoder_ops,
      .encoder_ops = &doitrand_encoder_ops, .decoder_size = sizeof(pwm_priv_t),
      .encoder_size = sizeof(ook_enc_priv_t) },
    { .info = &elplast_info, .decoder_ops = &elplast_decoder_ops,
      .encoder_ops = &elplast_encoder_ops, .decoder_size = sizeof(pwm_priv_t),
      .encoder_size = sizeof(ook_enc_priv_t) },
    { .info = &feron_info, .decoder_ops = &feron_decoder_ops,
      .encoder_ops = &feron_encoder_ops, .decoder_size = sizeof(pwm_priv_t),
      .encoder_size = sizeof(ook_enc_priv_t) },
    { .info = &roger_info, .decoder_ops = &roger_decoder_ops,
      .encoder_ops = &roger_encoder_ops, .decoder_size = sizeof(pwm_priv_t),
      .encoder_size = sizeof(ook_enc_priv_t) },
    { .info = &mastercode_info, .decoder_ops = &mastercode_decoder_ops,
      .encoder_ops = &mastercode_encoder_ops, .decoder_size = sizeof(pwm_priv_t),
      .encoder_size = sizeof(ook_enc_priv_t) },
    { .info = &marantec24_info, .decoder_ops = &marantec24_decoder_ops,
      .encoder_ops = &marantec24_encoder_ops, .decoder_size = sizeof(pwm_priv_t),
      .encoder_size = sizeof(ook_enc_priv_t) },
    { .info = &holtek40_info, .decoder_ops = &holtek40_decoder_ops,
      .encoder_ops = &holtek40_encoder_ops, .decoder_size = sizeof(pwm_priv_t),
      .encoder_size = sizeof(ook_enc_priv_t) },
    { .info = &phoenix_v2_info, .decoder_ops = &phoenix_v2_decoder_ops,
      .encoder_ops = &phoenix_v2_encoder_ops, .decoder_size = sizeof(pwm_priv_t),
      .encoder_size = sizeof(ook_enc_priv_t) },
    { .info = &hormann_info, .decoder_ops = &hormann_decoder_ops,
      .encoder_ops = &hormann_encoder_ops, .decoder_size = sizeof(pwm_priv_t),
      .encoder_size = sizeof(ook_enc_priv_t) },
    { .info = &gangqi_info, .decoder_ops = &gangqi_decoder_ops,
      .encoder_ops = &gangqi_encoder_ops, .decoder_size = sizeof(pwm_priv_t),
      .encoder_size = sizeof(ook_enc_priv_t) },
    { .info = &nero_radio_info, .decoder_ops = &nero_radio_decoder_ops,
      .encoder_ops = &nero_radio_encoder_ops, .decoder_size = sizeof(nero_priv_t),
      .encoder_size = sizeof(ook_enc_priv_t) },
    { .info = &nero_sketch_info, .decoder_ops = &nero_sketch_decoder_ops,
      .encoder_ops = &nero_sketch_encoder_ops, .decoder_size = sizeof(nsketch_priv_t),
      .encoder_size = sizeof(ook_enc_priv_t) },
    { .info = &megacode_info, .decoder_ops = &megacode_decoder_ops,
      .encoder_ops = &megacode_encoder_ops, .decoder_size = sizeof(megacode_priv_t),
      .encoder_size = sizeof(ook_enc_priv_t) },
    { .info = &legrand_info, .decoder_ops = &legrand_decoder_ops,
      .encoder_ops = &legrand_encoder_ops, .decoder_size = sizeof(legrand_priv_t),
      .encoder_size = sizeof(ook_enc_priv_t) },
    { .info = &hollarm_info, .decoder_ops = &hollarm_decoder_ops,
      .encoder_ops = &hollarm_encoder_ops, .decoder_size = sizeof(hollarm_priv_t),
      .encoder_size = sizeof(ook_enc_priv_t) },
    { .info = &honeywell_info, .decoder_ops = &honeywell_decoder_ops,
      .encoder_ops = &honeywell_encoder_ops, .decoder_size = sizeof(honeywell_priv_t),
      .encoder_size = sizeof(ook_enc_priv_t) },
};

#define PROTOCOL_COUNT (sizeof(protocol_registry) / sizeof(protocol_registry[0]))

const subghz_protocol_entry_t *subghz_protocol_find(const char *name) {
    if (!name) {
        return NULL;
    }
    for (uint32_t i = 0; i < PROTOCOL_COUNT; i++) {
        if (strcmp(protocol_registry[i].info->name, name) == 0) {
            return &protocol_registry[i];
        }
    }
    return NULL;
}

const subghz_protocol_entry_t *subghz_protocol_find_by_index(uint32_t index) {
    if (index >= PROTOCOL_COUNT) {
        return NULL;
    }
    return &protocol_registry[index];
}

uint32_t subghz_protocol_count(void) {
    return PROTOCOL_COUNT;
}

uint32_t subghz_protocol_count_static(void) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < PROTOCOL_COUNT; i++) {
        if (!protocol_registry[i].info->rolling) {
            n++;
        }
    }
    return n;
}

subghz_protocol_decoder_t *subghz_decoder_alloc(const subghz_protocol_entry_t *entry) {
    if (!entry || !entry->decoder_ops) {
        return NULL;
    }
    subghz_protocol_decoder_t *d = malloc(sizeof(*d) + entry->decoder_size);
    if (!d) {
        return NULL;
    }
    memset(d, 0, sizeof(*d) + entry->decoder_size);
    d->ops = entry->decoder_ops;
    d->info = entry->info;
    d->priv = (uint8_t *)d + sizeof(*d);
    return d;
}

void subghz_decoder_free(subghz_protocol_decoder_t *decoder) {
    free(decoder);
}

subghz_protocol_status_t subghz_decoder_feed(
    subghz_protocol_decoder_t *decoder, bool level, uint32_t duration_us) {
    if (!decoder || !decoder->ops || !decoder->ops->feed) {
        return SUBGHZ_PROTOCOL_ERROR;
    }
    return decoder->ops->feed(decoder, level, duration_us);
}

void subghz_decoder_reset(subghz_protocol_decoder_t *decoder) {
    if (decoder && decoder->ops && decoder->ops->reset) {
        decoder->ops->reset(decoder);
    }
}

const char *subghz_decoder_get_data(
    subghz_protocol_decoder_t *decoder, char *buffer, size_t size) {
    if (!decoder || !decoder->ops || !decoder->ops->get_data || !buffer || !size) {
        return NULL;
    }
    return decoder->ops->get_data(decoder, buffer, size);
}

const char *subghz_decoder_get_string(
    subghz_protocol_decoder_t *decoder, char *buffer, size_t size) {
    if (!decoder || !decoder->ops || !buffer || !size) {
        return NULL;
    }
    if (decoder->ops->get_string) {
        return decoder->ops->get_string(decoder, buffer, size);
    }
    return decoder->ops->get_data ? decoder->ops->get_data(decoder, buffer, size) : NULL;
}

subghz_protocol_encoder_t *subghz_encoder_alloc(const subghz_protocol_entry_t *entry) {
    if (!entry || !entry->encoder_ops) {
        return NULL;
    }
    subghz_protocol_encoder_t *e = malloc(sizeof(*e) + entry->encoder_size);
    if (!e) {
        return NULL;
    }
    memset(e, 0, sizeof(*e) + entry->encoder_size);
    e->ops = entry->encoder_ops;
    e->info = entry->info;
    e->priv = (uint8_t *)e + sizeof(*e);
    return e;
}

void subghz_encoder_free(subghz_protocol_encoder_t *encoder) {
    free(encoder);
}

subghz_protocol_status_t subghz_encoder_feed(
    subghz_protocol_encoder_t *encoder, uint64_t data, uint32_t bits) {
    if (!encoder || !encoder->ops || !encoder->ops->feed) {
        return SUBGHZ_PROTOCOL_ERROR;
    }
    return encoder->ops->feed(encoder, data, bits);
}

bool subghz_encoder_yield(
    subghz_protocol_encoder_t *encoder, bool *level, uint32_t *duration_us) {
    if (!encoder || !encoder->ops || !encoder->ops->yield || !level || !duration_us) {
        return false;
    }
    return encoder->ops->yield(encoder, level, duration_us);
}

void subghz_encoder_reset(subghz_protocol_encoder_t *encoder) {
    if (encoder && encoder->ops && encoder->ops->reset) {
        encoder->ops->reset(encoder);
    }
}

void subghz_protocols_init(void) {
}

/* Keystore hook storage (set by subghz_keystore.c). */
static const void *keeloq_ks;

const void *subghz_keeloq_keystore_get(void) {
    return keeloq_ks;
}

void subghz_keeloq_keystore_set(const void *ks) {
    keeloq_ks = ks;
}

const char *subghz_keeloq_last_manufacture(void) {
    return keeloq_last_analysis.manufacturer ? keeloq_last_analysis.manufacturer : "Unknown";
}

uint16_t subghz_keeloq_last_counter(void) {
    return keeloq_last_analysis.counter;
}
