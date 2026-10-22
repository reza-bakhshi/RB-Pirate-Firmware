/* SubGHz protocol library: decoders + encoders for static OOK protocols
 * plus decode-only Keeloq dynamic support.
 *
 * Timing constants verified against Flipper Zero firmware
 * lib/subghz/protocols (dev branch): princeton, nice_flo, came,
 * holtek_ht12x, linear, intertechno_v3, gate_tx, dooya, smc5326,
 * ansonic, keeloq, dickert_mahs, linear_delta3, revers_rb2,
 * power_smart, marantec, chamberlain_code (plus the shared
 * lib/toolbox Manchester codec), somfy_telis, somfy_keytis,
 * hay21, magellan, holtek_ht12x, came_twee, faac_slh, scher_khan,
 * ido, kia.
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
 * RcSwitch protocol (Protocols 1, 2, and 11)
 * ============================================================ */

typedef struct {
    uint32_t high_te;
    uint32_t te_us;
    uint8_t proto;
    bool have_high;
    bool decoding;
} rcswitch_priv_t;

static subghz_protocol_status_t rcswitch_feed(
    subghz_protocol_decoder_t *d, bool level, uint32_t dur) {
    rcswitch_priv_t *p = d->priv;
    if (level) {
        p->high_te = dur;
        p->have_high = true;
        return SUBGHZ_PROTOCOL_SYNC;
    }
    if (!p->have_high) {
        return SUBGHZ_PROTOCOL_SYNC;
    }
    p->have_high = false;
    if (!p->decoding) {
        /* Sync: high is ~1 TE (100-1500us), low is sync_lo:
         * Proto 1:  sync_lo = 31 TE (ratio 26..36)
         * Proto 11: sync_lo = 23 TE (ratio 19..25)
         * Proto 2:  sync_lo = 10 TE (ratio 8..14) */
        if (p->high_te >= 100u && p->high_te <= 1500u) {
            uint32_t ratio = dur / p->high_te;
            if (ratio >= 26u && ratio <= 36u) {
                p->proto = 1;
                p->decoding = true;
            } else if (ratio >= 19u && ratio <= 25u) {
                p->proto = 11;
                p->decoding = true;
            } else if (ratio >= 8u && ratio <= 14u) {
                p->proto = 2;
                p->decoding = true;
            }
            if (p->decoding) {
                p->te_us = p->high_te;
                d->decoded_data = 0;
                d->decoded_bits = 0;
                d->te_us = p->te_us;
            }
        }
        return SUBGHZ_PROTOCOL_SYNC;
    }

    if (!p->te_us) return SUBGHZ_PROTOCOL_SYNC;
    uint32_t h = subghz_te_count(p->high_te, p->te_us);
    uint32_t l = subghz_te_count(dur, p->te_us);

    /* Check for trailing frame sync / inter-frame gap */
    uint32_t min_gap_ratio = (p->proto == 2) ? 7u : 15u;
    if (h <= 2u && l >= min_gap_ratio) {
        if (d->decoded_bits >= 12u) {
            p->decoding = false;
            decoder_finish(d);
            return SUBGHZ_PROTOCOL_DONE;
        }
        d->decoded_data = 0;
        d->decoded_bits = 0;
        p->te_us = p->high_te;
        d->te_us = p->te_us;
        return SUBGHZ_PROTOCOL_SYNC;
    }

    if (p->proto == 1) {
        /* Proto 1: 0 = 1h:3l, 1 = 3h:1l */
        if (h >= 2u && h <= 4u && l == 1u) {
            decoder_add_bit(d, 1);
        } else if (h == 1u && l >= 2u && l <= 4u) {
            decoder_add_bit(d, 0);
        } else {
            p->decoding = false;
            d->decoded_data = 0;
            d->decoded_bits = 0;
        }
    } else {
        /* Proto 2 or 11: 0 = 1h:2l, 1 = 2h:1l */
        if (h >= 2u && h <= 3u && l == 1u) {
            decoder_add_bit(d, 1);
        } else if (h == 1u && l >= 2u && l <= 3u) {
            decoder_add_bit(d, 0);
        } else {
            p->decoding = false;
            d->decoded_data = 0;
            d->decoded_bits = 0;
        }
    }
    return SUBGHZ_PROTOCOL_SYNC;
}

static void rcswitch_reset(subghz_protocol_decoder_t *d) {
    memset(d->priv, 0, sizeof(rcswitch_priv_t));
    d->decoded_data = 0;
    d->decoded_bits = 0;
    d->te_us = 0;
}

static const char *rcswitch_get_data(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->decoded_bits < 12u) return NULL;
    rcswitch_priv_t *p = d->priv;
    snprintf(b, s, "RcSwitch P%u %ubit: 0x%llx", (unsigned)(p->proto ? p->proto : 1),
             (unsigned)d->decoded_bits, (unsigned long long)d->decoded_data);
    return b;
}

static const char *rcswitch_get_string(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->decoded_bits < 12u) return NULL;
    rcswitch_priv_t *p = d->priv;
    snprintf(b, s, "RcSwitch P%u %ubit\r\nKey:0x%llX\r\nTE:%luus",
             (unsigned)(p->proto ? p->proto : 1), (unsigned)d->decoded_bits,
             (unsigned long long)d->decoded_data, (unsigned long)d->te_us);
    return b;
}

static const subghz_decoder_ops_t rcswitch_decoder_ops = {
    .feed = rcswitch_feed, .reset = rcswitch_reset, .get_data = rcswitch_get_data,
    .get_string = rcswitch_get_string,
};

static subghz_protocol_status_t rcswitch_encode(
    subghz_protocol_encoder_t *e, uint64_t data, uint32_t bits) {
    rcswitch_enc_priv_t *p = e->priv;
    uint8_t proto = (p && p->proto) ? p->proto : 1;
    uint32_t te = (p && p->te_us) ? p->te_us : (e->te_us ? e->te_us : (proto == 2 ? 650u : 350u));
    if (!bits || bits > 64u) return SUBGHZ_PROTOCOL_ERROR;

    uint32_t sync_hi, sync_lo, zero_hi, zero_lo, one_hi, one_lo;
    if (proto == 2) {
        sync_hi = te; sync_lo = te * 10u;
        zero_hi = te; zero_lo = te * 2u;
        one_hi = te * 2u; one_lo = te;
    } else if (proto == 11) {
        sync_hi = te; sync_lo = te * 23u;
        zero_hi = te; zero_lo = te * 2u;
        one_hi = te * 2u; one_lo = te;
    } else {
        proto = 1;
        sync_hi = te; sync_lo = te * 31u;
        zero_hi = te; zero_lo = te * 3u;
        one_hi = te * 3u; one_lo = te;
    }

    subghz_upload_reset(&e->upload);
    e->te_us = te;
    subghz_upload_add(&e->upload, true, sync_hi);
    subghz_upload_add(&e->upload, false, sync_lo);
    for (int i = (int)bits - 1; i >= 0; i--) {
        if ((data >> i) & 1ull) {
            subghz_upload_add(&e->upload, true, one_hi);
            subghz_upload_add(&e->upload, false, one_lo);
        } else {
            subghz_upload_add(&e->upload, true, zero_hi);
            subghz_upload_add(&e->upload, false, zero_lo);
        }
    }
    subghz_upload_add(&e->upload, true, sync_hi);
    subghz_upload_add(&e->upload, false, sync_lo);
    return SUBGHZ_PROTOCOL_DONE;
}

static const subghz_encoder_ops_t rcswitch_encoder_ops = {
    .feed = rcswitch_encode, .yield = encoder_yield_generic, .reset = encoder_reset_generic,
};

bool subghz_rcswitch_set_proto(subghz_protocol_encoder_t *encoder, uint8_t proto) {
    if (!encoder || !encoder->priv) return false;
    if (proto != 1 && proto != 2 && proto != 11) return false;
    rcswitch_enc_priv_t *p = encoder->priv;
    p->proto = proto;
    if (!p->te_us) {
        p->te_us = (proto == 2) ? 650u : 350u;
    }
    return true;
}

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

static const pwm_const_t dickert_const = {
    /* Flipper dickert_mahs: te 400/800/100, header LOW 50*long, start H 1TE,
     * bits low-first 1 = L2/H1, 0 = L1/H2, fixed 36 bits (tri-state DIPs). */
    .te_short = 400, .te_long = 800, .te_delta = 100,
    .header_mult = 112, .header_delta_mult = 70,
    .start_high = 1, .start_low = 0, .end_gap_mult = 20,
    .min_bits = 36, .max_bits = 36,
    .h_for_one = 1, .l_for_one = 2, .h_for_zero = 2, .l_for_zero = 1,
    .bit_from_high = false, .invert_data = false,
};

static const pwm_const_t hay21_const = {
    /* Flipper hay21: te 300/700/150, gap LOW 6*long, no start bit,
     * bits high-first 1 = H2/L1, 0 = H1/L2, 21 bits, rolling. */
    .te_short = 300, .te_long = 700, .te_delta = 150,
    .header_mult = 14, .header_delta_mult = 3,
    .start_high = 0, .start_low = 0, .end_gap_mult = 10,
    .min_bits = 21, .max_bits = 21,
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
PWM_PROTOCOL_SIMPLE(dickert, dickert_const, "Dickert_MAHS", "", 433920)
/* Hay21 is decode-only (rolling); no encoder row on purpose. */
PWM_PROTOCOL_SIMPLE(hay21, hay21_const, "Hay21", "", 433920)

PWM_ENCODER_SIMPLE(gate_tx, gate_tx_const, 350, true)
PWM_ENCODER_SIMPLE(ansonic, ansonic_const, 555, true)
PWM_ENCODER_SIMPLE(dooya, dooya_const, 366, true)
PWM_ENCODER_SIMPLE(nice_flo, nice_flo_const, 700, true)
PWM_ENCODER_SIMPLE(came, came_const, 320, true)
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
PWM_ENCODER_SIMPLE(dickert, dickert_const, 400, true)

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

static const char *hay21_get_string(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->decoded_bits != 21u) return NULL;
    uint32_t v = (uint32_t)(d->decoded_data & 0x1fffffu);
    uint8_t btn = (uint8_t)((v >> 13) & 0xffu);
    uint8_t serial = (uint8_t)((v >> 5) & 0xffu);
    uint8_t cnt = (uint8_t)((v >> 1) & 0xfu);
    const char *btn_name = "Unknown";
    if (btn == 0x5au) btn_name = "On/Off";
    else if (btn == 0xc3u) btn_name = "Mode";
    else if (btn == 0x88u) btn_name = "Hold";
    snprintf(b, s, "Hay21 21bit\r\nKey: 0x%06lX\r\nSerial: 0x%02X\r\nBtn: 0x%02X - %s\r\nCnt: 0x%X",
             (unsigned long)v, serial, btn, btn_name, cnt);
    return b;
}

static const char *dickert_get_string(subghz_protocol_decoder_t *d, char *b, size_t s) {
    /* Flipper dickert_mahs renders the 36-bit payload as 18 tri-state DIP
     * symbols (01 = "0", 00 = "-", 11 = "+", else "?"): 10 user DIPs + 8
     * factory-code symbols. */
    if (d->decoded_bits != 36u) return NULL;
    uint64_t data = d->decoded_data;
    uint8_t bits[36];
    for (int i = 35; i >= 0; i--) {
        bits[i] = (uint8_t)(data & 1u);
        data >>= 1;
    }
    char code[19];
    for (size_t i = 0; i < 18; i++) {
        uint8_t dip = (uint8_t)((bits[i * 2u] << 1) | bits[i * 2u + 1u]);
        code[i] = dip == 0x01u ? '0' : dip == 0x00u ? '-' : dip == 0x03u ? '+' : '?';
    }
    code[18] = '\0';
    snprintf(b, s, "Dickert_MAHS 36bit\r\nUser-Dips:\t%.10s\r\nFac-Code:\t%.8s", code,
             code + 10);
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
PWM_DECODER_OPS(dickert)
PWM_DECODER_OPS(hay21)

/* ============================================================
 * LINEAR DELTA3 (Moore-O-Matic dip-switch): te 500/2000/150, 8 bits.
 * No TX header: the previous frame's guard gap (LOW 70TE) is the RX
 * header. Bits high-first: 1 = H1TE+L7TE, 0 = H4TE+L4TE. The last
 * bit's low is the 70/73TE guard, which terminates the frame and
 * recovers the last bit from the preceding high pulse. Like Flipper,
 * a frame is accepted only after two identical consecutive frames.
 * ============================================================ */

typedef struct {
    uint8_t step;
    uint32_t te_last;
    uint64_t last_data;
} delta3_priv_t;

enum { D3_RESET = 0, D3_SAVE, D3_CHECK };

#define D3_TE 500u
#define D3_LONG 2000u
#define D3_D 150u
#define D3_BITS 8u

static subghz_protocol_status_t delta3_feed(
    subghz_protocol_decoder_t *d, bool level, uint32_t dur) {
    delta3_priv_t *p = d->priv;
    switch (p->step) {
    case D3_RESET:
        if (!level && subghz_duration_match(dur, D3_TE * 70u, D3_D * 24u)) {
            p->step = D3_SAVE;
            d->decoded_data = 0;
            d->decoded_bits = 0;
            d->te_us = D3_TE;
        }
        break;
    case D3_SAVE:
        if (level) {
            p->te_last = dur;
            p->step = D3_CHECK;
        } else {
            p->step = D3_RESET;
        }
        break;
    case D3_CHECK:
        if (!level) {
            if (dur >= D3_TE * 10u) {
                /* Guard gap: recover the last bit from the high pulse. */
                p->step = D3_RESET;
                if (subghz_duration_match(p->te_last, D3_TE, D3_D)) {
                    decoder_add_bit(d, 1);
                } else if (subghz_duration_match(p->te_last, D3_LONG, D3_D)) {
                    decoder_add_bit(d, 0);
                } else {
                    d->decoded_data = 0;
                    d->decoded_bits = 0;
                    break;
                }
                if (d->decoded_bits == D3_BITS) {
                    if (p->last_data == d->decoded_data && p->last_data) {
                        p->last_data = 0; /* Debounce: re-arm for the next pair. */
                        decoder_finish(d);
                        return SUBGHZ_PROTOCOL_DONE;
                    }
                    p->last_data = d->decoded_data;
                }
                /* Re-arm on the guard gap: it is also the next header. */
                d->decoded_data = 0;
                d->decoded_bits = 0;
                p->step = D3_SAVE;
                break;
            }
            if (subghz_duration_match(p->te_last, D3_TE, D3_D) &&
                subghz_duration_match(dur, D3_TE * 7u, D3_D)) {
                decoder_add_bit(d, 1);
                p->step = D3_SAVE;
            } else if (
                subghz_duration_match(p->te_last, D3_LONG, D3_D) &&
                subghz_duration_match(dur, D3_LONG, D3_D)) {
                decoder_add_bit(d, 0);
                p->step = D3_SAVE;
            } else {
                p->step = D3_RESET;
            }
        } else {
            p->step = D3_RESET;
        }
        break;
    default:
        p->step = D3_RESET;
        break;
    }
    return SUBGHZ_PROTOCOL_SYNC;
}

static void delta3_reset(subghz_protocol_decoder_t *d) {
    memset(d->priv, 0, sizeof(delta3_priv_t));
    d->decoded_data = 0;
    d->decoded_bits = 0;
    d->hash = 0;
    d->te_us = 0;
}

static const char *delta3_get_data(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->decoded_bits != D3_BITS) {
        return NULL;
    }
    snprintf(b, s, "LinearDelta3 %ubit: 0x%02lX", d->decoded_bits,
             (unsigned long)(d->decoded_data & 0xffu));
    return b;
}

static const char *delta3_get_string(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->decoded_bits != D3_BITS) {
        return NULL;
    }
    uint8_t v = (uint8_t)(d->decoded_data & 0xffu);
    char dip[9];
    for (int i = 7; i >= 0; i--) {
        dip[7 - i] = ((v >> i) & 1u) ? '+' : '-';
    }
    dip[8] = '\0';
    snprintf(b, s, "LinearDelta3 %ubit\r\nKey:0x%02X\r\nDIP:%s", d->decoded_bits, v, dip);
    return b;
}

static const subghz_decoder_ops_t delta3_decoder_ops = {
    .feed = delta3_feed, .reset = delta3_reset, .get_data = delta3_get_data,
    .get_string = delta3_get_string,
};

static subghz_protocol_status_t delta3_encode(
    subghz_protocol_encoder_t *e, uint64_t data, uint32_t bits) {
    if (bits != D3_BITS || (data >> D3_BITS)) {
        return SUBGHZ_PROTOCOL_ERROR;
    }
    subghz_upload_reset(&e->upload);
    e->te_us = D3_TE;
    for (int i = (int)D3_BITS - 1; i >= 1; i--) {
        if ((data >> i) & 1ull) {
            subghz_upload_add(&e->upload, true, D3_TE);
            subghz_upload_add(&e->upload, false, D3_TE * 7u);
        } else {
            subghz_upload_add(&e->upload, true, D3_LONG);
            subghz_upload_add(&e->upload, false, D3_LONG);
        }
    }
    /* Last bit: the trailing low is the inter-frame guard. */
    if (data & 1ull) {
        subghz_upload_add(&e->upload, true, D3_TE);
        subghz_upload_add(&e->upload, false, D3_TE * 73u);
    } else {
        subghz_upload_add(&e->upload, true, D3_LONG);
        subghz_upload_add(&e->upload, false, D3_TE * 70u);
    }
    return SUBGHZ_PROTOCOL_DONE;
}

static const subghz_encoder_ops_t delta3_encoder_ops = {
    .feed = delta3_encode, .yield = encoder_yield_generic, .reset = encoder_reset_generic,
};

/* ============================================================
 * Manchester helpers (Flipper lib/toolbox manchester_{decoder,encoder}).
 * ============================================================ */

typedef enum {
    MAN_EV_SHORT_LOW = 0,
    MAN_EV_SHORT_HIGH = 2,
    MAN_EV_LONG_LOW = 4,
    MAN_EV_LONG_HIGH = 6,
    MAN_EV_RESET = 8,
} man_event_t;

typedef enum {
    MAN_ST_START1 = 0,
    MAN_ST_MID1 = 1,
    MAN_ST_MID0 = 2,
    MAN_ST_START0 = 3,
} man_state_t;

static const uint8_t man_transitions[] = { 0b00000001, 0b10010001, 0b10011011, 0b11111011 };

static bool man_advance(man_state_t state, man_event_t event, man_state_t *next, bool *data) {
    man_state_t ns;
    bool result = false;
    if (event == MAN_EV_RESET) {
        ns = MAN_ST_MID1;
    } else {
        ns = (man_state_t)((man_transitions[state] >> event) & 0x3u);
        if (ns == state) {
            ns = MAN_ST_MID1;
        } else if (ns == MAN_ST_MID0) {
            if (data) *data = false;
            result = true;
        } else if (ns == MAN_ST_MID1) {
            if (data) *data = true;
            result = true;
        }
    }
    *next = ns;
    return result;
}

typedef struct {
    bool prev_bit;
    uint8_t step;
} man_enc_t;

typedef enum {
    MAN_R_SHORT_LOW = 0,
    MAN_R_LONG_LOW = 1,
    MAN_R_LONG_HIGH = 2,
    MAN_R_SHORT_HIGH = 3,
} man_result_t;

static bool man_enc_advance(man_enc_t *s, bool curr, man_result_t *r) {
    bool advance = false;
    switch (s->step) {
    case 0:
        s->prev_bit = curr;
        *r = curr ? MAN_R_SHORT_LOW : MAN_R_SHORT_HIGH;
        s->step = 1;
        advance = true;
        break;
    case 1:
        *r = (man_result_t)((s->prev_bit << 1) | (curr & 1u));
        if (curr == s->prev_bit) {
            s->step = 2;
        } else {
            s->prev_bit = curr;
            advance = true;
        }
        break;
    case 2:
        *r = curr ? MAN_R_SHORT_LOW : MAN_R_SHORT_HIGH;
        s->prev_bit = curr;
        s->step = 1;
        advance = true;
        break;
    default:
        break;
    }
    return advance;
}

static man_result_t man_enc_finish(man_enc_t *s) {
    s->step = 0;
    return (man_result_t)((s->prev_bit << 1) | (s->prev_bit & 1u));
}

/* Upload append that merges consecutive same-level runs (as seen on air).
 * Used for run-based encodings (Chamberlain bit slots, inter-frame gaps)
 * where the decoder classifies merged durations. Manchester encoders below
 * intentionally keep Flipper's separate entries; the air merge happens in
 * TX physics (and in tests via air-merge of the pulse stream). */
static bool man_upload_add(subghz_upload_t *u, bool level, uint32_t dur) {
    if (u->count && u->items[u->count - 1u].level == level) {
        uint64_t sum = (uint64_t)u->items[u->count - 1u].duration_us + dur;
        if (!sum || sum > 60000000ull) {
            return false;
        }
        u->items[u->count - 1u].duration_us = (uint32_t)sum;
        return true;
    }
    return subghz_upload_add(u, level, dur);
}

static bool man_result_add(
    subghz_upload_t *u, man_result_t r, uint32_t te_short, uint32_t te_long) {
    /* No run merging here: mirrors the Flipper upload entry-for-entry.
     * Back-to-back same-level entries merge on air (and in air-merge
     * test feeds) into the Long durations the decoder classifies. */
    switch (r) {
    case MAN_R_SHORT_LOW:
        return subghz_upload_add(u, false, te_short);
    case MAN_R_LONG_LOW:
        return subghz_upload_add(u, false, te_long);
    case MAN_R_LONG_HIGH:
        return subghz_upload_add(u, true, te_long);
    case MAN_R_SHORT_HIGH:
        return subghz_upload_add(u, true, te_short);
    default:
        return false;
    }
}

/* Manchester-encode <bits> of <data> MSB-first (single frame). */
static bool man_encode_bits(subghz_upload_t *u, uint64_t data, uint32_t bits, bool invert,
                            uint32_t te_short, uint32_t te_long) {
    man_enc_t st = { false, 0 };
    man_result_t r;
    for (uint32_t i = bits; i > 0; i--) {
        bool b = ((data >> (i - 1u)) & 1ull) != 0;
        if (invert) {
            b = !b;
        }
        if (!man_enc_advance(&st, b, &r)) {
            if (!man_result_add(u, r, te_short, te_long)) {
                return false;
            }
            if (!man_enc_advance(&st, b, &r)) {
                return false;
            }
        }
        if (!man_result_add(u, r, te_short, te_long)) {
            return false;
        }
    }
    return man_result_add(u, man_enc_finish(&st), te_short, te_long);
}

/* ============================================================
 * REVERS RB2: te 250/500/160, Manchester, 64 bits.
 * Sync LOW ~600us, then 4 alternating 250us sync pulses (decoded
 * data is pre-seeded with 0xF), then Manchester data. A frame is
 * valid when the top byte is 0xFF and the low 10 bits are 0x200.
 *
 * NOTE (matches upstream): the event-based Manchester decoder drops
 * the data bit of a transition that arrives as a merged Long while
 * in a Start state, so encoder->decoder loopback of a single synthetic
 * frame is phase-sensitive and generally does not complete. The TX
 * upload is entry-for-entry identical to Flipper's (textbook-decodable
 * by any G.E. Thomas receiver); the RX path mirrors upstream exactly
 * and accumulates long exact runs on real captures. Host tests cover
 * TX via textbook decode, RX via deterministic spot checks, and
 * rejection of corrupt frames.
 * ============================================================ */

typedef struct {
    uint8_t step;
    uint32_t te_last;
    uint8_t header_count;
    man_state_t man;
} rb2_priv_t;

enum { RB2_RESET = 0, RB2_HEADER, RB2_DATA };

#define RB2_SHORT 250u
#define RB2_LONG 500u
#define RB2_D 160u
#define RB2_BITS 64u

static void rb2_addbit(subghz_protocol_decoder_t *d, bool bit) {
    d->decoded_data = (d->decoded_data << 1) | (bit & 1u);
    d->decoded_bits++;
    if (d->decoded_bits >= RB2_BITS + 1u) {
        d->decoded_data = 0;
        d->decoded_bits = 0;
        return;
    }
    if (d->decoded_bits < RB2_BITS) {
        return;
    }
    if (((d->decoded_data >> 48) & 0xffu) == 0xffu && (d->decoded_data & 0x3ffu) == 0x200u) {
        decoder_finish(d);
    } else {
        d->decoded_data = 0;
        d->decoded_bits = 0;
    }
}

static subghz_protocol_status_t rb2_feed(
    subghz_protocol_decoder_t *d, bool level, uint32_t dur) {
    rb2_priv_t *p = d->priv;
    man_event_t event = MAN_EV_RESET;
    switch (p->step) {
    case RB2_RESET:
        if (!level && subghz_duration_match(dur, 600u, RB2_D)) {
            p->step = RB2_HEADER;
            d->decoded_data = 0;
            d->decoded_bits = 0;
            d->te_us = RB2_SHORT;
            p->header_count = 0;
            p->te_last = 0;
            man_advance(p->man, MAN_EV_RESET, &p->man, NULL);
        }
        break;
    case RB2_HEADER:
        if (subghz_duration_match(dur, RB2_SHORT, RB2_D)) {
            if (p->te_last != (uint32_t)level) {
                p->header_count++;
            }
            p->te_last = (uint32_t)level;
        } else {
            p->header_count = 0;
            p->te_last = 0;
            p->step = RB2_RESET;
            break;
        }
        if (p->header_count == 4u) {
            p->header_count = 0;
            d->decoded_data = 0xfu;
            d->decoded_bits = 4u;
            p->step = RB2_DATA;
        }
        break;
    case RB2_DATA:
        if (!level) {
            if (subghz_duration_match(dur, RB2_SHORT, RB2_D)) {
                event = MAN_EV_SHORT_LOW;
            } else if (subghz_duration_match(dur, RB2_LONG, RB2_D)) {
                event = MAN_EV_LONG_LOW;
            } else {
                p->step = RB2_RESET;
            }
        } else {
            if (subghz_duration_match(dur, RB2_SHORT, RB2_D)) {
                event = MAN_EV_SHORT_HIGH;
            } else if (subghz_duration_match(dur, RB2_LONG, RB2_D)) {
                event = MAN_EV_LONG_HIGH;
            } else {
                p->step = RB2_RESET;
            }
        }
        if (event != MAN_EV_RESET) {
            bool bit = false;
            if (man_advance(p->man, event, &p->man, &bit)) {
                rb2_addbit(d, bit);
                /* rb2_addbit leaves decoded_bits == 64 only for a valid
                 * frame (hash set); anything else is cleared/accumulating. */
                if (d->decoded_bits == RB2_BITS) {
                    return SUBGHZ_PROTOCOL_DONE;
                }
            }
        }
        break;
    default:
        p->step = RB2_RESET;
        break;
    }
    return SUBGHZ_PROTOCOL_SYNC;
}

static void rb2_reset(subghz_protocol_decoder_t *d) {
    memset(d->priv, 0, sizeof(rb2_priv_t));
    d->decoded_data = 0;
    d->decoded_bits = 0;
    d->hash = 0;
    d->te_us = 0;
    ((rb2_priv_t *)d->priv)->man = MAN_ST_MID1;
}

static const char *rb2_get_data(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->decoded_bits != RB2_BITS) {
        return NULL;
    }
    snprintf(b, s, "Revers_RB2 %ubit: 0x%08lx%08lx", d->decoded_bits,
             (unsigned long)(d->decoded_data >> 32), (unsigned long)d->decoded_data);
    return b;
}

static const char *rb2_get_string(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->decoded_bits != RB2_BITS) {
        return NULL;
    }
    uint64_t v = d->decoded_data;
    uint32_t serial = (uint32_t)((v & 0xffffu) >> 10);
    snprintf(b, s, "Revers_RB2 %ubit\r\nKey:%lX%08lX\r\nSn:0x%08lX", d->decoded_bits,
             (unsigned long)(uint32_t)(v >> 32), (unsigned long)(uint32_t)v,
             (unsigned long)serial);
    return b;
}

static const subghz_decoder_ops_t rb2_decoder_ops = {
    .feed = rb2_feed, .reset = rb2_reset, .get_data = rb2_get_data,
    .get_string = rb2_get_string,
};

static subghz_protocol_status_t rb2_encode(
    subghz_protocol_encoder_t *e, uint64_t data, uint32_t bits) {
    if (bits != RB2_BITS) {
        return SUBGHZ_PROTOCOL_ERROR;
    }
    subghz_upload_reset(&e->upload);
    e->te_us = RB2_SHORT;
    if (!man_encode_bits(&e->upload, data, bits, false, RB2_SHORT, RB2_LONG)) {
        return SUBGHZ_PROTOCOL_ERROR;
    }
    /* Inter-frame gap (merges with the next frame's opening low on air into
     * the ~600us sync pulse the decoder looks for). */
    man_upload_add(&e->upload, false, 320u);
    return SUBGHZ_PROTOCOL_DONE;
}

static const subghz_encoder_ops_t rb2_encoder_ops = {
    .feed = rb2_encode, .yield = encoder_yield_generic, .reset = encoder_reset_generic,
};

/* ============================================================
 * POWER SMART: te 225/450/100, Manchester, 64 bits, no header.
 * Decoded bits are inverted (!manchester). A frame is valid when
 * the sync words match (0xFD......0xAA......) and the complementary
 * data/check fields agree (chek_valid).
 *
 * NOTE (matches upstream): same event-phase caveat as Revers_RB2 —
 * single-frame encoder->decoder loopback is phase-sensitive, so host
 * tests cover TX via textbook decode, RX via deterministic spot
 * checks, and rejection of corrupt frames.
 * ============================================================ */

typedef struct {
    man_state_t man;
} ps_priv_t;

#define PS_SHORT 225u
#define PS_LONG 450u
#define PS_D 100u
#define PS_BITS 64u
#define PS_HEADER 0xfd000000aa000000ull
#define PS_HEADER_MASK 0xff000000ff000000ull

static bool powersmart_valid(uint64_t packet) {
    uint32_t data_1 = (uint32_t)((packet >> 40) & 0xffffu);
    uint32_t data_2 = (uint32_t)((~packet >> 8) & 0xffffu);
    uint8_t data_3 = (uint8_t)(packet >> 32) & 0xffu;
    uint8_t data_4 = (uint8_t)(((~packet) & 0xffu) - 1u);
    return (data_1 == data_2) && (data_3 == data_4);
}

static subghz_protocol_status_t powersmart_feed(
    subghz_protocol_decoder_t *d, bool level, uint32_t dur) {
    ps_priv_t *p = d->priv;
    man_event_t event = MAN_EV_RESET;
    if (!level) {
        if (subghz_duration_match(dur, PS_SHORT, PS_D)) {
            event = MAN_EV_SHORT_LOW;
        } else if (subghz_duration_match(dur, PS_LONG, PS_D * 2u)) {
            event = MAN_EV_LONG_LOW;
        }
    } else {
        if (subghz_duration_match(dur, PS_SHORT, PS_D)) {
            event = MAN_EV_SHORT_HIGH;
        } else if (subghz_duration_match(dur, PS_LONG, PS_D * 2u)) {
            event = MAN_EV_LONG_HIGH;
        }
    }
    if (event != MAN_EV_RESET) {
        bool bit = false;
        if (man_advance(p->man, event, &p->man, &bit)) {
            d->decoded_data = (d->decoded_data << 1) | ((bit ? 0u : 1u));
            d->decoded_bits++;
            d->te_us = PS_SHORT;
            if ((d->decoded_data & PS_HEADER_MASK) == PS_HEADER &&
                powersmart_valid(d->decoded_data)) {
                d->decoded_bits = PS_BITS;
                decoder_finish(d);
                return SUBGHZ_PROTOCOL_DONE;
            }
        }
    } else {
        d->decoded_data = 0;
        d->decoded_bits = 0;
        man_advance(p->man, MAN_EV_RESET, &p->man, NULL);
    }
    return SUBGHZ_PROTOCOL_SYNC;
}

static void powersmart_reset(subghz_protocol_decoder_t *d) {
    memset(d->priv, 0, sizeof(ps_priv_t));
    d->decoded_data = 0;
    d->decoded_bits = 0;
    d->hash = 0;
    d->te_us = 0;
    ((ps_priv_t *)d->priv)->man = MAN_ST_MID1;
}

static const char *powersmart_get_data(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->decoded_bits != PS_BITS) {
        return NULL;
    }
    snprintf(b, s, "Power Smart %ubit: 0x%08lx%08lx", d->decoded_bits,
             (unsigned long)(d->decoded_data >> 32), (unsigned long)d->decoded_data);
    return b;
}

static const char *powersmart_get_string(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->decoded_bits != PS_BITS) {
        return NULL;
    }
    uint64_t v = d->decoded_data;
    uint8_t btn = (uint8_t)(((v >> 54) & 0x02u) | ((v >> 40) & 0x01u));
    uint32_t serial = (uint32_t)(((v >> 33) & 0x3fff00u) | ((v >> 32) & 0xffu));
    uint8_t cnt = (uint8_t)((v >> 49) & 0x3fu);
    static const char *const btn_names[] = { "Unknown", "Down", "Up", "Stop" };
    char channel[7];
    for (int i = 0; i < 6; i++) {
        channel[i] = ((cnt >> i) & 1u) ? '*' : '-';
    }
    channel[6] = '\0';
    snprintf(b, s, "Power Smart %ubit\r\nKey:0x%08lx%08lx\r\nSn:0x%07lX\r\nBtn:%s\r\nChannel:%s",
             d->decoded_bits, (unsigned long)(uint32_t)(v >> 32),
             (unsigned long)(uint32_t)v, (unsigned long)(serial & 0xffffffu),
             btn_names[btn & 0x3u], channel);
    return b;
}

static const subghz_decoder_ops_t powersmart_decoder_ops = {
    .feed = powersmart_feed,
    .reset = powersmart_reset,
    .get_data = powersmart_get_data,
    .get_string = powersmart_get_string,
};

static subghz_protocol_status_t powersmart_encode(
    subghz_protocol_encoder_t *e, uint64_t data, uint32_t bits) {
    if (bits != PS_BITS) {
        return SUBGHZ_PROTOCOL_ERROR;
    }
    subghz_upload_reset(&e->upload);
    e->te_us = PS_SHORT;
    if (!man_encode_bits(&e->upload, data, bits, true, PS_SHORT, PS_LONG)) {
        return SUBGHZ_PROTOCOL_ERROR;
    }
    man_upload_add(&e->upload, false, PS_LONG * 1111u);
    return SUBGHZ_PROTOCOL_DONE;
}

static const subghz_encoder_ops_t powersmart_encoder_ops = {
    .feed = powersmart_encode, .yield = encoder_yield_generic, .reset = encoder_reset_generic,
};

/* ============================================================
 * MARANTEC: te 1000/2000/200, Manchester, 49 bits.
 * Header LOW 5*long; payload is seeded with a leading 1. A long
 * LOW (>= 2*long + delta) terminates the frame; 49 bits + CRC8
 * (poly 0x1D, init 0x01) over the top 6 bytes.
 * ============================================================ */

typedef struct {
    uint8_t step;
    man_state_t man;
} marantec_priv_t;

enum { MAR_RESET = 0, MAR_DATA };

#define MAR_SHORT 1000u
#define MAR_LONG 2000u
#define MAR_D 200u
#define MAR_BITS 49u

static subghz_protocol_status_t marantec_feed(
    subghz_protocol_decoder_t *d, bool level, uint32_t dur) {
    marantec_priv_t *p = d->priv;
    man_event_t event = MAN_EV_RESET;
    switch (p->step) {
    case MAR_RESET:
        if (!level && subghz_duration_match(dur, MAR_LONG * 5u, MAR_D * 8u)) {
            p->step = MAR_DATA;
            d->decoded_data = 1u;
            d->decoded_bits = 1u;
            d->te_us = MAR_SHORT;
            man_advance(p->man, MAN_EV_RESET, &p->man, NULL);
        }
        break;
    case MAR_DATA:
        if (!level) {
            if (subghz_duration_match(dur, MAR_SHORT, MAR_D)) {
                event = MAN_EV_SHORT_LOW;
            } else if (subghz_duration_match(dur, MAR_LONG, MAR_D)) {
                event = MAN_EV_LONG_LOW;
            } else if (dur >= MAR_LONG * 2u + MAR_D) {
                if (d->decoded_bits == MAR_BITS) {
                    decoder_finish(d);
                    return SUBGHZ_PROTOCOL_DONE;
                }
                d->decoded_data = 1u;
                d->decoded_bits = 1u;
                man_advance(p->man, MAN_EV_RESET, &p->man, NULL);
            } else {
                p->step = MAR_RESET;
            }
        } else {
            if (subghz_duration_match(dur, MAR_SHORT, MAR_D)) {
                event = MAN_EV_SHORT_HIGH;
            } else if (subghz_duration_match(dur, MAR_LONG, MAR_D)) {
                event = MAN_EV_LONG_HIGH;
            } else {
                p->step = MAR_RESET;
            }
        }
        if (event != MAN_EV_RESET) {
            bool bit = false;
            if (man_advance(p->man, event, &p->man, &bit)) {
                d->decoded_data = (d->decoded_data << 1) | (bit & 1u);
                d->decoded_bits++;
            }
        }
        break;
    default:
        p->step = MAR_RESET;
        break;
    }
    return SUBGHZ_PROTOCOL_SYNC;
}

static void marantec_reset(subghz_protocol_decoder_t *d) {
    memset(d->priv, 0, sizeof(marantec_priv_t));
    d->decoded_data = 0;
    d->decoded_bits = 0;
    d->hash = 0;
    d->te_us = 0;
    ((marantec_priv_t *)d->priv)->man = MAN_ST_MID1;
}

static uint8_t marantec_crc8(uint64_t data) {
    uint8_t tdata[6] = {
        (uint8_t)(data >> 48), (uint8_t)(data >> 40), (uint8_t)(data >> 32),
        (uint8_t)(data >> 24), (uint8_t)(data >> 16), (uint8_t)(data >> 8),
    };
    uint8_t crc = 0x01;
    for (size_t i = 0; i < sizeof(tdata); i++) {
        crc ^= tdata[i];
        for (size_t j = 0; j < 8; j++) {
            if (crc & 0x80u) {
                crc = (uint8_t)((crc << 1) ^ 0x1du);
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

static const char *marantec_get_data(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->decoded_bits != MAR_BITS) {
        return NULL;
    }
    snprintf(b, s, "Marantec %ubit: 0x%08lx%08lx", d->decoded_bits,
             (unsigned long)(d->decoded_data >> 32), (unsigned long)d->decoded_data);
    return b;
}

static const char *marantec_get_string(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->decoded_bits != MAR_BITS) {
        return NULL;
    }
    uint64_t v = d->decoded_data;
    uint32_t serial = (uint32_t)(((v >> 12) & 0xffffff00u) | ((v >> 8) & 0xffu));
    uint8_t crc = marantec_crc8(v);
    bool crc_ok = crc == (uint8_t)(v & 0xffu);
    snprintf(b, s, "Marantec %ubit\r\nKey: 0x%lX%08lX\r\nSn: 0x%07lX\r\nCRC: 0x%02X - %s\r\nBtn: %X",
             d->decoded_bits, (unsigned long)(uint32_t)(v >> 32),
             (unsigned long)(uint32_t)v, (unsigned long)(serial & 0xffffffu), crc,
             crc_ok ? "Valid" : "Invalid", (unsigned)((v >> 16) & 0xfu));
    return b;
}

static const subghz_decoder_ops_t marantec_decoder_ops = {
    .feed = marantec_feed, .reset = marantec_reset, .get_data = marantec_get_data,
    .get_string = marantec_get_string,
};

static subghz_protocol_status_t marantec_encode(
    subghz_protocol_encoder_t *e, uint64_t data, uint32_t bits) {
    if (bits != MAR_BITS || ((data >> 48) & 1u) == 0u) {
        return SUBGHZ_PROTOCOL_ERROR;
    }
    subghz_upload_reset(&e->upload);
    e->te_us = MAR_SHORT;
    /* Prime the Manchester encoder with the MSB, then the header gap, then
     * the remaining 48 bits (mirrors the Flipper upload layout). */
    man_enc_t st = { false, 0 };
    man_result_t r;
    bool msb = ((data >> 48) & 1ull) != 0;
    if (!man_enc_advance(&st, msb, &r)) {
        if (!man_result_add(&e->upload, r, MAR_SHORT, MAR_LONG)) {
            return SUBGHZ_PROTOCOL_ERROR;
        }
        if (!man_enc_advance(&st, msb, &r)) {
            return SUBGHZ_PROTOCOL_ERROR;
        }
    }
    if (!man_result_add(&e->upload, r, MAR_SHORT, MAR_LONG)) {
        return SUBGHZ_PROTOCOL_ERROR;
    }
    man_upload_add(&e->upload, false, MAR_LONG * 5u);
    for (int i = (int)MAR_BITS - 2; i >= 0; i--) {
        bool b = ((data >> i) & 1ull) != 0;
        if (!man_enc_advance(&st, b, &r)) {
            if (!man_result_add(&e->upload, r, MAR_SHORT, MAR_LONG)) {
                return SUBGHZ_PROTOCOL_ERROR;
            }
            if (!man_enc_advance(&st, b, &r)) {
                return SUBGHZ_PROTOCOL_ERROR;
            }
        }
        if (!man_result_add(&e->upload, r, MAR_SHORT, MAR_LONG)) {
            return SUBGHZ_PROTOCOL_ERROR;
        }
    }
    if (!man_result_add(&e->upload, man_enc_finish(&st), MAR_SHORT, MAR_LONG)) {
        return SUBGHZ_PROTOCOL_ERROR;
    }
    /* Trailing gap terminates the frame for decoders (inter-frame silence). */
    man_upload_add(&e->upload, false, MAR_LONG * 5u);
    return SUBGHZ_PROTOCOL_DONE;
}

static const subghz_encoder_ops_t marantec_encoder_ops = {
    .feed = marantec_encode, .yield = encoder_yield_generic, .reset = encoder_reset_generic,
};

/* ============================================================
 * CHAMBERLAIN CODE ("Cham_Code", 315 MHz): te 1000/3000/200.
 * Nibble symbols over L/H pairs: STOP = L3/H1, 1 = L2/H2,
 * 0 = L1/H3. Header LOW 39TE, start HIGH 1TE (first STOP).
 * A LOW longer than 5TE ends the frame; 10-11 symbols must match
 * one of the 7/8/9-bit masks, then nibbles collapse to DIP bits.
 * ============================================================ */

typedef struct {
    uint8_t step;
    uint32_t te_last;
} chamb_priv_t;

enum { CH_RESET = 0, CH_START, CH_SAVE, CH_CHECK };

#define CH_TE 1000u
#define CH_LONG 3000u
#define CH_D 200u

#define CH_BIT_STOP 0x1u
#define CH_BIT_1 0x3u
#define CH_BIT_0 0x7u

#define CH_MASK_7 0xf000000ff0full
#define CH_MASK_8 0xf00000f00full
#define CH_MASK_9 0xf000000000full
#define CH_CHECK_7 0x10000001101ull
#define CH_CHECK_8 0x1000001001ull
#define CH_CHECK_9 0x10000000001ull

static bool chamb_to_bit(uint64_t *data, uint32_t bits) {
    uint64_t tmp = *data, res = 0;
    for (uint32_t i = 0; i < bits; i++) {
        if ((tmp & 0xfull) == CH_BIT_0) {
            res &= ~(1ull << i);
        } else if ((tmp & 0xfull) == CH_BIT_1) {
            res |= 1ull << i;
        } else {
            return false;
        }
        tmp >>= 4;
    }
    *data = res;
    return true;
}

static bool chamb_mask_parse(subghz_protocol_decoder_t *d) {
    if (d->decoded_bits > 11u) {
        return false;
    }
    if ((d->decoded_data & CH_MASK_7) == CH_CHECK_7) {
        d->decoded_bits = 7u;
        d->decoded_data &= ~CH_MASK_7;
        d->decoded_data = (d->decoded_data >> 12) | ((d->decoded_data >> 4) & 0xfu);
    } else if ((d->decoded_data & CH_MASK_8) == CH_CHECK_8) {
        d->decoded_bits = 8u;
        d->decoded_data &= ~CH_MASK_8;
        d->decoded_data = (d->decoded_data >> 4) | (CH_BIT_0 << 8);
    } else if ((d->decoded_data & CH_MASK_9) == CH_CHECK_9) {
        d->decoded_bits = 9u;
        d->decoded_data &= ~CH_MASK_9;
        d->decoded_data >>= 4;
    } else {
        return false;
    }
    return chamb_to_bit(&d->decoded_data, d->decoded_bits);
}

static subghz_protocol_status_t chamb_feed(
    subghz_protocol_decoder_t *d, bool level, uint32_t dur) {
    chamb_priv_t *p = d->priv;
    switch (p->step) {
    case CH_RESET:
        if (!level && subghz_duration_match(dur, CH_TE * 39u, CH_D * 20u)) {
            p->step = CH_START;
        }
        break;
    case CH_START:
        if (level && subghz_duration_match(dur, CH_TE, CH_D)) {
            d->decoded_data = CH_BIT_STOP;
            d->decoded_bits = 1u;
            d->te_us = CH_TE;
            p->step = CH_SAVE;
        } else {
            p->step = CH_RESET;
        }
        break;
    case CH_SAVE:
        if (!level) {
            if (dur > CH_TE * 5u) {
                p->step = CH_RESET;
                if (d->decoded_bits >= 10u && chamb_mask_parse(d)) {
                    decoder_finish(d);
                    return SUBGHZ_PROTOCOL_DONE;
                }
                d->decoded_data = 0;
                d->decoded_bits = 0;
            } else {
                p->te_last = dur;
                p->step = CH_CHECK;
            }
        } else {
            p->step = CH_RESET;
        }
        break;
    case CH_CHECK:
        if (level) {
            if (subghz_duration_match(p->te_last, CH_TE * 3u, CH_D) &&
                subghz_duration_match(dur, CH_TE, CH_D)) {
                d->decoded_data = (d->decoded_data << 4) | CH_BIT_STOP;
                d->decoded_bits++;
                p->step = CH_SAVE;
            } else if (
                subghz_duration_match(p->te_last, CH_TE * 2u, CH_D) &&
                subghz_duration_match(dur, CH_TE * 2u, CH_D)) {
                d->decoded_data = (d->decoded_data << 4) | CH_BIT_1;
                d->decoded_bits++;
                p->step = CH_SAVE;
            } else if (
                subghz_duration_match(p->te_last, CH_TE, CH_D) &&
                subghz_duration_match(dur, CH_TE * 3u, CH_D)) {
                d->decoded_data = (d->decoded_data << 4) | CH_BIT_0;
                d->decoded_bits++;
                p->step = CH_SAVE;
            } else {
                p->step = CH_RESET;
            }
        } else {
            p->step = CH_RESET;
        }
        break;
    default:
        p->step = CH_RESET;
        break;
    }
    return SUBGHZ_PROTOCOL_SYNC;
}

static void chamb_reset(subghz_protocol_decoder_t *d) {
    memset(d->priv, 0, sizeof(chamb_priv_t));
    d->decoded_data = 0;
    d->decoded_bits = 0;
    d->hash = 0;
    d->te_us = 0;
}

static const char *chamb_get_data(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->decoded_bits < 7u || d->decoded_bits > 9u) {
        return NULL;
    }
    snprintf(b, s, "Cham_Code %ubit: 0x%03lX", d->decoded_bits,
             (unsigned long)(d->decoded_data & 0xffffffffu));
    return b;
}

static const char *chamb_get_string(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->decoded_bits < 7u || d->decoded_bits > 9u) {
        return NULL;
    }
    uint32_t lo = (uint32_t)(d->decoded_data & 0xffffffffu);
    uint32_t rev = (uint32_t)reverse_key(d->decoded_data, d->decoded_bits);
    char dip[10];
    if (d->decoded_bits == 7u) {
        for (int i = 0; i < 7; i++) {
            dip[i] = ((lo >> (6 - i)) & 1u) ? '1' : '0';
        }
        dip[7] = '\0';
    } else if (d->decoded_bits == 8u) {
        /* Bit 2 is unused on 8-dip remotes (shown as 'x', Flipper order). */
        dip[0] = ((lo >> 7) & 1u) ? '1' : '0';
        dip[1] = ((lo >> 6) & 1u) ? '1' : '0';
        dip[2] = ((lo >> 5) & 1u) ? '1' : '0';
        dip[3] = ((lo >> 4) & 1u) ? '1' : '0';
        dip[4] = ((lo >> 3) & 1u) ? '1' : '0';
        dip[5] = 'x';
        dip[6] = ((lo >> 0) & 1u) ? '1' : '0';
        dip[7] = ((lo >> 1) & 1u) ? '1' : '0';
        dip[8] = '\0';
    } else {
        const uint8_t order[] = { 8, 7, 6, 5, 4, 3, 0, 1, 2 };
        for (int i = 0; i < 9; i++) {
            dip[i] = ((lo >> order[i]) & 1u) ? '1' : '0';
        }
        dip[9] = '\0';
    }
    snprintf(b, s, "Cham_Code %ubit\r\nKey:0x%03lX\r\nYek:0x%03lX\r\nDIP:%s", d->decoded_bits,
             (unsigned long)lo, (unsigned long)(rev & 0xffffffffu), dip);
    return b;
}

static uint64_t chamb_bit_to_code(uint64_t data, uint32_t bits) {
    uint64_t res = 0;
    for (uint32_t i = 0; i < bits; i++) {
        res = (res << 4) | (((data >> (bits - i - 1u)) & 1ull) ? CH_BIT_1 : CH_BIT_0);
    }
    return res;
}

static subghz_protocol_status_t chamb_encode(
    subghz_protocol_encoder_t *e, uint64_t data, uint32_t bits) {
    if ((bits != 7u && bits != 8u && bits != 9u) || (data >> bits)) {
        return SUBGHZ_PROTOCOL_ERROR;
    }
    uint64_t frame = chamb_bit_to_code(data, bits);
    uint32_t frame_bits;
    if (bits == 7u) {
        frame = ((frame >> 4) << 16) | ((frame & 0xfu) << 4) | CH_CHECK_7;
        frame_bits = 44u;
    } else if (bits == 8u) {
        frame = ((frame >> 12) << 16) | ((frame & 0xffu) << 4) | CH_CHECK_8;
        frame_bits = 40u;
    } else {
        frame = (frame << 4) | CH_CHECK_9;
        frame_bits = 44u;
    }
    subghz_upload_reset(&e->upload);
    e->te_us = CH_TE;
    /* Guard gap (also the RX header) then the frame MSB-first, merged runs. */
    man_upload_add(&e->upload, false, CH_TE * 36u);
    for (uint32_t i = frame_bits; i > 0; i--) {
        bool bit = ((frame >> (i - 1u)) & 1ull) != 0;
        man_upload_add(&e->upload, bit, CH_TE);
    }
    /* Trailing gap terminates the frame for decoders. */
    man_upload_add(&e->upload, false, CH_TE * 36u);
    return SUBGHZ_PROTOCOL_DONE;
}

static const subghz_encoder_ops_t chamb_encoder_ops = {
    .feed = chamb_encode, .yield = encoder_yield_generic, .reset = encoder_reset_generic,
};

static const subghz_decoder_ops_t chamb_decoder_ops = {
    .feed = chamb_feed, .reset = chamb_reset, .get_data = chamb_get_data,
    .get_string = chamb_get_string,
};

/* ============================================================
 * HOLTEK HT12X ("Holtek_HT12X"): te 320/640/200, 12 bits.
 * Header LOW 28TE, start HIGH 1TE, bits low-first 1 = L2/H1,
 * 0 = L1/H2. Like Flipper, a frame is accepted only after two
 * identical consecutive frames; TE is measured from the frame.
 * ============================================================ */

typedef struct {
    uint8_t step;
    uint32_t te_last;
    uint32_t te_sum;
    uint64_t last_data;
} ht12x_priv_t;

enum { HT12X_RESET = 0, HT12X_START, HT12X_SAVE, HT12X_CHECK };

#define HT12X_TE 320u
#define HT12X_LONG 640u
#define HT12X_D 200u
#define HT12X_BITS 12u

static subghz_protocol_status_t ht12x_feed(
    subghz_protocol_decoder_t *d, bool level, uint32_t dur) {
    ht12x_priv_t *p = d->priv;
    switch (p->step) {
    case HT12X_RESET:
        if (!level && subghz_duration_match(dur, HT12X_TE * 28u, HT12X_D * 20u)) {
            p->step = HT12X_START;
        }
        break;
    case HT12X_START:
        if (level && subghz_duration_match(dur, HT12X_TE, HT12X_D)) {
            p->step = HT12X_SAVE;
            d->decoded_data = 0;
            d->decoded_bits = 0;
            d->te_us = HT12X_TE;
            p->te_sum = dur;
        } else {
            p->step = HT12X_RESET;
        }
        break;
    case HT12X_SAVE:
        if (!level) {
            if (dur >= HT12X_TE * 10u + HT12X_D) {
                p->step = HT12X_START;
                if (d->decoded_bits == HT12X_BITS) {
                    if (p->last_data == d->decoded_data && p->last_data) {
                        d->te_us = p->te_sum / (HT12X_BITS * 3u + 1u);
                        p->last_data = 0; /* Debounce: re-arm for the next pair. */
                        decoder_finish(d);
                        return SUBGHZ_PROTOCOL_DONE;
                    }
                    p->last_data = d->decoded_data;
                }
                d->decoded_data = 0;
                d->decoded_bits = 0;
                p->te_sum = 0;
                break;
            }
            p->te_last = dur;
            p->te_sum += dur;
            p->step = HT12X_CHECK;
        } else {
            p->step = HT12X_RESET;
        }
        break;
    case HT12X_CHECK:
        if (level) {
            p->te_sum += dur;
            if (subghz_duration_match(p->te_last, HT12X_LONG, HT12X_D * 2u) &&
                subghz_duration_match(dur, HT12X_TE, HT12X_D)) {
                decoder_add_bit(d, 1);
                p->step = HT12X_SAVE;
            } else if (
                subghz_duration_match(p->te_last, HT12X_TE, HT12X_D) &&
                subghz_duration_match(dur, HT12X_LONG, HT12X_D * 2u)) {
                decoder_add_bit(d, 0);
                p->step = HT12X_SAVE;
            } else {
                p->step = HT12X_RESET;
            }
        } else {
            p->step = HT12X_RESET;
        }
        break;
    default:
        p->step = HT12X_RESET;
        break;
    }
    return SUBGHZ_PROTOCOL_SYNC;
}

static void ht12x_reset(subghz_protocol_decoder_t *d) {
    memset(d->priv, 0, sizeof(ht12x_priv_t));
    d->decoded_data = 0;
    d->decoded_bits = 0;
    d->hash = 0;
    d->te_us = 0;
}

static const char *ht12x_get_data(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->decoded_bits != HT12X_BITS) {
        return NULL;
    }
    snprintf(b, s, "Holtek_HT12X %ubit: 0x%03lX", d->decoded_bits,
             (unsigned long)(d->decoded_data & 0xffful));
    return b;
}

static const char *ht12x_get_string(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->decoded_bits != HT12X_BITS) {
        return NULL;
    }
    uint16_t v = (uint16_t)(d->decoded_data & 0xffful);
    uint8_t btn = (uint8_t)(v & 0xfu);
    uint8_t cnt = (uint8_t)((v >> 4) & 0xffu);
    char dip[9];
    for (int i = 0; i < 8; i++) {
        dip[i] = ((cnt >> (7 - i)) & 1u) ? '0' : '1';
    }
    dip[8] = '\0';
    char buttons[16];
    size_t pos = 0;
    for (int i = 3; i >= 0; i--) {
        if (!((btn >> i) & 1u) && pos + 4u < sizeof(buttons)) {
            int w = snprintf(buttons + pos, sizeof(buttons) - pos, "B%u ",
                             (unsigned)(4 - i));
            if (w > 0) {
                pos += (size_t)w;
            }
        }
    }
    buttons[pos < sizeof(buttons) ? pos : sizeof(buttons) - 1u] = '\0';
    snprintf(b, s, "Holtek_HT12X %ubit\r\nKey:0x%03X\r\nBtn: %s\r\nDIP:%s\r\nTe:%luus",
             d->decoded_bits, (unsigned int)v, buttons, dip, (unsigned long)d->te_us);
    return b;
}

static const subghz_decoder_ops_t ht12x_decoder_ops = {
    .feed = ht12x_feed, .reset = ht12x_reset, .get_data = ht12x_get_data,
    .get_string = ht12x_get_string,
};

static subghz_protocol_status_t ht12x_encode(
    subghz_protocol_encoder_t *e, uint64_t data, uint32_t bits) {
    if (bits != HT12X_BITS || (data >> HT12X_BITS)) {
        return SUBGHZ_PROTOCOL_ERROR;
    }
    subghz_upload_reset(&e->upload);
    e->te_us = HT12X_TE;
    subghz_upload_add(&e->upload, false, HT12X_TE * 36u);
    subghz_upload_add(&e->upload, true, HT12X_TE);
    for (int i = (int)HT12X_BITS - 1; i >= 0; i--) {
        if ((data >> i) & 1ull) {
            subghz_upload_add(&e->upload, false, HT12X_LONG);
            subghz_upload_add(&e->upload, true, HT12X_TE);
        } else {
            subghz_upload_add(&e->upload, false, HT12X_TE);
            subghz_upload_add(&e->upload, true, HT12X_LONG);
        }
    }
    return SUBGHZ_PROTOCOL_DONE;
}

static const subghz_encoder_ops_t ht12x_encoder_ops = {
    .feed = ht12x_encode, .yield = encoder_yield_generic, .reset = encoder_reset_generic,
};

/* ============================================================
 * MAGELLAN: te 200/400/100, 32 bits + CRC8 (poly 0x31, init 0).
 * Preamble of 12+ H/L 1TE pairs, start H 6TE + L 2TE, then bits
 * high-first 1 = H1/L2, 0 = H2/L1, stop on LOW >= 3*long.
 * ============================================================ */

typedef struct {
    uint8_t step;
    uint32_t te_last;
    uint16_t header_count;
} magellan_priv_t;

enum { MAG_RESET = 0, MAG_PREAMBLE, MAG_START, MAG_SAVE, MAG_CHECK };

#define MAG_SHORT 200u
#define MAG_LONG 400u
#define MAG_D 100u
#define MAG_BITS 32u

static uint8_t magellan_crc8(uint32_t data) {
    uint8_t bytes[3] = {
        (uint8_t)(data >> 24), (uint8_t)(data >> 16), (uint8_t)(data >> 8),
    };
    uint8_t crc = 0x00;
    for (size_t i = 0; i < sizeof(bytes); i++) {
        crc ^= bytes[i];
        for (size_t j = 0; j < 8; j++) {
            if (crc & 0x80u) {
                crc = (uint8_t)((crc << 1) ^ 0x31u);
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

static subghz_protocol_status_t magellan_feed(
    subghz_protocol_decoder_t *d, bool level, uint32_t dur) {
    magellan_priv_t *p = d->priv;
    switch (p->step) {
    case MAG_RESET:
        if (level && subghz_duration_match(dur, MAG_SHORT, MAG_D)) {
            p->step = MAG_PREAMBLE;
            d->te_us = MAG_SHORT;
            p->te_last = dur;
            p->header_count = 0;
        }
        break;
    case MAG_PREAMBLE:
        if (level) {
            p->te_last = dur;
        } else {
            if (subghz_duration_match(p->te_last, MAG_SHORT, MAG_D) &&
                subghz_duration_match(dur, MAG_SHORT, MAG_D)) {
                p->header_count++;
            } else if (
                subghz_duration_match(p->te_last, MAG_SHORT, MAG_D) &&
                subghz_duration_match(dur, MAG_LONG, MAG_D * 2u) && p->header_count > 10u) {
                p->step = MAG_START;
            } else {
                p->step = MAG_RESET;
            }
        }
        break;
    case MAG_START:
        if (level) {
            p->te_last = dur;
        } else {
            if (subghz_duration_match(p->te_last, MAG_SHORT * 6u, MAG_D * 3u) &&
                subghz_duration_match(dur, MAG_LONG, MAG_D * 2u)) {
                p->step = MAG_SAVE;
                d->decoded_data = 0;
                d->decoded_bits = 0;
            } else {
                p->step = MAG_RESET;
            }
        }
        break;
    case MAG_SAVE:
        if (level) {
            p->te_last = dur;
            p->step = MAG_CHECK;
        } else {
            p->step = MAG_RESET;
        }
        break;
    case MAG_CHECK:
        if (!level) {
            if (subghz_duration_match(p->te_last, MAG_SHORT, MAG_D) &&
                subghz_duration_match(dur, MAG_LONG, MAG_D)) {
                decoder_add_bit(d, 1);
                p->step = MAG_SAVE;
            } else if (
                subghz_duration_match(p->te_last, MAG_LONG, MAG_D) &&
                subghz_duration_match(dur, MAG_SHORT, MAG_D)) {
                decoder_add_bit(d, 0);
                p->step = MAG_SAVE;
            } else if (dur >= MAG_LONG * 3u) {
                p->step = MAG_RESET;
                if (d->decoded_bits == MAG_BITS &&
                    (uint8_t)(d->decoded_data & 0xffu) ==
                        magellan_crc8((uint32_t)d->decoded_data)) {
                    decoder_finish(d);
                    return SUBGHZ_PROTOCOL_DONE;
                }
                d->decoded_data = 0;
                d->decoded_bits = 0;
            } else {
                p->step = MAG_RESET;
            }
        } else {
            p->step = MAG_RESET;
        }
        break;
    default:
        p->step = MAG_RESET;
        break;
    }
    return SUBGHZ_PROTOCOL_SYNC;
}

static void magellan_reset(subghz_protocol_decoder_t *d) {
    memset(d->priv, 0, sizeof(magellan_priv_t));
    d->decoded_data = 0;
    d->decoded_bits = 0;
    d->hash = 0;
    d->te_us = 0;
}

static const char *magellan_get_data(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->decoded_bits != MAG_BITS) {
        return NULL;
    }
    snprintf(b, s, "Magellan %ubit: 0x%08lX", d->decoded_bits,
             (unsigned long)(d->decoded_data & 0xffffffffu));
    return b;
}

static void magellan_event_name(uint8_t event, char *out, size_t n) {
    /* Mirrors Flipper's table including its 0x06 -> "REM2 - Off1" quirk. */
    uint8_t type = (uint8_t)((event >> 4) & 0xfu);
    uint8_t sub = (uint8_t)(event & 0xfu);
    if (type == 0x05u) {
        snprintf(out, n, "REM1 - Off1");
        return;
    }
    if (type == 0x06u) {
        snprintf(out, n, "REM2 - Off1");
        return;
    }
    const char *type_name = "Unknown";
    switch (type) {
    case 0x00: type_name = "Nothing"; break;
    case 0x01: type_name = "Door"; break;
    case 0x02: type_name = "Motion"; break;
    case 0x03: type_name = "Smoke Alarm"; break;
    case 0x04: type_name = "REM1"; break;
    default: break;
    }
    const char *sub_name = "Unknown";
    switch (sub) {
    case 0x00: sub_name = type > 0x03u ? "Arm1" : "Sealed"; break;
    case 0x01: sub_name = type > 0x03u ? "Btn1" : "Alarm"; break;
    case 0x02: sub_name = type > 0x03u ? "Btn2" : "Tamper"; break;
    case 0x03: sub_name = type > 0x03u ? "Btn3" : "Alarm + Tamper"; break;
    case 0x08: sub_name = "Reset"; break;
    case 0x09: sub_name = "LowBatt"; break;
    case 0x0a: sub_name = "BattOk"; break;
    case 0x0b: sub_name = "Learn"; break;
    default: break;
    }
    snprintf(out, n, "%s - %s", type_name, sub_name);
}

static const char *magellan_get_string(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->decoded_bits != MAG_BITS) {
        return NULL;
    }
    uint64_t v = d->decoded_data;
    uint64_t rev = reverse_key((v >> 8) & 0xffffffull, 24);
    uint16_t serial = (uint16_t)(rev & 0xffffu);
    uint8_t btn = (uint8_t)((rev >> 16) & 0xffu);
    char event[32];
    magellan_event_name(btn, event, sizeof(event));
    snprintf(b, s, "Magellan %ubit\r\nKey:0x%08lX\r\nSn:%03u%03u, Event:0x%02X\r\nStat:%s",
             d->decoded_bits, (unsigned long)v, (serial >> 8) & 0xffu, serial & 0xffu, btn,
             event);
    return b;
}

static const subghz_decoder_ops_t magellan_decoder_ops = {
    .feed = magellan_feed, .reset = magellan_reset, .get_data = magellan_get_data,
    .get_string = magellan_get_string,
};

static subghz_protocol_status_t magellan_encode(
    subghz_protocol_encoder_t *e, uint64_t data, uint32_t bits) {
    if (bits != MAG_BITS || (data >> MAG_BITS)) {
        return SUBGHZ_PROTOCOL_ERROR;
    }
    subghz_upload_reset(&e->upload);
    e->te_us = MAG_SHORT;
    man_upload_add(&e->upload, true, MAG_SHORT * 4u);
    man_upload_add(&e->upload, false, MAG_SHORT);
    for (int i = 0; i < 12; i++) {
        man_upload_add(&e->upload, true, MAG_SHORT);
        man_upload_add(&e->upload, false, MAG_SHORT);
    }
    man_upload_add(&e->upload, true, MAG_SHORT);
    man_upload_add(&e->upload, false, MAG_LONG);
    man_upload_add(&e->upload, true, MAG_LONG * 3u);
    man_upload_add(&e->upload, false, MAG_LONG);
    for (int i = (int)MAG_BITS - 1; i >= 0; i--) {
        if ((data >> i) & 1ull) {
            man_upload_add(&e->upload, true, MAG_SHORT);
            man_upload_add(&e->upload, false, MAG_LONG);
        } else {
            man_upload_add(&e->upload, true, MAG_LONG);
            man_upload_add(&e->upload, false, MAG_SHORT);
        }
    }
    man_upload_add(&e->upload, true, MAG_SHORT);
    man_upload_add(&e->upload, false, MAG_LONG * 100u);
    return SUBGHZ_PROTOCOL_DONE;
}

static const subghz_encoder_ops_t magellan_encoder_ops = {
    .feed = magellan_encode, .yield = encoder_yield_generic, .reset = encoder_reset_generic,
};

/* ============================================================
 * CAME TWEE ("CAME TWEE"): te 500/1000/250, Manchester, 54 bits.
 * Header LOW 51*long; the Manchester engine is primed with
 * LongLow + LongHigh + ShortLow. Decoded bits are inverted. Frames
 * carry a rolling parcel counter (low nibble selects one of 15
 * magic XORs); Btn/DIP are recovered by the documented decrypt.
 * Single-parcel replay mirrors Flipper's per-parcel upload.
 * ============================================================ */

typedef struct {
    uint8_t step;
    man_state_t man;
} twee_priv_t;

enum { TWEE_RESET = 0, TWEE_DATA };

#define TWEE_SHORT 500u
#define TWEE_LONG 1000u
#define TWEE_D 250u
#define TWEE_BITS 54u

static const uint32_t twee_magic[15] = {
    0x0e0e0e00u, 0x1d1d1d11u, 0x2c2c2c22u, 0x3b3b3b33u, 0x4a4a4a44u,
    0x59595955u, 0x68686866u, 0x77777777u, 0x86868688u, 0x95959599u,
    0xa4a4a4aau, 0xb3b3b3bbu, 0xc2c2c2ccu, 0xd1d1d1ddu, 0xe0e0e0eeu,
};

static void twee_prime(man_state_t *man) {
    man_advance(*man, MAN_EV_RESET, man, NULL);
    man_advance(*man, MAN_EV_LONG_LOW, man, NULL);
    man_advance(*man, MAN_EV_LONG_HIGH, man, NULL);
    man_advance(*man, MAN_EV_SHORT_LOW, man, NULL);
}

static subghz_protocol_status_t twee_feed(
    subghz_protocol_decoder_t *d, bool level, uint32_t dur) {
    twee_priv_t *p = d->priv;
    man_event_t event = MAN_EV_RESET;
    switch (p->step) {
    case TWEE_RESET:
        if (!level && subghz_duration_match(dur, TWEE_LONG * 51u, TWEE_D * 20u)) {
            p->step = TWEE_DATA;
            d->decoded_data = 0;
            d->decoded_bits = 0;
            d->te_us = TWEE_SHORT;
            twee_prime(&p->man);
        }
        break;
    case TWEE_DATA:
        if (!level) {
            if (subghz_duration_match(dur, TWEE_SHORT, TWEE_D)) {
                event = MAN_EV_SHORT_LOW;
            } else if (subghz_duration_match(dur, TWEE_LONG, TWEE_D)) {
                event = MAN_EV_LONG_LOW;
            } else if (dur >= TWEE_LONG * 2u + TWEE_D) {
                if (d->decoded_bits == TWEE_BITS) {
                    decoder_finish(d);
                    return SUBGHZ_PROTOCOL_DONE;
                }
                d->decoded_data = 0;
                d->decoded_bits = 0;
                twee_prime(&p->man);
            } else {
                p->step = TWEE_RESET;
            }
        } else {
            if (subghz_duration_match(dur, TWEE_SHORT, TWEE_D)) {
                event = MAN_EV_SHORT_HIGH;
            } else if (subghz_duration_match(dur, TWEE_LONG, TWEE_D)) {
                event = MAN_EV_LONG_HIGH;
            } else {
                p->step = TWEE_RESET;
            }
        }
        if (event != MAN_EV_RESET) {
            bool bit = false;
            if (man_advance(p->man, event, &p->man, &bit)) {
                d->decoded_data = (d->decoded_data << 1) | ((bit ? 0u : 1u));
                d->decoded_bits++;
            }
        }
        break;
    default:
        p->step = TWEE_RESET;
        break;
    }
    return SUBGHZ_PROTOCOL_SYNC;
}

static void twee_reset(subghz_protocol_decoder_t *d) {
    memset(d->priv, 0, sizeof(twee_priv_t));
    d->decoded_data = 0;
    d->decoded_bits = 0;
    d->hash = 0;
    d->te_us = 0;
    ((twee_priv_t *)d->priv)->man = MAN_ST_MID1;
}

/* Display decrypt: rolling parcel counter selects the magic XOR, then
 * divide by 4, button nibble, bit-reversed 16-bit DIP field. */
static void twee_decode_detail(
    uint64_t data, uint8_t *btn, uint16_t *dip, uint32_t *serial) {
    uint8_t parcel = (uint8_t)(data & 0xfu);
    uint32_t w = (uint32_t)(data & 0xffffffffull);
    if (parcel < 15u) {
        w ^= twee_magic[parcel];
    }
    if (serial) {
        *serial = w;
    }
    w /= 4u;
    if (btn) {
        *btn = (uint8_t)((w >> 4) & 0xfu);
    }
    w >>= 16;
    uint16_t rev = 0;
    for (int i = 0; i < 16; i++) {
        rev = (uint16_t)((rev << 1) | ((w >> i) & 1u));
    }
    if (dip) {
        *dip = (uint16_t)(rev >> 6);
    }
}

static const char *twee_get_data(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->decoded_bits != TWEE_BITS) {
        return NULL;
    }
    snprintf(b, s, "CAME TWEE %ubit: 0x%08lx%08lx", d->decoded_bits,
             (unsigned long)(d->decoded_data >> 32), (unsigned long)d->decoded_data);
    return b;
}

static const char *twee_get_string(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->decoded_bits != TWEE_BITS) {
        return NULL;
    }
    uint64_t v = d->decoded_data;
    uint8_t btn = 0;
    uint16_t dip = 0;
    twee_decode_detail(v, &btn, &dip, NULL);
    char dips[11];
    for (int i = 0; i < 10; i++) {
        dips[i] = ((dip >> (9 - i)) & 1u) ? '1' : '0';
    }
    dips[10] = '\0';
    snprintf(b, s, "CAME TWEE %ubit\r\nKey:0x%lX%08lX\r\nBtn:%X\r\nDIP:%s", d->decoded_bits,
             (unsigned long)(uint32_t)(v >> 32), (unsigned long)(uint32_t)v, btn, dips);
    return b;
}

static const subghz_decoder_ops_t twee_decoder_ops = {
    .feed = twee_feed, .reset = twee_reset, .get_data = twee_get_data,
    .get_string = twee_get_string,
};

static subghz_protocol_status_t twee_encode(
    subghz_protocol_encoder_t *e, uint64_t data, uint32_t bits) {
    if (bits != TWEE_BITS || (data >> TWEE_BITS)) {
        return SUBGHZ_PROTOCOL_ERROR;
    }
    subghz_upload_reset(&e->upload);
    e->te_us = TWEE_SHORT;
    if (!man_encode_bits(&e->upload, data, bits, true, TWEE_SHORT, TWEE_LONG)) {
        return SUBGHZ_PROTOCOL_ERROR;
    }
    man_upload_add(&e->upload, false, TWEE_LONG * 51u);
    return SUBGHZ_PROTOCOL_DONE;
}

static const subghz_encoder_ops_t twee_encoder_ops = {
    .feed = twee_encode, .yield = encoder_yield_generic, .reset = encoder_reset_generic,
};

/* ============================================================
 * FAAC SLH ("Faac SLH"): te 255/595/100, 64 bits, rolling.
 * Preamble HIGH/LOW 2*long; bits high-first 0 = H1/L2.33,
 * 1 = H2.33/L1. A HIGH >= 3*short doubles as frame end and next
 * preamble (decode-only, no TX in upstream either).
 * ============================================================ */

typedef struct {
    uint8_t step;
    uint32_t te_last;
} faac_priv_t;

enum { FAAC_RESET = 0, FAAC_PREAMBLE, FAAC_SAVE, FAAC_CHECK };

#define FAAC_SHORT 255u
#define FAAC_LONG 595u
#define FAAC_D 100u
#define FAAC_BITS 64u

static subghz_protocol_status_t faac_feed(
    subghz_protocol_decoder_t *d, bool level, uint32_t dur) {
    faac_priv_t *p = d->priv;
    switch (p->step) {
    case FAAC_RESET:
        if (level && subghz_duration_match(dur, FAAC_LONG * 2u, FAAC_D * 3u)) {
            p->step = FAAC_PREAMBLE;
        }
        break;
    case FAAC_PREAMBLE:
        if (!level && subghz_duration_match(dur, FAAC_LONG * 2u, FAAC_D * 3u)) {
            p->step = FAAC_SAVE;
            d->decoded_data = 0;
            d->decoded_bits = 0;
            d->te_us = FAAC_SHORT;
        } else {
            p->step = FAAC_RESET;
        }
        break;
    case FAAC_SAVE:
        if (level) {
            if (dur >= FAAC_SHORT * 3u + FAAC_D) {
                p->step = FAAC_PREAMBLE;
                if (d->decoded_bits == FAAC_BITS) {
                    decoder_finish(d);
                    return SUBGHZ_PROTOCOL_DONE;
                }
                d->decoded_data = 0;
                d->decoded_bits = 0;
                break;
            }
            p->te_last = dur;
            p->step = FAAC_CHECK;
        } else {
            p->step = FAAC_RESET;
        }
        break;
    case FAAC_CHECK:
        if (!level) {
            if (subghz_duration_match(p->te_last, FAAC_SHORT, FAAC_D) &&
                subghz_duration_match(dur, FAAC_LONG, FAAC_D)) {
                decoder_add_bit(d, 0);
                p->step = FAAC_SAVE;
            } else if (
                subghz_duration_match(p->te_last, FAAC_LONG, FAAC_D) &&
                subghz_duration_match(dur, FAAC_SHORT, FAAC_D)) {
                decoder_add_bit(d, 1);
                p->step = FAAC_SAVE;
            } else {
                p->step = FAAC_RESET;
            }
        } else {
            p->step = FAAC_RESET;
        }
        break;
    default:
        p->step = FAAC_RESET;
        break;
    }
    return SUBGHZ_PROTOCOL_SYNC;
}

static void faac_reset(subghz_protocol_decoder_t *d) {
    memset(d->priv, 0, sizeof(faac_priv_t));
    d->decoded_data = 0;
    d->decoded_bits = 0;
    d->hash = 0;
    d->te_us = 0;
}

static const char *faac_get_data(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->decoded_bits != FAAC_BITS) {
        return NULL;
    }
    snprintf(b, s, "Faac SLH %ubit: 0x%08lx%08lx", d->decoded_bits,
             (unsigned long)(d->decoded_data >> 32), (unsigned long)d->decoded_data);
    return b;
}

static const char *faac_get_string(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->decoded_bits != FAAC_BITS) {
        return NULL;
    }
    uint64_t v = d->decoded_data;
    uint64_t rev = reverse_key(v, FAAC_BITS);
    uint32_t fix = (uint32_t)(rev & 0xffffffffu);
    uint32_t hop = (uint32_t)((rev >> 32) & 0xffffffffu);
    uint32_t serial = fix & 0x0ffffffful;
    uint8_t btn = (uint8_t)((fix >> 28) & 0xfu);
    snprintf(b, s, "Faac SLH %ubit\r\nKey:%lX%08lX\r\nFix:%08lX\r\nHop:%08lX\r\nSn:%07lX Btn:%X",
             d->decoded_bits, (unsigned long)(uint32_t)(v >> 32),
             (unsigned long)(uint32_t)v, (unsigned long)fix, (unsigned long)hop,
             (unsigned long)serial, btn);
    return b;
}

static const subghz_decoder_ops_t faac_decoder_ops = {
    .feed = faac_feed, .reset = faac_reset, .get_data = faac_get_data,
    .get_string = faac_get_string,
};

/* ============================================================
 * SCHER-KHAN ("Scher-Khan", 433 FM): te 750/1100/150, rolling.
 * Preamble of H/L 2TE pairs (2+), start on H 1TE, bits 0 = H1/L1,
 * 1 = H2/L2 (same-TE pairs). A HIGH >= long + 2*delta ends the
 * frame with 35+ bits; length selects the MAGIC CODE variant.
 * ============================================================ */

typedef struct {
    uint8_t step;
    uint32_t te_last;
    uint8_t header_count;
    uint32_t total_bits; /* Frame length incl. overflow past 64-bit storage. */
} scher_priv_t;

enum { SCHER_RESET = 0, SCHER_PREAMBLE, SCHER_SAVE, SCHER_CHECK };

#define SCHER_SHORT 750u
#define SCHER_LONG 1100u
#define SCHER_D 150u
#define SCHER_MIN_BITS 35u

static const char *scher_variant(
    uint32_t bits, uint64_t data, uint32_t *serial, uint8_t *btn, uint16_t *cnt) {
    const char *name = "Unknown";
    *serial = 0;
    *btn = 0;
    *cnt = 0;
    switch (bits) {
    case 35:
        name = "MAGIC CODE, Static";
        break;
    case 51:
        name = "MAGIC CODE, Dynamic";
        *serial = (uint32_t)(((data >> 24) & 0xffffff0u) | ((data >> 20) & 0xfu));
        *btn = (uint8_t)((data >> 24) & 0xfu);
        *cnt = (uint16_t)(data & 0xffffu);
        break;
    case 57:
        name = "MAGIC CODE PRO/PRO2";
        break;
    case 63:
        name = "MAGIC CODE, Response";
        break;
    case 64:
        name = "MAGICAR, Response";
        break;
    case 81:
    case 82:
        name = "MAGIC CODE PRO,\n Response";
        break;
    default:
        break;
    }
    return name;
}

static subghz_protocol_status_t scher_feed(
    subghz_protocol_decoder_t *d, bool level, uint32_t dur) {
    scher_priv_t *p = d->priv;
    switch (p->step) {
    case SCHER_RESET:
        if (level && subghz_duration_match(dur, SCHER_SHORT * 2u, SCHER_D)) {
            p->step = SCHER_PREAMBLE;
            p->te_last = dur;
            p->header_count = 0;
        }
        break;
    case SCHER_PREAMBLE:
        if (level) {
            if (subghz_duration_match(dur, SCHER_SHORT * 2u, SCHER_D) ||
                subghz_duration_match(dur, SCHER_SHORT, SCHER_D)) {
                p->te_last = dur;
            } else {
                p->step = SCHER_RESET;
            }
        } else if (
            subghz_duration_match(dur, SCHER_SHORT * 2u, SCHER_D) ||
            subghz_duration_match(dur, SCHER_SHORT, SCHER_D)) {
            if (subghz_duration_match(p->te_last, SCHER_SHORT * 2u, SCHER_D)) {
                p->header_count++;
            } else if (subghz_duration_match(p->te_last, SCHER_SHORT, SCHER_D)) {
                if (p->header_count >= 2u) {
                    p->step = SCHER_SAVE;
                    d->decoded_data = 0;
                    d->decoded_bits = 1u;
                    d->te_us = SCHER_SHORT;
                    p->total_bits = 1u;
                } else {
                    p->step = SCHER_RESET;
                }
            } else {
                p->step = SCHER_RESET;
            }
        } else {
            p->step = SCHER_RESET;
        }
        break;
    case SCHER_SAVE:
        if (level) {
            if (dur >= SCHER_LONG + SCHER_D * 2u) {
                p->step = SCHER_RESET;
                /* 81/82-bit PRO responses exceed 64-bit storage: refuse
                 * rather than report truncated data. */
                if (p->total_bits >= SCHER_MIN_BITS && p->total_bits <= 64u) {
                    decoder_finish(d);
                    return SUBGHZ_PROTOCOL_DONE;
                }
                d->decoded_data = 0;
                d->decoded_bits = 0;
                break;
            }
            p->te_last = dur;
            p->step = SCHER_CHECK;
        } else {
            p->step = SCHER_RESET;
        }
        break;
    case SCHER_CHECK:
        if (!level) {
            if (subghz_duration_match(p->te_last, SCHER_SHORT, SCHER_D) &&
                subghz_duration_match(dur, SCHER_SHORT, SCHER_D)) {
                decoder_add_bit(d, 0);
                p->total_bits++;
                p->step = SCHER_SAVE;
            } else if (
                subghz_duration_match(p->te_last, SCHER_LONG, SCHER_D) &&
                subghz_duration_match(dur, SCHER_LONG, SCHER_D)) {
                decoder_add_bit(d, 1);
                p->total_bits++;
                p->step = SCHER_SAVE;
            } else {
                p->step = SCHER_RESET;
            }
        } else {
            p->step = SCHER_RESET;
        }
        break;
    default:
        p->step = SCHER_RESET;
        break;
    }
    return SUBGHZ_PROTOCOL_SYNC;
}

static void scher_reset(subghz_protocol_decoder_t *d) {
    memset(d->priv, 0, sizeof(scher_priv_t));
    d->decoded_data = 0;
    d->decoded_bits = 0;
    d->hash = 0;
    d->te_us = 0;
}

static const char *scher_get_data(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->decoded_bits < SCHER_MIN_BITS) {
        return NULL;
    }
    snprintf(b, s, "Scher-Khan %ubit: 0x%08lx%08lx", d->decoded_bits,
             (unsigned long)(d->decoded_data >> 32), (unsigned long)d->decoded_data);
    return b;
}

static const char *scher_get_string(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->decoded_bits < SCHER_MIN_BITS || d->decoded_bits > 82u) {
        return NULL;
    }
    /* Detail fields are 64-bit; longer captures show the low 64 bits. */
    uint64_t v = d->decoded_data;
    uint32_t serial = 0;
    uint8_t btn = 0;
    uint16_t cnt = 0;
    const char *variant = scher_variant(d->decoded_bits, v, &serial, &btn, &cnt);
    snprintf(b, s, "Scher-Khan %ubit\r\nKey:0x%lX%08lX\r\nSn:%07lX Btn:%X Cnt:%04X\r\nPt: %s\r\n",
             d->decoded_bits, (unsigned long)(uint32_t)(v >> 32),
             (unsigned long)(uint32_t)v, (unsigned long)serial, btn, cnt, variant);
    return b;
}

static const subghz_decoder_ops_t scher_decoder_ops = {
    .feed = scher_feed, .reset = scher_reset, .get_data = scher_get_data,
    .get_string = scher_get_string,
};

/* ============================================================
 * IDO ("iDo 117/111"): te 450/1450/150, 48+ bits rolling.
 * Preamble HIGH/LOW 10*short; bits high-first 0 = H1/L3.2,
 * 1 = H1/L1. A HIGH >= 5*short ends the frame (and re-arms the
 * preamble); fix/hop halves come from the bit-reversed payload.
 * ============================================================ */

typedef struct {
    uint8_t step;
    uint32_t te_last;
    uint32_t total_bits; /* Frame length incl. overflow past 64-bit storage. */
} ido_priv_t;

enum { IDO_RESET = 0, IDO_PREAMBLE, IDO_SAVE, IDO_CHECK };

#define IDO_SHORT 450u
#define IDO_LONG 1450u
#define IDO_D 150u
#define IDO_MIN_BITS 48u

static subghz_protocol_status_t ido_feed(
    subghz_protocol_decoder_t *d, bool level, uint32_t dur) {
    ido_priv_t *p = d->priv;
    switch (p->step) {
    case IDO_RESET:
        if (level && subghz_duration_match(dur, IDO_SHORT * 10u, IDO_D * 5u)) {
            p->step = IDO_PREAMBLE;
        }
        break;
    case IDO_PREAMBLE:
        if (!level && subghz_duration_match(dur, IDO_SHORT * 10u, IDO_D * 5u)) {
            p->step = IDO_SAVE;
            d->decoded_data = 0;
            d->decoded_bits = 0;
            d->te_us = IDO_SHORT;
            p->total_bits = 0;
        } else {
            p->step = IDO_RESET;
        }
        break;
    case IDO_SAVE:
        if (level) {
            if (dur >= IDO_SHORT * 5u + IDO_D) {
                p->step = IDO_PREAMBLE;
                if (p->total_bits >= IDO_MIN_BITS && p->total_bits <= 64u) {
                    decoder_finish(d);
                    return SUBGHZ_PROTOCOL_DONE;
                }
                d->decoded_data = 0;
                d->decoded_bits = 0;
                break;
            }
            p->te_last = dur;
            p->step = IDO_CHECK;
        } else {
            p->step = IDO_RESET;
        }
        break;
    case IDO_CHECK:
        if (!level) {
            if (subghz_duration_match(p->te_last, IDO_SHORT, IDO_D) &&
                subghz_duration_match(dur, IDO_LONG, IDO_D * 3u)) {
                decoder_add_bit(d, 0);
                p->total_bits++;
                p->step = IDO_SAVE;
            } else if (
                subghz_duration_match(p->te_last, IDO_SHORT, IDO_D * 3u) &&
                subghz_duration_match(dur, IDO_SHORT, IDO_D)) {
                decoder_add_bit(d, 1);
                p->total_bits++;
                p->step = IDO_SAVE;
            } else {
                p->step = IDO_RESET;
            }
        } else {
            p->step = IDO_RESET;
        }
        break;
    default:
        p->step = IDO_RESET;
        break;
    }
    return SUBGHZ_PROTOCOL_SYNC;
}

static void ido_reset(subghz_protocol_decoder_t *d) {
    memset(d->priv, 0, sizeof(ido_priv_t));
    d->decoded_data = 0;
    d->decoded_bits = 0;
    d->hash = 0;
    d->te_us = 0;
}

static const char *ido_get_data(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->decoded_bits < IDO_MIN_BITS) {
        return NULL;
    }
    snprintf(b, s, "iDo 117/111 %ubit: 0x%08lx%08lx", d->decoded_bits,
             (unsigned long)(d->decoded_data >> 32), (unsigned long)d->decoded_data);
    return b;
}

static const char *ido_get_string(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->decoded_bits < IDO_MIN_BITS) {
        return NULL;
    }
    uint64_t v = d->decoded_data;
    uint64_t rev = reverse_key(v, d->decoded_bits);
    uint32_t fix = (uint32_t)(rev & 0xffffffu);
    uint32_t hop = (uint32_t)((rev >> 24) & 0xffffffu);
    uint32_t serial = fix & 0xfffffu;
    uint8_t btn = (uint8_t)((fix >> 20) & 0xfu);
    snprintf(b, s, "iDo 117/111 %ubit\r\nKey:0x%lX%08lX\r\nFix:%06lX\r\nHop:%06lX\r\nSn:%05lX Btn:%X",
             d->decoded_bits, (unsigned long)(uint32_t)(v >> 32),
             (unsigned long)(uint32_t)v, (unsigned long)fix, (unsigned long)hop,
             (unsigned long)serial, btn);
    return b;
}

static const subghz_decoder_ops_t ido_decoder_ops = {
    .feed = ido_feed, .reset = ido_reset, .get_data = ido_get_data,
    .get_string = ido_get_string,
};

/* ============================================================
 * KIA ("KIA Seed"): te 250/500/100, 61 bits rolling.
 * Preamble of 16+ H/L 1TE pairs, start H/L 2TE (seeds bit 1),
 * bits 0 = H1/L1, 1 = H2/L2, stop on HIGH >= long + 2*delta.
 * ============================================================ */

typedef struct {
    uint8_t step;
    uint32_t te_last;
    uint8_t header_count;
} kia_priv_t;

enum { KIA_RESET = 0, KIA_PREAMBLE, KIA_SAVE, KIA_CHECK };

#define KIA_SHORT 250u
#define KIA_LONG 500u
#define KIA_D 100u
#define KIA_BITS 61u

static subghz_protocol_status_t kia_feed(
    subghz_protocol_decoder_t *d, bool level, uint32_t dur) {
    kia_priv_t *p = d->priv;
    switch (p->step) {
    case KIA_RESET:
        if (level && subghz_duration_match(dur, KIA_SHORT, KIA_D)) {
            p->step = KIA_PREAMBLE;
            p->te_last = dur;
            p->header_count = 0;
        }
        break;
    case KIA_PREAMBLE:
        if (level) {
            if (subghz_duration_match(dur, KIA_SHORT, KIA_D) ||
                subghz_duration_match(dur, KIA_LONG, KIA_D)) {
                p->te_last = dur;
            } else {
                p->step = KIA_RESET;
            }
        } else if (
            subghz_duration_match(dur, KIA_SHORT, KIA_D) &&
            subghz_duration_match(p->te_last, KIA_SHORT, KIA_D)) {
            p->header_count++;
        } else if (
            subghz_duration_match(dur, KIA_LONG, KIA_D) &&
            subghz_duration_match(p->te_last, KIA_LONG, KIA_D)) {
            if (p->header_count > 15u) {
                p->step = KIA_SAVE;
                d->decoded_data = 0;
                d->decoded_bits = 1u;
                d->te_us = KIA_SHORT;
                decoder_add_bit(d, 1);
            } else {
                p->step = KIA_RESET;
            }
        } else {
            p->step = KIA_RESET;
        }
        break;
    case KIA_SAVE:
        if (level) {
            if (dur >= KIA_LONG + KIA_D * 2u) {
                p->step = KIA_RESET;
                if (d->decoded_bits == KIA_BITS) {
                    decoder_finish(d);
                    return SUBGHZ_PROTOCOL_DONE;
                }
                d->decoded_data = 0;
                d->decoded_bits = 0;
                break;
            }
            p->te_last = dur;
            p->step = KIA_CHECK;
        } else {
            p->step = KIA_RESET;
        }
        break;
    case KIA_CHECK:
        if (!level) {
            if (subghz_duration_match(p->te_last, KIA_SHORT, KIA_D) &&
                subghz_duration_match(dur, KIA_SHORT, KIA_D)) {
                decoder_add_bit(d, 0);
                p->step = KIA_SAVE;
            } else if (
                subghz_duration_match(p->te_last, KIA_LONG, KIA_D) &&
                subghz_duration_match(dur, KIA_LONG, KIA_D)) {
                decoder_add_bit(d, 1);
                p->step = KIA_SAVE;
            } else {
                p->step = KIA_RESET;
            }
        } else {
            p->step = KIA_RESET;
        }
        break;
    default:
        p->step = KIA_RESET;
        break;
    }
    return SUBGHZ_PROTOCOL_SYNC;
}

static void kia_reset(subghz_protocol_decoder_t *d) {
    memset(d->priv, 0, sizeof(kia_priv_t));
    d->decoded_data = 0;
    d->decoded_bits = 0;
    d->hash = 0;
    d->te_us = 0;
}

static const char *kia_get_data(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->decoded_bits != KIA_BITS) {
        return NULL;
    }
    snprintf(b, s, "KIA Seed %ubit: 0x%08lx%08lx", d->decoded_bits,
             (unsigned long)(d->decoded_data >> 32), (unsigned long)d->decoded_data);
    return b;
}

static const char *kia_get_string(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->decoded_bits != KIA_BITS) {
        return NULL;
    }
    uint64_t v = d->decoded_data;
    uint32_t serial = (uint32_t)((v >> 12) & 0x0ffffffful);
    uint8_t btn = (uint8_t)((v >> 8) & 0xfu);
    uint16_t cnt = (uint16_t)((v >> 40) & 0xffffu);
    snprintf(b, s, "KIA Seed %ubit\r\nKey:%08lX%08lX\r\nSn:%07lX Btn:%X Cnt:%04X",
             d->decoded_bits, (unsigned long)(uint32_t)(v >> 32),
             (unsigned long)(uint32_t)v, (unsigned long)serial, btn, cnt);
    return b;
}

static const subghz_decoder_ops_t kia_decoder_ops = {
    .feed = kia_feed, .reset = kia_reset, .get_data = kia_get_data,
    .get_string = kia_get_string,
};

/* ============================================================
 * SOMFY RTS ("Somfy Telis" 56-bit, "Somfy Keytis" 56+24-bit):
 * te 640/1280/250. Rolling (decode-only, no TX).
 *
 * Frame: HW sync (H/L 4TE pairs) + SW sync (H 7TE), then 56
 * manchester cells; Keytis appends a 24-bit press-duration counter.
 * The manchester engine is primed with Reset + LongHigh after the
 * SW sync (Flipper framing). Payload is descrambled as
 * data ^ (data >> 8): key byte, button/checksum nibbles, 16-bit
 * rolling counter (BE), 24-bit serial (LE bytes). Checksum: XOR of
 * all bytes and half-bytes with the button nibble masked out.
 * ============================================================ */

typedef struct {
    uint8_t step;
    uint8_t header_count;
    man_state_t man;
    uint32_t pdc;
    uint32_t pdc_bits;
} somfy_priv_t;

enum { SOMFY_RESET = 0, SOMFY_PREAMBLE, SOMFY_CHECK, SOMFY_DATA };

#define SOMFY_SHORT 640u
#define SOMFY_LONG 1280u
#define SOMFY_D 250u
#define SOMFY_KEY_BITS 56u
#define SOMFY_PDC_BITS 24u

static uint8_t somfy_crc(uint64_t data) {
    uint8_t crc = 0;
    data &= 0xfff0ffffffffffull;
    for (uint8_t i = 0; i < 56u; i += 8u) {
        crc = (uint8_t)(crc ^ (data >> i) ^ (data >> (i + 4u)));
    }
    return (uint8_t)(crc & 0xfu);
}

/* Shared feed; total_bits is 56 (Telis) or 80 (Keytis, +24 pdc bits). */
static subghz_protocol_status_t somfy_feed(
    subghz_protocol_decoder_t *d, uint32_t total_bits, bool level, uint32_t dur) {
    somfy_priv_t *p = d->priv;
    man_event_t event = MAN_EV_RESET;
    switch (p->step) {
    case SOMFY_RESET:
        if (level && subghz_duration_match(dur, SOMFY_SHORT * 4u, SOMFY_D * 4u)) {
            p->step = SOMFY_PREAMBLE;
            p->header_count++;
        }
        break;
    case SOMFY_PREAMBLE:
        if (!level && subghz_duration_match(dur, SOMFY_SHORT * 4u, SOMFY_D * 4u)) {
            p->step = SOMFY_CHECK;
        } else {
            p->header_count = 0;
            p->step = SOMFY_RESET;
        }
        break;
    case SOMFY_CHECK:
        if (level) {
            if (subghz_duration_match(dur, SOMFY_SHORT * 4u, SOMFY_D * 4u)) {
                p->step = SOMFY_PREAMBLE;
                p->header_count++;
            } else if (
                p->header_count > 1u &&
                subghz_duration_match(dur, SOMFY_SHORT * 7u, SOMFY_D * 4u)) {
                p->step = SOMFY_DATA;
                d->decoded_data = 0;
                d->decoded_bits = 0;
                d->te_us = SOMFY_SHORT;
                p->pdc = 0;
                p->pdc_bits = 0;
                p->header_count = 0;
                man_advance(p->man, MAN_EV_RESET, &p->man, NULL);
                man_advance(p->man, MAN_EV_LONG_HIGH, &p->man, NULL);
            }
        }
        break;
    case SOMFY_DATA:
        if (!level) {
            if (subghz_duration_match(dur, SOMFY_SHORT, SOMFY_D)) {
                event = MAN_EV_SHORT_LOW;
            } else if (subghz_duration_match(dur, SOMFY_LONG, SOMFY_D)) {
                event = MAN_EV_LONG_LOW;
            } else if (dur >= SOMFY_LONG + SOMFY_D) {
                uint32_t total = d->decoded_bits + p->pdc_bits;
                if (total == total_bits) {
                    /* NOTE: upstream Keytis validates against the STALE
                     * generic.data here; validating the current frame
                     * (decode_data) is the obvious intent. */
                    uint64_t raw = d->decoded_data;
                    uint64_t dt = raw ^ (raw >> 8);
                    if (((dt >> 40) & 0xfu) == somfy_crc(dt)) {
                        decoder_finish(d);
                        p->step = SOMFY_RESET;
                        return SUBGHZ_PROTOCOL_DONE;
                    }
                }
                d->decoded_data = 0;
                d->decoded_bits = 0;
                p->pdc = 0;
                p->pdc_bits = 0;
                man_advance(p->man, MAN_EV_RESET, &p->man, NULL);
                man_advance(p->man, MAN_EV_LONG_HIGH, &p->man, NULL);
                p->step = SOMFY_RESET;
            } else {
                p->step = SOMFY_RESET;
            }
        } else {
            if (subghz_duration_match(dur, SOMFY_SHORT, SOMFY_D)) {
                event = MAN_EV_SHORT_HIGH;
            } else if (subghz_duration_match(dur, SOMFY_LONG, SOMFY_D)) {
                event = MAN_EV_LONG_HIGH;
            } else {
                p->step = SOMFY_RESET;
            }
        }
        if (event != MAN_EV_RESET) {
            bool bit = false;
            if (man_advance(p->man, event, &p->man, &bit)) {
                if (d->decoded_bits < SOMFY_KEY_BITS) {
                    d->decoded_data = (d->decoded_data << 1) | (bit & 1u);
                    d->decoded_bits++;
                } else if (p->pdc_bits < SOMFY_PDC_BITS) {
                    p->pdc = (p->pdc << 1) | (bit & 1u);
                    p->pdc_bits++;
                }
            }
        }
        break;
    default:
        p->step = SOMFY_RESET;
        break;
    }
    return SUBGHZ_PROTOCOL_SYNC;
}

static void somfy_reset(subghz_protocol_decoder_t *d) {
    memset(d->priv, 0, sizeof(somfy_priv_t));
    d->decoded_data = 0;
    d->decoded_bits = 0;
    d->hash = 0;
    d->te_us = 0;
    ((somfy_priv_t *)d->priv)->man = MAN_ST_MID1;
}

static subghz_protocol_status_t somfy_telis_feed(
    subghz_protocol_decoder_t *d, bool level, uint32_t dur) {
    return somfy_feed(d, SOMFY_KEY_BITS, level, dur);
}

static subghz_protocol_status_t somfy_keytis_feed(
    subghz_protocol_decoder_t *d, bool level, uint32_t dur) {
    return somfy_feed(d, SOMFY_KEY_BITS + SOMFY_PDC_BITS, level, dur);
}

static const char *somfy_telis_get_data(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->decoded_bits != SOMFY_KEY_BITS) {
        return NULL;
    }
    snprintf(b, s, "Somfy Telis %ubit: 0x%08lx%08lx", d->decoded_bits,
             (unsigned long)(d->decoded_data >> 32), (unsigned long)d->decoded_data);
    return b;
}

static const char *somfy_keytis_get_data(subghz_protocol_decoder_t *d, char *b, size_t s) {
    const somfy_priv_t *p = d->priv;
    if (d->decoded_bits != SOMFY_KEY_BITS || p->pdc_bits != SOMFY_PDC_BITS) {
        return NULL;
    }
    snprintf(b, s, "Somfy Keytis %ubit: 0x%08lx%08lx+%06lx", d->decoded_bits,
             (unsigned long)(d->decoded_data >> 32), (unsigned long)d->decoded_data,
             (unsigned long)p->pdc);
    return b;
}

static const char *somfy_button_name(uint8_t btn) {
    static const char *const names[] = {
        "Unknown", "My",      "Up",     "My+Up", "Down", "My+Down",
        "Up+Down", "0x07",    "Prog",   "Sun+Flag", "Flag", "0x0B",
        "0x0C",    "0x0D",    "0x0E",   "0x0F",
    };
    return names[btn & 0xfu];
}

static const char *somfy_telis_get_string(subghz_protocol_decoder_t *d, char *b, size_t s) {
    if (d->decoded_bits != SOMFY_KEY_BITS) {
        return NULL;
    }
    uint64_t v = d->decoded_data;
    uint64_t dt = v ^ (v >> 8);
    uint8_t btn = (uint8_t)((dt >> 44) & 0xfu);
    uint16_t cnt = (uint16_t)((dt >> 24) & 0xffffu);
    uint32_t serial = (uint32_t)(dt & 0xffffffu);
    snprintf(b, s, "Somfy Telis %ubit\r\nKey:0x%lX%08lX\r\nSn:0x%06lX\r\nCnt:0x%04X\r\nBtn:%s",
             d->decoded_bits, (unsigned long)(v >> 32), (unsigned long)(uint32_t)v,
             (unsigned long)serial, cnt, somfy_button_name(btn));
    return b;
}

static const char *somfy_keytis_get_string(subghz_protocol_decoder_t *d, char *b, size_t s) {
    const somfy_priv_t *p = d->priv;
    if (d->decoded_bits != SOMFY_KEY_BITS || p->pdc_bits != SOMFY_PDC_BITS) {
        return NULL;
    }
    uint64_t v = d->decoded_data;
    uint64_t dt = v ^ (v >> 8);
    uint8_t btn = (uint8_t)((dt >> 48) & 0xfu);
    uint16_t cnt = (uint16_t)((dt >> 24) & 0xffffu);
    uint32_t serial = (uint32_t)(dt & 0xffffffu);
    snprintf(b, s,
             "Somfy Keytis %ubit\r\n%lX%08lX%06lX\r\nSn:0x%06lX\r\nCnt:0x%04X\r\nBtn:%s\r\n",
             d->decoded_bits, (unsigned long)(v >> 32), (unsigned long)(uint32_t)v,
             (unsigned long)p->pdc, (unsigned long)serial, cnt, somfy_button_name(btn));
    return b;
}

static const subghz_decoder_ops_t somfy_telis_decoder_ops = {
    .feed = somfy_telis_feed, .reset = somfy_reset, .get_data = somfy_telis_get_data,
    .get_string = somfy_telis_get_string,
};

static const subghz_decoder_ops_t somfy_keytis_decoder_ops = {
    .feed = somfy_keytis_feed, .reset = somfy_reset, .get_data = somfy_keytis_get_data,
    .get_string = somfy_keytis_get_string,
};

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
static const subghz_protocol_info_t ht12x_info = {
    /* Flipper .sub name is "Holtek_HT12X" (replaces our older approximation). */
    .name = "Holtek_HT12X", .description = "Holtek HT12X 12-bit OOK",
    .frequency_default_khz = 433920, .te_min_us = 220, .te_max_us = 420,
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
static const subghz_protocol_info_t dickert_info = {
    /* Flipper .sub name is "Dickert_MAHS". */
    .name = "Dickert_MAHS", .description = "Dickert MAHS 36-bit OOK",
    .frequency_default_khz = 433920, .te_min_us = 300, .te_max_us = 500,
    .bit_min = 36, .bit_max = 36, .rolling = false,
};
static const subghz_protocol_info_t delta3_info = {
    /* Flipper .sub name is "LinearDelta3" (315 MHz dip-switch remote). */
    .name = "LinearDelta3", .description = "Linear Delta3 8-bit OOK",
    .frequency_default_khz = 315000, .te_min_us = 400, .te_max_us = 600,
    .bit_min = 8, .bit_max = 8, .rolling = false,
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
static const subghz_protocol_info_t rb2_info = {
    /* Flipper .sub name is "Revers_RB2". */
    .name = "Revers_RB2", .description = "Revers RB2 64-bit Manchester OOK",
    .frequency_default_khz = 433920, .te_min_us = 150, .te_max_us = 350,
    .bit_min = 64, .bit_max = 64, .rolling = false,
};
static const subghz_protocol_info_t powersmart_info = {
    /* Flipper .sub name is "Power Smart". */
    .name = "Power Smart", .description = "Power Smart 64-bit Manchester OOK",
    .frequency_default_khz = 433920, .te_min_us = 150, .te_max_us = 300,
    .bit_min = 64, .bit_max = 64, .rolling = false,
};
static const subghz_protocol_info_t marantec_info = {
    /* Flipper .sub name is "Marantec" (49-bit, distinct from Marantec24). */
    .name = "Marantec", .description = "Marantec 49-bit Manchester OOK",
    .frequency_default_khz = 433920, .te_min_us = 800, .te_max_us = 1200,
    .bit_min = 49, .bit_max = 49, .rolling = false,
};
static const subghz_protocol_info_t chamb_info = {
    /* Flipper .sub name is "Cham_Code" (315 MHz, 7/8/9 DIP bits). */
    .name = "Cham_Code", .description = "Chamberlain Code 7/8/9-bit OOK",
    .frequency_default_khz = 315000, .te_min_us = 800, .te_max_us = 1200,
    .bit_min = 7, .bit_max = 9, .rolling = false,
};
static const subghz_protocol_info_t somfy_telis_info = {
    /* Flipper .sub name is "Somfy Telis" (rolling, decode-only). */
    .name = "Somfy Telis", .description = "Somfy RTS Telis 56-bit rolling",
    .frequency_default_khz = 433920, .te_min_us = 450, .te_max_us = 800,
    .bit_min = 56, .bit_max = 56, .rolling = true,
};
static const subghz_protocol_info_t somfy_keytis_info = {
    /* Flipper .sub name is "Somfy Keytis" (rolling, decode-only). */
    .name = "Somfy Keytis", .description = "Somfy RTS Keytis 80-bit rolling",
    .frequency_default_khz = 433920, .te_min_us = 450, .te_max_us = 800,
    .bit_min = 80, .bit_max = 80, .rolling = true,
};
static const subghz_protocol_info_t hay21_info = {
    /* Flipper .sub name is "Hay21" (rolling, decode-only). */
    .name = "Hay21", .description = "Hay21 21-bit rolling",
    .frequency_default_khz = 433920, .te_min_us = 200, .te_max_us = 400,
    .bit_min = 21, .bit_max = 21, .rolling = true,
};
static const subghz_protocol_info_t magellan_info = {
    /* Flipper .sub name is "Magellan". */
    .name = "Magellan", .description = "Magellan 32-bit OOK",
    .frequency_default_khz = 433920, .te_min_us = 150, .te_max_us = 250,
    .bit_min = 32, .bit_max = 32, .rolling = false,
};
static const subghz_protocol_info_t twee_info = {
    /* Flipper .sub name is "CAME TWEE" (rolling parcels). */
    .name = "CAME TWEE", .description = "CAME TWEE 54-bit rolling",
    .frequency_default_khz = 433920, .te_min_us = 400, .te_max_us = 600,
    .bit_min = 54, .bit_max = 54, .rolling = false,
};
static const subghz_protocol_info_t faac_info = {
    /* Flipper .sub name is "Faac SLH" (decode-only upstream too). */
    .name = "Faac SLH", .description = "FAAC SLH 64-bit rolling",
    .frequency_default_khz = 433920, .te_min_us = 180, .te_max_us = 330,
    .bit_min = 64, .bit_max = 64, .rolling = true,
};
static const subghz_protocol_info_t scher_info = {
    /* Flipper .sub name is "Scher-Khan" (decode-only, 433 FM). */
    .name = "Scher-Khan", .description = "Scher-Khan MAGICAR rolling",
    .frequency_default_khz = 433920, .te_min_us = 600, .te_max_us = 900,
    .bit_min = 35, .bit_max = 82, .rolling = true,
};
static const subghz_protocol_info_t ido_info = {
    /* Flipper .sub name is "iDo 117/111" (decode-only). */
    .name = "iDo 117/111", .description = "iDo 48-bit rolling",
    .frequency_default_khz = 433920, .te_min_us = 350, .te_max_us = 550,
    .bit_min = 48, .bit_max = 64, .rolling = true,
};
static const subghz_protocol_info_t kia_info = {
    /* Flipper .sub name is "KIA Seed" (decode-only). */
    .name = "KIA Seed", .description = "KIA Seed 61-bit rolling",
    .frequency_default_khz = 433920, .te_min_us = 180, .te_max_us = 320,
    .bit_min = 61, .bit_max = 61, .rolling = true,
};

static const subghz_protocol_info_t rcswitch_info = {
    .name = "RcSwitch", .description = "RcSwitch OOK (proto 1/2/11)",
    .frequency_default_khz = 433920, .te_min_us = 100, .te_max_us = 1000,
    .bit_min = 12, .bit_max = 64, .rolling = false,
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
    { .info = &ht12x_info, .decoder_ops = &ht12x_decoder_ops,
      .encoder_ops = &ht12x_encoder_ops, .decoder_size = sizeof(ht12x_priv_t),
      .encoder_size = sizeof(ook_enc_priv_t) },
    { .info = &twee_info, .decoder_ops = &twee_decoder_ops,
      .encoder_ops = &twee_encoder_ops, .decoder_size = sizeof(twee_priv_t),
      .encoder_size = sizeof(ook_enc_priv_t) },
    { .info = &faac_info, .decoder_ops = &faac_decoder_ops,
      .encoder_ops = NULL, .decoder_size = sizeof(faac_priv_t), .encoder_size = 0 },
    { .info = &scher_info, .decoder_ops = &scher_decoder_ops,
      .encoder_ops = NULL, .decoder_size = sizeof(scher_priv_t), .encoder_size = 0 },
    { .info = &ido_info, .decoder_ops = &ido_decoder_ops,
      .encoder_ops = NULL, .decoder_size = sizeof(ido_priv_t), .encoder_size = 0 },
    { .info = &kia_info, .decoder_ops = &kia_decoder_ops,
      .encoder_ops = NULL, .decoder_size = sizeof(kia_priv_t), .encoder_size = 0 },
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
    { .info = &dickert_info, .decoder_ops = &dickert_decoder_ops,
      .encoder_ops = &dickert_encoder_ops, .decoder_size = sizeof(pwm_priv_t),
      .encoder_size = sizeof(ook_enc_priv_t) },
    { .info = &delta3_info, .decoder_ops = &delta3_decoder_ops,
      .encoder_ops = &delta3_encoder_ops, .decoder_size = sizeof(delta3_priv_t),
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
    { .info = &rb2_info, .decoder_ops = &rb2_decoder_ops,
      .encoder_ops = &rb2_encoder_ops, .decoder_size = sizeof(rb2_priv_t),
      .encoder_size = sizeof(ook_enc_priv_t) },
    { .info = &powersmart_info, .decoder_ops = &powersmart_decoder_ops,
      .encoder_ops = &powersmart_encoder_ops, .decoder_size = sizeof(ps_priv_t),
      .encoder_size = sizeof(ook_enc_priv_t) },
    { .info = &marantec_info, .decoder_ops = &marantec_decoder_ops,
      .encoder_ops = &marantec_encoder_ops, .decoder_size = sizeof(marantec_priv_t),
      .encoder_size = sizeof(ook_enc_priv_t) },
    { .info = &chamb_info, .decoder_ops = &chamb_decoder_ops,
      .encoder_ops = &chamb_encoder_ops, .decoder_size = sizeof(chamb_priv_t),
      .encoder_size = sizeof(ook_enc_priv_t) },
    { .info = &somfy_telis_info, .decoder_ops = &somfy_telis_decoder_ops,
      .encoder_ops = NULL, .decoder_size = sizeof(somfy_priv_t), .encoder_size = 0 },
    { .info = &somfy_keytis_info, .decoder_ops = &somfy_keytis_decoder_ops,
      .encoder_ops = NULL, .decoder_size = sizeof(somfy_priv_t), .encoder_size = 0 },
    { .info = &hay21_info, .decoder_ops = &hay21_decoder_ops,
      .encoder_ops = NULL, .decoder_size = sizeof(pwm_priv_t), .encoder_size = 0 },
    { .info = &magellan_info, .decoder_ops = &magellan_decoder_ops,
      .encoder_ops = &magellan_encoder_ops, .decoder_size = sizeof(magellan_priv_t),
      .encoder_size = sizeof(ook_enc_priv_t) },
    { .info = &rcswitch_info, .decoder_ops = &rcswitch_decoder_ops,
      .encoder_ops = &rcswitch_encoder_ops, .decoder_size = sizeof(rcswitch_priv_t),
      .encoder_size = sizeof(rcswitch_enc_priv_t) },
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
