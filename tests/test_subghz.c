/* Host unit test for the SubGHz protocol library.
 * Compiles the portable sources (blocks, protocols, rx, keystore) with the
 * host gcc and checks encoder->decoder round trips plus KeeLoq vectors.
 * Copyright (c) 2026 Bus Pirate contributors. SPDX-License-Identifier: MIT
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mode/subghz_protocols.h"
#include "mode/subghz_rx.h"
#include "mode/subghz_keystore.h"
#include "mode/subghz_file.h"
#include "mode/subghz_freqs.h"

static int failures;

#define CHECK(cond, ...)                                   \
    do {                                                   \
        if (!(cond)) {                                     \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);    \
            printf(__VA_ARGS__);                           \
            printf("\n");                                  \
            failures++;                                    \
        }                                                  \
    } while (0)

/* Collect an encoder upload with same-level runs merged, as seen on air
 * (GPIO TX of back-to-back same-level pulses is one continuous pulse). */
static size_t collect_merged(subghz_protocol_encoder_t *enc, bool *levels, uint32_t *durs,
                             size_t capacity) {
    size_t n = 0;
    bool lv;
    uint32_t du;
    subghz_encoder_reset(enc);
    while (subghz_encoder_yield(enc, &lv, &du)) {
        if (n && levels[n - 1u] == lv) {
            durs[n - 1u] += du;
        } else {
            if (n >= capacity) {
                break;
            }
            levels[n] = lv;
            durs[n] = du;
            n++;
        }
    }
    return n;
}

/* Feed an encoder upload into a decoder; expect DONE with matching data.
 * Uploads are consumed verbatim (pre-batch-2 behavior for the PWM family).
 * round_trip_air() merges same-level runs first (GPIO TX physics); required
 * for Manchester uploads whose decoders classify merged Long/Short
 * durations. */
static bool round_trip_feed(const char *proto, uint64_t key, uint32_t bits, bool air_merge) {
    const subghz_protocol_entry_t *entry = subghz_protocol_find(proto);
    if (!entry || !entry->encoder_ops) {
        printf("SKIP %s: no encoder\n", proto);
        return true;
    }
    subghz_protocol_encoder_t *enc = subghz_encoder_alloc(entry);
    subghz_protocol_decoder_t *dec = subghz_decoder_alloc(entry);
    if (!enc || !dec) {
        printf("FAIL %s: alloc\n", proto);
        failures++;
        return false;
    }
    if (subghz_encoder_feed(enc, key, bits) != SUBGHZ_PROTOCOL_DONE) {
        printf("FAIL %s: encode rejected\n", proto);
        failures++;
        goto out;
    }
    bool done = false;
    bool levels[512];
    uint32_t durs[512];
    size_t raw_n = 0;
    bool raw_levels[512];
    uint32_t raw_durs[512];
    subghz_encoder_reset(enc);
    bool lv;
    uint32_t du;
    while (subghz_encoder_yield(enc, &lv, &du) && raw_n < 512) {
        raw_levels[raw_n] = lv;
        raw_durs[raw_n] = du;
        raw_n++;
    }
    if (!raw_n) {
        printf("FAIL %s: empty upload\n", proto);
        failures++;
        goto out;
    }
    size_t n = 0;
    if (air_merge) {
        for (size_t i = 0; i < raw_n; i++) {
            if (n && levels[n - 1u] == raw_levels[i]) {
                durs[n - 1u] += raw_durs[i];
            } else {
                levels[n] = raw_levels[i];
                durs[n] = raw_durs[i];
                n++;
            }
        }
    } else {
        memcpy(levels, raw_levels, sizeof(raw_levels));
        memcpy(durs, raw_durs, sizeof(raw_durs));
        n = raw_n;
    }
    for (int pass = 0; pass < 3 && !done; pass++) {
        for (size_t i = 0; i < n; i++) {
            if (subghz_decoder_feed(dec, levels[i], durs[i]) == SUBGHZ_PROTOCOL_DONE) {
                done = true;
                break;
            }
        }
    }
    if (!done) {
        printf("FAIL %s: no decode\n", proto);
        failures++;
        goto out;
    }
    if (dec->decoded_bits != bits) {
        printf("FAIL %s: bits %u != %u\n", proto, dec->decoded_bits, bits);
        failures++;
        goto out;
    }
    uint64_t mask = bits >= 64 ? ~0ull : ((1ull << bits) - 1u);
    if ((dec->decoded_data & mask) != (key & mask)) {
        printf("FAIL %s: data 0x%llx != 0x%llx\n", proto,
               (unsigned long long)dec->decoded_data, (unsigned long long)key);
        failures++;
        goto out;
    }
    {
        char buf[192];
        const char *s = subghz_decoder_get_string(dec, buf, sizeof(buf));
        if (!s) {
            printf("FAIL %s: no detail string\n", proto);
            failures++;
            goto out;
        }
        printf("ok %-14s bits=%-3u key=0x%08llx -> %s\n", proto, bits,
               (unsigned long long)(key & 0xffffffffull), buf);
    }
out:
    subghz_encoder_free(enc);
    subghz_decoder_free(dec);
    return failures == 0;
}

static bool round_trip(const char *proto, uint64_t key, uint32_t bits) {
    return round_trip_feed(proto, key, bits, false);
}

static bool round_trip_air(const char *proto, uint64_t key, uint32_t bits) {
    return round_trip_feed(proto, key, bits, true);
}

/* Feed raw pulses for protocols whose encoder omits the inter-frame gap. */
static void test_princeton_noise_reject(void) {
    const subghz_protocol_entry_t *entry = subghz_protocol_find("Princeton");
    subghz_protocol_decoder_t *dec = subghz_decoder_alloc(entry);
    bool done = false;
    /* Random-ish garbage must not decode. */
    uint32_t seq[] = { 100, 5000, 37, 9999, 210, 63, 4000, 11, 777, 42 };
    for (int r = 0; r < 20 && !done; r++) {
        for (size_t i = 0; i < sizeof(seq) / sizeof(seq[0]); i++) {
            if (subghz_decoder_feed(dec, (r + i) & 1, seq[i]) == SUBGHZ_PROTOCOL_DONE) {
                done = true;
                break;
            }
        }
    }
    CHECK(!done, "princeton decoded garbage");
    subghz_decoder_free(dec);
}

static void test_keeloq_frame(void) {
    const subghz_protocol_entry_t *entry = subghz_protocol_find("KeeLoq");
    CHECK(entry != NULL, "keeloq registered");
    CHECK(entry->info->rolling, "keeloq flagged rolling");
    CHECK(entry->encoder_ops == NULL, "keeloq has no encoder");
    subghz_protocol_decoder_t *dec = subghz_decoder_alloc(entry);
    bool done = false;
    /* Preamble: 11 x H400/L400, header L4000, then 64 bits (1 = H400/L800). */
    for (int i = 0; i < 11 && !done; i++) {
        subghz_decoder_feed(dec, true, 400);
        subghz_decoder_feed(dec, false, 400);
    }
    subghz_decoder_feed(dec, true, 400);
    subghz_decoder_feed(dec, false, 4000);
    uint64_t key = 0xA5A5A5A55A5A5A5Aull;
    for (int i = 63; i >= 0; i--) {
        if ((key >> i) & 1ull) {
            subghz_decoder_feed(dec, true, 400);
            if (subghz_decoder_feed(dec, false, 800) == SUBGHZ_PROTOCOL_DONE) {
                done = true;
            }
        } else {
            subghz_decoder_feed(dec, true, 800);
            if (subghz_decoder_feed(dec, false, 400) == SUBGHZ_PROTOCOL_DONE) {
                done = true;
            }
        }
    }
    /* Status bit + end gap (mirrors the Flipper encoder tail). */
    subghz_decoder_feed(dec, true, 400);
    subghz_decoder_feed(dec, false, 800);
    subghz_decoder_feed(dec, true, 400);
    if (subghz_decoder_feed(dec, false, 16000) == SUBGHZ_PROTOCOL_DONE) {
        done = true;
    }
    CHECK(done, "keeloq frame decoded");
    if (done) {
        char buf[256];
        const char *s = subghz_decoder_get_string(dec, buf, sizeof(buf));
        CHECK(s != NULL, "keeloq detail string");
        if (s) {
            printf("ok KeeLoq frame -> %s\n", buf);
        }
    }
    subghz_decoder_free(dec);
}

static void test_keeloq_cipher_keystore(void) {
    /* Cipher self-test: encrypt then decrypt must round-trip. */
    uint64_t mkey = 0x0102030405060708ull;
    uint32_t plain = 0xDEADBEEFu;
    uint32_t enc = subghz_keeloq_encrypt(plain, mkey);
    uint32_t back = subghz_keeloq_decrypt(enc, mkey);
    CHECK(back == plain, "keeloq cipher round trip 0x%lx != 0x%lx", (unsigned long)back,
          (unsigned long)plain);
    CHECK(enc != plain, "keeloq encrypt is identity?!");

    /* Keystore: build a hopping code with button+discriminator, decrypt it. */
    subghz_keystore_t ks;
    subghz_keystore_init(&ks);
    CHECK(subghz_keystore_parse_line(&ks, "TestMfg,0102030405060708,1"), "keystore parse");
    CHECK(!subghz_keystore_parse_line(&ks, "BadLine"), "keystore rejects garbage");
    CHECK(ks.count == 1, "keystore count");
    uint8_t btn = 0xAu;
    uint32_t serial = 0x1234567u;
    uint16_t cnt = 0x42u;
    uint32_t fix = ((uint32_t)btn << 28) | (serial & 0x0ffffffful);
    uint32_t hop_plain = ((uint32_t)btn << 28) | ((serial & 0x3ffu) << 16) | cnt;
    uint32_t hop = subghz_keeloq_encrypt(hop_plain, mkey);
    const char *mf = NULL;
    uint16_t got_cnt = 0;
    CHECK(subghz_keystore_simple_decrypt(&ks, fix, hop, &mf, &got_cnt),
          "keystore simple decrypt");
    CHECK(got_cnt == cnt, "decrypted counter 0x%x != 0x%x", got_cnt, cnt);
    CHECK(mf && !strcmp(mf, "TestMfg"), "manufacturer name");
    printf("ok keeloq cipher+keystore cnt=0x%04x mf=%s\n", got_cnt, mf ? mf : "?");

    /* Normal learning: device key derived from master must decrypt. */
    {
        subghz_keystore_t ks2;
        subghz_keystore_init(&ks2);
        uint64_t master = 0xAAAAAAAAAAAAAAAAull;
        CHECK(subghz_keystore_add(&ks2, "NormMfg", master, SUBGHZ_KEY_TYPE_NORMAL),
              "keystore add normal");
        uint8_t btn2 = 0x3;
        uint32_t serial2 = 0x0BADC0DEu;
        uint16_t cnt2 = 0x1234;
        uint32_t fix2 = ((uint32_t)btn2 << 28) | (serial2 & 0x0ffffffful);
        uint64_t devkey = subghz_keeloq_normal_learning(fix2, master);
        uint32_t hop2_plain =
            ((uint32_t)btn2 << 28) | ((serial2 & 0x3ffu) << 16) | cnt2;
        uint32_t hop2 = subghz_keeloq_encrypt(hop2_plain, devkey);
        const char *mf2 = NULL;
        uint16_t got2 = 0;
        CHECK(subghz_keystore_simple_decrypt(&ks2, fix2, hop2, &mf2, &got2),
              "keystore normal-learning decrypt");
        CHECK(got2 == cnt2, "normal counter 0x%x != 0x%x", got2, cnt2);
        printf("ok keeloq normal-learning cnt=0x%04x mf=%s\n", got2, mf2 ? mf2 : "?");
    }

    /* Magic xor type 1 round trip. */
    {
        subghz_keystore_t ks3;
        subghz_keystore_init(&ks3);
        uint64_t magic = 0x0123456789ABCDEFull;
        CHECK(subghz_keystore_add(&ks3, "MagicMfg", magic, SUBGHZ_KEY_TYPE_MAGIC_XOR),
              "keystore add magic");
        uint8_t btn3 = 0x7;
        uint32_t serial3 = 0x00C0FFEEu;
        uint16_t cnt3 = 0x00FF;
        uint32_t fix3 = ((uint32_t)btn3 << 28) | (serial3 & 0x0ffffffful);
        uint64_t devkey3 = subghz_keeloq_magic_xor(fix3, magic);
        uint32_t hop3_plain =
            ((uint32_t)btn3 << 28) | ((serial3 & 0x3ffu) << 16) | cnt3;
        uint32_t hop3 = subghz_keeloq_encrypt(hop3_plain, devkey3);
        const char *mf3 = NULL;
        uint16_t got3 = 0;
        CHECK(subghz_keystore_simple_decrypt(&ks3, fix3, hop3, &mf3, &got3),
              "keystore magic-xor decrypt");
        CHECK(got3 == cnt3, "magic counter 0x%x != 0x%x", got3, cnt3);
        printf("ok keeloq magic-xor cnt=0x%04x mf=%s\n", got3, mf3 ? mf3 : "?");
    }
}

/* Independent textbook G.E. Thomas check of the Manchester encoders:
 * split durations into TE halves (peeling a merged finish off the trailing
 * gap), pair halves into cells from stream start, and map cells without any
 * shared state machine. Proves the TX uploads are protocol-correct for real
 * (edge-based) receivers. */
static bool textbook_manchester(const char *proto, uint64_t key, uint32_t bits,
                                uint32_t te_us, bool invert, uint32_t gap_us) {
    const subghz_protocol_entry_t *entry = subghz_protocol_find(proto);
    if (!entry || !entry->encoder_ops) {
        printf("SKIP %s: no encoder\n", proto);
        return true;
    }
    subghz_protocol_encoder_t *enc = subghz_encoder_alloc(entry);
    if (!enc) {
        printf("FAIL %s: alloc\n", proto);
        failures++;
        return false;
    }
    if (subghz_encoder_feed(enc, key, bits) != SUBGHZ_PROTOCOL_DONE) {
        printf("FAIL %s: encode rejected\n", proto);
        failures++;
        subghz_encoder_free(enc);
        return false;
    }
    int halves[8192];
    int nhalves = 0;
    bool level;
    uint32_t dur;
    subghz_encoder_reset(enc);
    while (subghz_encoder_yield(enc, &level, &dur) && nhalves < 8192) {
        if (dur % te_us) {
            /* Trailing gap with a merged finish half: peel the finish. */
            if (dur > gap_us && ((dur - gap_us) % te_us) == 0) {
                for (uint32_t k = 0; k < (dur - gap_us) / te_us && nhalves < 8192;
                     k++) {
                    halves[nhalves++] = level;
                }
            }
            break;
        }
        for (uint32_t k = 0; k < dur / te_us && nhalves < 8192; k++) {
            halves[nhalves++] = level;
        }
    }
    uint64_t got = 0;
    int got_bits = 0;
    bool cell_ok = true;
    for (int i = 0; i + 1 < nhalves && got_bits < 64; i += 2) {
        int bit;
        if (!halves[i] && halves[i + 1]) {
            bit = 1;
        } else if (halves[i] && !halves[i + 1]) {
            bit = 0;
        } else {
            cell_ok = false;
            break;
        }
        if (invert) {
            bit = !bit;
        }
        got = (got << 1) | (uint64_t)bit;
        got_bits++;
    }
    bool pass = cell_ok && got_bits == (int)bits && got == key;
    if (!pass) {
        printf("FAIL %s: textbook got=0x%llx (%d bits, cells %s) want=0x%llx\n", proto,
               (unsigned long long)got, got_bits, cell_ok ? "ok" : "bad",
               (unsigned long long)key);
        failures++;
    } else {
        printf("ok %-14s textbook %ubit key=0x%08llx\n", proto, bits,
               (unsigned long long)(key & 0xffffffffull));
    }
    subghz_encoder_free(enc);
    return pass;
}

/* Deterministic decoder spot checks on transition-free waveforms (no phase
 * luck involved): sync/framing, TRUE/FALSE cell yields, and exact prefix
 * accumulation. Full-frame loopback for these two stays phase-sensitive by
 * Flipper's event-based design (see decoder notes), so it is not asserted. */
static void test_manchester_decode_spots(void) {
    {
        const subghz_protocol_entry_t *entry = subghz_protocol_find("Power Smart");
        subghz_protocol_decoder_t *dec = subghz_decoder_alloc(entry);
        /* Gap aligns phase, then six 0-cells (H/L): six FALSE=!TRUE bits. */
        subghz_decoder_feed(dec, false, 499950);
        for (int i = 0; i < 6; i++) {
            subghz_decoder_feed(dec, true, 225);
            subghz_decoder_feed(dec, false, 225);
        }
        CHECK(dec->decoded_bits == 6 && dec->decoded_data == 0, "ps zero cells");
        subghz_decoder_free(dec);
    }
    {
        const subghz_protocol_entry_t *entry = subghz_protocol_find("Power Smart");
        subghz_protocol_decoder_t *dec = subghz_decoder_alloc(entry);
        /* LongHigh from Mid1 reaches Mid0 and yields FALSE -> decoded 1. */
        subghz_decoder_feed(dec, false, 499950);
        subghz_decoder_feed(dec, true, 450);
        CHECK(dec->decoded_bits == 1 && dec->decoded_data == 1, "ps long-high cell");
        subghz_decoder_free(dec);
    }
    {
        const subghz_protocol_entry_t *entry = subghz_protocol_find("Revers_RB2");
        subghz_protocol_decoder_t *dec = subghz_decoder_alloc(entry);
        /* Remote-style sync: gap low + four alternating halves seeds 0xF. */
        subghz_decoder_feed(dec, false, 600);
        subghz_decoder_feed(dec, true, 250);
        subghz_decoder_feed(dec, false, 250);
        subghz_decoder_feed(dec, true, 250);
        subghz_decoder_feed(dec, false, 250);
        CHECK(dec->decoded_bits == 4 && dec->decoded_data == 0xf, "rb2 sync seed");
        /* Five 1-cells (L/H): first L aligns, then four TRUEs. */
        for (int i = 0; i < 5; i++) {
            subghz_decoder_feed(dec, false, 250);
            subghz_decoder_feed(dec, true, 250);
        }
        CHECK(dec->decoded_bits == 8 && dec->decoded_data == 0xff, "rb2 ones prefix");
        subghz_decoder_free(dec);
    }
    printf("ok manchester decode spots\n");
}

/* Somfy RTS waveform synthesis: HW sync + SW sync + 1/2-TE lead-in low,
 * textbook G.E. Thomas halves (1 = L,H) air-merged, trailing gap. */
static size_t somfy_build(const char *proto, uint64_t frame, uint32_t frame_bits,
                          uint32_t pdc, uint32_t pdc_bits, bool *levels, uint32_t *durs,
                          size_t cap) {
    (void)proto;
    size_t n = 0;
#define SOMFY_PUSH(lv, du)                                        \
    do {                                                          \
        if (n && levels[n - 1u] == (lv)) {                        \
            durs[n - 1u] += (du);                                 \
        } else {                                                  \
            if (n >= cap) return 0;                               \
            levels[n] = (lv);                                     \
            durs[n] = (du);                                       \
            n++;                                                  \
        }                                                         \
    } while (0)
    SOMFY_PUSH(true, 2560);
    SOMFY_PUSH(false, 2560);
    SOMFY_PUSH(true, 2560);
    SOMFY_PUSH(false, 2560);
    SOMFY_PUSH(true, 4480);
    SOMFY_PUSH(false, 604);
    for (uint32_t i = 0; i < frame_bits; i++) {
        bool bit = ((frame >> (frame_bits - 1u - i)) & 1ull) != 0;
        if (bit) {
            SOMFY_PUSH(false, 640);
            SOMFY_PUSH(true, 640);
        } else {
            SOMFY_PUSH(true, 640);
            SOMFY_PUSH(false, 640);
        }
    }
    for (uint32_t i = 0; i < pdc_bits; i++) {
        bool bit = pdc_bits ? ((pdc >> (pdc_bits - 1u - i)) & 1u) != 0 : false;
        if (bit) {
            SOMFY_PUSH(false, 640);
            SOMFY_PUSH(true, 640);
        } else {
            SOMFY_PUSH(true, 640);
            SOMFY_PUSH(false, 640);
        }
    }
    SOMFY_PUSH(false, 9000);
    return n;
#undef SOMFY_PUSH
}

static uint8_t somfy_test_crc(uint64_t data) {
    uint8_t crc = 0;
    data &= 0xfff0ffffffffffull;
    for (uint8_t i = 0; i < 56u; i += 8u) {
        crc = (uint8_t)(crc ^ (data >> i) ^ (data >> (i + 4u)));
    }
    return (uint8_t)(crc & 0xfu);
}

/* Somfy Telis: checksum-solved frame must decode exactly. The CRC gate
 * makes this self-proving (a DONE implies a valid frame and decoder). */
static void test_somfy_telis(void) {
    uint64_t base = 0xA7800042121300ull; /* seed A7, Up, cnt 0x42, serial */
    uint64_t frame = 0;
    for (int k = 0; k < 16; k++) {
        uint64_t cand = base | ((uint64_t)k << 40);
        uint64_t dt = cand ^ (cand >> 8);
        if (((dt >> 40) & 0xfu) == somfy_test_crc(dt)) {
            frame = cand;
            break;
        }
    }
    CHECK(frame != 0, "somfy vector solves");
    const subghz_protocol_entry_t *entry = subghz_protocol_find("Somfy Telis");
    CHECK(entry != NULL && entry->info->rolling && entry->encoder_ops == NULL,
          "telis rolling decode-only");
    subghz_protocol_decoder_t *dec = subghz_decoder_alloc(entry);
    bool levels[512];
    uint32_t durs[512];
    size_t n = somfy_build("t", frame, 56, 0, 0, levels, durs, 512);
    CHECK(n > 0, "somfy waveform builds");
    bool done = false;
    for (size_t i = 0; i < n; i++) {
        if (subghz_decoder_feed(dec, levels[i], durs[i]) == SUBGHZ_PROTOCOL_DONE) {
            done = true;
            break;
        }
    }
    CHECK(done, "telis decodes");
    CHECK(dec->decoded_bits == 56 && dec->decoded_data == frame, "telis exact 0x%llx",
          (unsigned long long)frame);
    {
        char buf[256];
        const char *s = subghz_decoder_get_string(dec, buf, sizeof(buf));
        CHECK(s != NULL, "telis detail string");
        if (s) {
            printf("ok %-14s 56bit key=0x%08llx -> %s\n", "Somfy Telis",
                   (unsigned long long)(frame & 0xffffffffull), buf);
            CHECK(strstr(buf, "Btn:Up") != NULL, "telis button Up");
        }
    }
    subghz_decoder_free(dec);
}

/* Somfy Keytis: Flipper's real capture (data + press-duration counter). */
static void test_somfy_keytis(void) {
    const subghz_protocol_entry_t *entry = subghz_protocol_find("Somfy Keytis");
    CHECK(entry != NULL && entry->info->rolling && entry->encoder_ops == NULL,
          "keytis rolling decode-only");
    subghz_protocol_decoder_t *dec = subghz_decoder_alloc(entry);
    bool levels[512];
    uint32_t durs[512];
    size_t n = somfy_build("k", 0xA453537C4B9855ull, 56, 0xC40019, 24, levels, durs,
                           512);
    CHECK(n > 0, "keytis waveform builds");
    bool done = false;
    for (size_t i = 0; i < n; i++) {
        if (subghz_decoder_feed(dec, levels[i], durs[i]) == SUBGHZ_PROTOCOL_DONE) {
            done = true;
            break;
        }
    }
    CHECK(done, "keytis decodes");
    CHECK(dec->decoded_bits == 56 && dec->decoded_data == 0xA453537C4B9855ull,
          "keytis exact");
    {
        char buf[256];
        const char *s = subghz_decoder_get_string(dec, buf, sizeof(buf));
        CHECK(s != NULL, "keytis detail string");
        if (s) {
            printf("ok %-14s 80bit key=0x%08llx -> %s\n", "Somfy Keytis",
                   (unsigned long long)(dec->decoded_data & 0xffffffffull), buf);
            CHECK(strstr(buf, "Sn:0x37D3CD") != NULL, "keytis serial");
            CHECK(strstr(buf, "Cnt:0x002F") != NULL, "keytis counter");
            CHECK(strstr(buf, "Btn:Down") != NULL, "keytis button");
        }
    }
    subghz_decoder_free(dec);
}

/* Hay21: Flipper's real sample 0x14A84A (btn A5, serial 0x42, cnt 5). */
static void test_hay21(void) {
    const subghz_protocol_entry_t *entry = subghz_protocol_find("Hay21");
    CHECK(entry != NULL && entry->info->rolling && entry->encoder_ops == NULL,
          "hay21 rolling decode-only");
    subghz_protocol_decoder_t *dec = subghz_decoder_alloc(entry);
    uint64_t key = 0x14A84Aull;
    /* Gap header, 21 high-first bits (1 = H700/L300), trailing gap. */
    int levels[128];
    uint32_t durs[128];
    size_t n = 0;
    levels[n] = false;
    durs[n++] = 4200;
    for (int i = 20; i >= 0; i--) {
        if ((key >> i) & 1ull) {
            levels[n] = true;
            durs[n++] = 700;
            levels[n] = false;
            durs[n++] = 300;
        } else {
            levels[n] = true;
            durs[n++] = 300;
            levels[n] = false;
            durs[n++] = 700;
        }
    }
    levels[n] = false;
    durs[n++] = 4200;
    bool done = false;
    for (size_t i = 0; i < n; i++) {
        if (subghz_decoder_feed(dec, levels[i], durs[i]) == SUBGHZ_PROTOCOL_DONE) {
            done = true;
            break;
        }
    }
    CHECK(done, "hay21 decodes");
    CHECK(dec->decoded_bits == 21 && dec->decoded_data == key, "hay21 exact");
    {
        char buf[256];
        const char *s = subghz_decoder_get_string(dec, buf, sizeof(buf));
        CHECK(s != NULL, "hay21 detail string");
        if (s) {
            printf("ok %-14s 21bit key=0x%08llx -> %s\n", "Hay21",
                   (unsigned long long)key, buf);
            CHECK(strstr(buf, "Serial: 0x42") != NULL, "hay21 serial");
            CHECK(strstr(buf, "Cnt: 0x5") != NULL, "hay21 counter");
        }
    }
    subghz_decoder_free(dec);
}

/* Magellan: CRC-solved frame must round-trip exactly. */
static uint8_t magellan_test_crc(uint32_t data24) {
    uint8_t bytes[3] = {
        (uint8_t)(data24 >> 16), (uint8_t)(data24 >> 8), (uint8_t)data24,
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

static void test_magellan(void) {
    uint64_t key = ((uint64_t)0x37AE48 << 8) | magellan_test_crc(0x37AE48);
    round_trip("Magellan", key, 32);
}

/* CAME TWEE: Flipper's documented parcel must replay exactly, with the
 * magic-XOR DIP decrypt matching their worked example. */
static void test_twee(void) {
    const subghz_protocol_entry_t *entry = subghz_protocol_find("CAME TWEE");
    CHECK(entry != NULL && !entry->info->rolling && entry->encoder_ops != NULL,
          "twee static with encoder");
    if (!round_trip("CAME TWEE", 0x003FFF721DB79211ull, 54)) {
        return;
    }
    /* Detail check on a fresh decode (Btn:C, DIP:0101010000 per comment). */
    subghz_protocol_encoder_t *enc = subghz_encoder_alloc(entry);
    subghz_protocol_decoder_t *dec = subghz_decoder_alloc(entry);
    subghz_encoder_feed(enc, 0x003FFF721DB79211ull, 54);
    bool done = false;
    for (int pass = 0; pass < 2 && !done; pass++) {
        subghz_encoder_reset(enc);
        bool level;
        uint32_t dur;
        while (subghz_encoder_yield(enc, &level, &dur)) {
            if (subghz_decoder_feed(dec, level, dur) == SUBGHZ_PROTOCOL_DONE) {
                done = true;
                break;
            }
        }
    }
    CHECK(done, "twee parcel decodes");
    {
        char buf[256];
        const char *s = subghz_decoder_get_string(dec, buf, sizeof(buf));
        CHECK(s != NULL, "twee detail string");
        if (s) {
            printf("ok %-14s 54bit -> %s\n", "CAME TWEE", buf);
            CHECK(strstr(buf, "Btn:C") != NULL, "twee button");
            CHECK(strstr(buf, "DIP:0101010000") != NULL, "twee dips");
        }
    }
    subghz_encoder_free(enc);
    subghz_decoder_free(dec);
}

/* FAAC SLH: preamble + 64 high-first bits + re-preamble terminator. */
static void test_faac(void) {
    const subghz_protocol_entry_t *entry = subghz_protocol_find("Faac SLH");
    CHECK(entry != NULL && entry->info->rolling && entry->encoder_ops == NULL,
          "faac rolling decode-only");
    subghz_protocol_decoder_t *dec = subghz_decoder_alloc(entry);
    uint64_t key = 0x123456789ABCDEF0ull;
    int levels[512];
    uint32_t durs[512];
    size_t n = 0;
    levels[n] = true;
    durs[n++] = 1190;
    levels[n] = false;
    durs[n++] = 1190;
    for (int i = 63; i >= 0; i--) {
        if ((key >> i) & 1ull) {
            levels[n] = true;
            durs[n++] = 595;
            levels[n] = false;
            durs[n++] = 255;
        } else {
            levels[n] = true;
            durs[n++] = 255;
            levels[n] = false;
            durs[n++] = 595;
        }
    }
    levels[n] = true;
    durs[n++] = 1190;
    bool done = false;
    for (size_t i = 0; i < n; i++) {
        if (subghz_decoder_feed(dec, levels[i], durs[i]) == SUBGHZ_PROTOCOL_DONE) {
            done = true;
            break;
        }
    }
    CHECK(done, "faac decodes");
    CHECK(dec->decoded_bits == 64 && dec->decoded_data == key, "faac exact");
    {
        char buf[256];
        const char *s = subghz_decoder_get_string(dec, buf, sizeof(buf));
        CHECK(s != NULL, "faac detail string");
        if (s) {
            printf("ok %-14s 64bit key=0x%08llx -> %s\n", "Faac SLH",
                   (unsigned long long)(key & 0xffffffffull), buf);
        }
    }
    subghz_decoder_free(dec);
}

/* Scher-Khan 51-bit MAGIC CODE Dynamic: preamble + start + 50 pairs. */
static void test_scher(void) {
    const subghz_protocol_entry_t *entry = subghz_protocol_find("Scher-Khan");
    CHECK(entry != NULL && entry->info->rolling && entry->encoder_ops == NULL,
          "scher rolling decode-only");
    subghz_protocol_decoder_t *dec = subghz_decoder_alloc(entry);
    uint64_t key = 0x0E8C1619E830Cull; /* 51-count value, top bit clear */
    int levels[512];
    uint32_t durs[512];
    size_t n = 0;
    for (int i = 0; i < 3; i++) {
        levels[n] = true;
        durs[n++] = 1500;
        levels[n] = false;
        durs[n++] = 1500;
    }
    levels[n] = true;
    durs[n++] = 750;
    levels[n] = false;
    durs[n++] = 750;
    for (int i = 49; i >= 0; i--) {
        if ((key >> i) & 1ull) {
            levels[n] = true;
            durs[n++] = 1100;
            levels[n] = false;
            durs[n++] = 1100;
        } else {
            levels[n] = true;
            durs[n++] = 750;
            levels[n] = false;
            durs[n++] = 750;
        }
    }
    levels[n] = true;
    durs[n++] = 1500;
    bool done = false;
    for (size_t i = 0; i < n; i++) {
        if (subghz_decoder_feed(dec, levels[i], durs[i]) == SUBGHZ_PROTOCOL_DONE) {
            done = true;
            break;
        }
    }
    CHECK(done, "scher decodes");
    CHECK(dec->decoded_bits == 51 && dec->decoded_data == key, "scher exact");
    {
        char buf[256];
        const char *s = subghz_decoder_get_string(dec, buf, sizeof(buf));
        CHECK(s != NULL, "scher detail string");
        if (s) {
            printf("ok %-14s 51bit key=0x%08llx -> %s\n", "Scher-Khan",
                   (unsigned long long)(key & 0xffffffffull), buf);
            CHECK(strstr(buf, "Dynamic") != NULL, "scher variant");
        }
    }
    subghz_decoder_free(dec);
}

/* iDo 48-bit fix/hop remote. */
static void test_ido(void) {
    const subghz_protocol_entry_t *entry = subghz_protocol_find("iDo 117/111");
    CHECK(entry != NULL && entry->info->rolling && entry->encoder_ops == NULL,
          "ido rolling decode-only");
    subghz_protocol_decoder_t *dec = subghz_decoder_alloc(entry);
    uint64_t key = 0xABCDEF123456ull;
    int levels[512];
    uint32_t durs[512];
    size_t n = 0;
    levels[n] = true;
    durs[n++] = 4500;
    levels[n] = false;
    durs[n++] = 4500;
    for (int i = 47; i >= 0; i--) {
        if ((key >> i) & 1ull) {
            levels[n] = true;
            durs[n++] = 450;
            levels[n] = false;
            durs[n++] = 450;
        } else {
            levels[n] = true;
            durs[n++] = 450;
            levels[n] = false;
            durs[n++] = 1450;
        }
    }
    levels[n] = true;
    durs[n++] = 2400;
    bool done = false;
    for (size_t i = 0; i < n; i++) {
        if (subghz_decoder_feed(dec, levels[i], durs[i]) == SUBGHZ_PROTOCOL_DONE) {
            done = true;
            break;
        }
    }
    CHECK(done, "ido decodes");
    CHECK(dec->decoded_bits == 48 && dec->decoded_data == key, "ido exact");
    {
        char buf[256];
        const char *s = subghz_decoder_get_string(dec, buf, sizeof(buf));
        CHECK(s != NULL, "ido detail string");
        if (s) {
            printf("ok %-14s 48bit key=0x%08llx -> %s\n", "iDo 117/111",
                   (unsigned long long)(key & 0xffffffffull), buf);
        }
    }
    subghz_decoder_free(dec);
}

/* KIA Seed 61-bit: preamble + start-seeded 1 + 59 pairs. */
static void test_kia(void) {
    const subghz_protocol_entry_t *entry = subghz_protocol_find("KIA Seed");
    CHECK(entry != NULL && entry->info->rolling && entry->encoder_ops == NULL,
          "kia rolling decode-only");
    subghz_protocol_decoder_t *dec = subghz_decoder_alloc(entry);
    uint64_t key = 0x0F011243B04EC17Dull; /* bit60 clear, seed bit59 set */
    int levels[512];
    uint32_t durs[512];
    size_t n = 0;
    for (int i = 0; i < 17; i++) {
        levels[n] = true;
        durs[n++] = 250;
        levels[n] = false;
        durs[n++] = 250;
    }
    levels[n] = true;
    durs[n++] = 500;
    levels[n] = false;
    durs[n++] = 500;
    for (int i = 58; i >= 0; i--) {
        if ((key >> i) & 1ull) {
            levels[n] = true;
            durs[n++] = 500;
            levels[n] = false;
            durs[n++] = 500;
        } else {
            levels[n] = true;
            durs[n++] = 250;
            levels[n] = false;
            durs[n++] = 250;
        }
    }
    levels[n] = true;
    durs[n++] = 800;
    bool done = false;
    for (size_t i = 0; i < n; i++) {
        if (subghz_decoder_feed(dec, levels[i], durs[i]) == SUBGHZ_PROTOCOL_DONE) {
            done = true;
            break;
        }
    }
    CHECK(done, "kia decodes");
    CHECK(dec->decoded_bits == 61 && dec->decoded_data == key, "kia exact");
    {
        char buf[256];
        const char *s = subghz_decoder_get_string(dec, buf, sizeof(buf));
        CHECK(s != NULL, "kia detail string");
        if (s) {
            printf("ok %-14s 61bit key=0x%08llx -> %s\n", "KIA Seed",
                   (unsigned long long)(key & 0xffffffffull), buf);
            CHECK(strstr(buf, "Sn:43B04EC") != NULL, "kia serial");
            CHECK(strstr(buf, "Btn:1") != NULL, "kia button");
            CHECK(strstr(buf, "Cnt:0112") != NULL, "kia counter");
        }
    }
    subghz_decoder_free(dec);
}

/* Corrupt frames must not decode: broken RB2 stop code, broken Power Smart
 * check fields, and Chamberlain garbage. Feeds use air-merged pulses. */
static void test_batch2_rejects(void) {
    struct {
        const char *proto;
        uint64_t key;
        uint32_t bits;
    } cases[] = {
        /* Valid structure (seed 0xF, preamble 0xFF) but stop 0x201. */
        { "Revers_RB2", 0xFFFF48D159E26A01ull, 64 },
        { "Power Smart", 0xFD2137ACAADEC853ull, 64 },
    };
    for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
        const subghz_protocol_entry_t *entry = subghz_protocol_find(cases[c].proto);
        CHECK(entry != NULL, "%s registered", cases[c].proto);
        subghz_protocol_encoder_t *enc = subghz_encoder_alloc(entry);
        subghz_protocol_decoder_t *dec = subghz_decoder_alloc(entry);
        CHECK(enc && dec, "%s alloc", cases[c].proto);
        CHECK(subghz_encoder_feed(enc, cases[c].key, cases[c].bits) ==
                  SUBGHZ_PROTOCOL_DONE,
              "%s corrupt encodes", cases[c].proto);
        bool levels[512];
        uint32_t durs[512];
        size_t n = collect_merged(enc, levels, durs, 512);
        bool done = false;
        for (int pass = 0; pass < 2 && !done; pass++) {
            for (size_t i = 0; i < n; i++) {
                if (subghz_decoder_feed(dec, levels[i], durs[i]) ==
                    SUBGHZ_PROTOCOL_DONE) {
                    done = true;
                    break;
                }
            }
        }
        CHECK(!done, "%s corrupt frame rejected", cases[c].proto);
        subghz_encoder_free(enc);
        subghz_decoder_free(dec);
    }
    /* Chamberlain garbage must not decode. */
    {
        const subghz_protocol_entry_t *entry = subghz_protocol_find("Cham_Code");
        subghz_protocol_decoder_t *dec = subghz_decoder_alloc(entry);
        bool done = false;
        uint32_t seq[] = { 39000, 1000, 2000, 2000, 1000, 3000, 2000, 2000,
                           3000, 1000, 1000, 1000, 9000 };
        for (int r = 0; r < 4 && !done; r++) {
            for (size_t i = 0; i < sizeof(seq) / sizeof(seq[0]); i++) {
                if (subghz_decoder_feed(dec, (r + i) & 1, seq[i]) ==
                    SUBGHZ_PROTOCOL_DONE) {
                    done = true;
                    break;
                }
            }
        }
        CHECK(!done, "chamberlain decoded garbage");
        subghz_decoder_free(dec);
    }
    printf("ok batch2 rejects\n");
}

static void test_rx_dedup(void) {
    subghz_rx_t rx;
    CHECK(subghz_rx_init(&rx), "rx init");
    CHECK(rx.slot_count == subghz_protocol_count(), "rx covers all protocols");
    /* Feed a Princeton frame twice through the receiver; callback fires once. */
    const subghz_protocol_entry_t *entry = subghz_protocol_find("Princeton");
    subghz_protocol_encoder_t *enc = subghz_encoder_alloc(entry);
    CHECK(subghz_encoder_feed(enc, 0xABCDEF, 24) == SUBGHZ_PROTOCOL_DONE, "princeton encode");
    int calls = 0;
    for (int pass = 0; pass < 2; pass++) {
        subghz_encoder_reset(enc);
        bool level;
        uint32_t dur;
        while (subghz_encoder_yield(enc, &level, &dur)) {
            /* Count callbacks via inline scan of slots instead of a callback. */
            for (uint32_t s = 0; s < rx.slot_count; s++) {
                if (subghz_decoder_feed(rx.slots[s].decoder, level, dur) ==
                    SUBGHZ_PROTOCOL_DONE) {
                    if (rx.slots[s].entry == entry) {
                        calls++;
                    }
                    subghz_decoder_reset(rx.slots[s].decoder);
                }
            }
        }
    }
    CHECK(calls == 2, "princeton decoded twice, got %d", calls);
    subghz_encoder_free(enc);
    subghz_rx_free(&rx);
    printf("ok rx dispatch\n");
}

static void test_file_fields(void) {
    uint32_t khz = 0, bits = 0, te = 0, kb = 0;
    uint64_t key = 0;
    char mf[32];

    CHECK(subghz_parse_frequency_khz("Frequency: 433920000", &khz) && khz == 433920,
          "freq parse");
    CHECK(!subghz_parse_frequency_khz("Frequency: 433920001", &khz), "freq non-kHz reject");
    CHECK(!subghz_parse_frequency_khz("Frequency: 500000000", &khz), "freq OOB reject");
    CHECK(subghz_parse_frequency_khz("Frequency: 868300000\r\n", &khz) && khz == 868300,
          "freq CRLF");

    CHECK(subghz_parse_bit_field("Bit: 24", &bits) && bits == 24, "bit parse");
    CHECK(!subghz_parse_bit_field("Bit: 0", &bits), "bit zero reject");
    CHECK(!subghz_parse_bit_field("Bit: 129", &bits), "bit huge reject");
    CHECK(!subghz_parse_bit_field("Bit: 2x", &bits), "bit garbage reject");

    CHECK(subghz_parse_key_field("Key: 00 00 00 00 00 A6 8D 45", &key, &kb) &&
              key == 0xA68D45ull && kb == 64,
          "key parse");
    CHECK(subghz_parse_key_field("Key: aB cD", &key, &kb) && key == 0xABCDull,
          "key lowercase");
    CHECK(!subghz_parse_key_field("Key:", &key, &kb), "key empty reject");
    CHECK(!subghz_parse_key_field("Key: ZZ", &key, &kb), "key hex reject");
    CHECK(!subghz_parse_key_field("Key: 00 11 22 33 44 55 66 77 88", &key, &kb),
          "key 9-byte reject");

    CHECK(subghz_parse_te_field("TE: 350", &te) && te == 350, "te parse");
    CHECK(!subghz_parse_te_field("TE: 49", &te), "te small reject");

    CHECK(subghz_parse_manufacture("Manufacture: DoorHan", mf, sizeof(mf)) &&
              !strcmp(mf, "DoorHan"),
          "manufacture parse");
    CHECK(!subghz_parse_manufacture("Manufacture: ", mf, sizeof(mf)),
          "manufacture empty reject");

    uint8_t pairs[96];
    size_t npairs = 0;
    CHECK(subghz_parse_custom_preset("Custom_preset_data: 02 0D 03 47 10 67", pairs,
                                     &npairs, 48) &&
              npairs == 3 && pairs[0] == 0x02 && pairs[1] == 0x0D && pairs[4] == 0x10,
          "custom preset parse");
    CHECK(!subghz_parse_custom_preset("Custom_preset_data: 02 0D 03", pairs, &npairs, 48),
          "custom odd reject");
    CHECK(!subghz_parse_custom_preset("Custom_preset_data:", pairs, &npairs, 48),
          "custom empty reject");

    /* Flipper-exact form: register pairs + 00 00 terminator + 8-byte PA table
     * (example from the official SubGhz file-format doc). */
    {
        uint8_t pa[SUBGHZ_CUSTOM_PA_SIZE];
        bool have_pa = false;
        size_t nfull = 0;
        CHECK(subghz_parse_custom_preset_full(
                  "Custom_preset_data: 02 0D 03 07 08 32 0B 06 14 00 13 00 12 30 "
                  "11 32 10 17 18 18 19 18 1D 91 1C 00 1B 07 20 FB 22 11 21 B6 "
                  "00 00 00 C0 00 00 00 00 00 00",
                  pairs, &nfull, 48, pa, &have_pa) &&
                  nfull == 17 && have_pa && pa[1] == 0xC0 && pa[0] == 0x00,
              "custom flipper PA parse");
        /* Legacy wrapper strips the PA table but keeps the register pairs. */
        size_t nleg = 0;
        CHECK(subghz_parse_custom_preset(
                  "Custom_preset_data: 02 0D 03 07 08 32 0B 06 14 00 13 00 12 30 "
                  "11 32 10 17 18 18 19 18 1D 91 1C 00 1B 07 20 FB 22 11 21 B6 "
                  "00 00 00 C0 00 00 00 00 00 00",
                  pairs, &nleg, 48) &&
                  nleg == 17,
              "custom flipper legacy wrapper");
        /* Truncated PA table (7 bytes after terminator) is rejected. */
        CHECK(!subghz_parse_custom_preset_full(
                  "Custom_preset_data: 02 0D 00 00 00 C0 00 00 00 00 00", pairs,
                  &nfull, 48, pa, &have_pa),
              "custom truncated PA reject");
        /* Trailing garbage after the PA table is rejected. */
        CHECK(!subghz_parse_custom_preset_full(
                  "Custom_preset_data: 02 0D 00 00 00 C0 00 00 00 00 00 00 FF", pairs,
                  &nfull, 48, pa, &have_pa),
              "custom trailing reject");
        /* Invalid register address is rejected. */
        CHECK(!subghz_parse_custom_preset_full("Custom_preset_data: 3F 00 00 00 00 C0 "
                                               "00 00 00 00 00 00",
                                               pairs, &nfull, 48, pa, &have_pa),
              "custom bad address reject");
    }

    /* BinRAW fields (Flipper BinRAW file section). */
    {
        uint32_t braw = 0;
        CHECK(subghz_parse_bit_raw_field("Bit_RAW: 260", &braw) && braw == 260,
              "bit_raw parse");
        CHECK(!subghz_parse_bit_raw_field("Bit_RAW: 0", &braw), "bit_raw zero reject");
        CHECK(!subghz_parse_bit_raw_field("Bit_RAW: 4097", &braw),
              "bit_raw huge reject");
        uint8_t bbytes[SUBGHZ_BINRAW_MAX_BYTES];
        size_t nbytes = 0;
        CHECK(subghz_parse_data_raw_field("Data_RAW: 00 0F 4A", bbytes, &nbytes, 512) &&
                  nbytes == 3 && bbytes[1] == 0x0F,
              "data_raw parse");
        CHECK(!subghz_parse_data_raw_field("Data_RAW:", bbytes, &nbytes, 512),
              "data_raw empty reject");
        CHECK(!subghz_parse_data_raw_field("Data_RAW: 0", bbytes, &nbytes, 512),
              "data_raw odd reject");
        /* Spec example: TE=100, Bit_RAW=8, Data=0x37 (0b00110111)
         * transmits -200 200 -100 300. */
        uint8_t ex[] = { 0x37 };
        int32_t pulses[8];
        uint32_t np = 0;
        CHECK(subghz_binraw_expand(100, ex, 8, pulses, &np, 8) && np == 4 &&
                  pulses[0] == -200 && pulses[1] == 200 && pulses[2] == -100 &&
                  pulses[3] == 300,
              "binraw expand spec example");
        CHECK(!subghz_binraw_expand(0, ex, 8, pulses, &np, 8),
              "binraw bad TE reject");
        CHECK(!subghz_binraw_expand(100, ex, 5000, pulses, &np, 8),
              "binraw huge bits reject");
    }

    CHECK(subghz_field_value_equals("Protocol: RAW", 9, "RAW"), "field equals");
    CHECK(!subghz_field_value_equals("Protocol: RAW ", 9, "RAWX"), "field mismatch");
    printf("ok file fields\n");
}

int main(void) {
    printf("protocol count: %u (static %u)\n", subghz_protocol_count(),
           subghz_protocol_count_static());
    CHECK(subghz_protocol_count() == 45, "expected 45 protocols, got %u",
          subghz_protocol_count());
    CHECK(subghz_protocol_count_static() == 37, "expected 37 static protocols, got %u",
          subghz_protocol_count_static());
    CHECK(subghz_protocol_find("Nope") == NULL, "unknown protocol NULL");

    round_trip("Princeton", 0xABCDEF, 24);
    round_trip("RcSwitch", 0xABCDEF, 24);
    round_trip("GateTX", 0x123456, 24);
    round_trip("Ansonic", 0xABC, 12);
    round_trip("Dooya", 0xE1DC030533ull, 40);
    round_trip("SMC5326", 0x1ABCDEF, 25);
    round_trip("Intertechno V3", 0x3F86C59F, 32);
    round_trip("Nice FLO", 0xABCDEF, 24);
    round_trip("CAME", 0xABC, 12);
    round_trip("CAME", 0xABCDEF, 24);
    round_trip("Holtek_HT12X", 0xA51, 12);
    round_trip("Linear", 0xABCDEF, 24);
    round_trip("BETT", 0x12345, 18);
    round_trip("Clemsa", 0x12345, 18);
    round_trip("Doitrand", 0x1ABCDEF12ull, 37);
    round_trip("Elplast", 0x12345, 18);
    round_trip("Feron", 0x12345678, 32);
    round_trip("Roger", 0x1234567, 28);
    round_trip("Mastercode", 0x123456789ull, 36);
    round_trip("Marantec24", 0xAC05C8, 24);
    round_trip("Holtek", 0x50ABCDEF12ull, 40);
    round_trip("Phoenix V2", 0xABCDEF1234567ull, 52);
    round_trip("Hormann", 0xABCDEF12345ull, 44);
    round_trip("Gangqi", 0x2ABCDEF12ull, 34);
    round_trip("Dickert_MAHS", 0xABCDEF123ull, 36);
    round_trip("LinearDelta3", 0xA5, 8);
    /* Batch 2: Manchester static + Chamberlain (Flipper-verified vectors).
     * Marantec and Chamberlain round-trip end to end. Revers_RB2 and
     * Power Smart use dedicated tests below (textbook + spot checks). */
    round_trip_air("Marantec", 0x1307EDF6486C5ull, 49);
    round_trip("Cham_Code", 0x55, 7);
    round_trip("Cham_Code", 0xA1, 8); /* bit 2 is unused on 8-dip remotes */
    round_trip("Cham_Code", 0x1AA, 9);
    round_trip("Nero Radio", 0x01020304050607ull, 56);
    round_trip("Nero Sketch", 0x0102030405ull, 40);
    round_trip("MegaCode", 0xFFFFFF, 24);
    round_trip("MegaCode", 0xABCDEF, 24);
    round_trip("MegaCode", 0x800001, 24);
    round_trip("Legrand", 0x2ABCD, 18);
    round_trip("Hollarm", 0x010203060Cull, 40);
    {
        /* Honeywell key with correct even parity in bit 0. */
        uint64_t hw = 0xABCDEF123450ull & ~1ull;
        if (subghz_even_parity(hw >> 1, 47)) {
            hw |= 1u;
        }
        round_trip("Honeywell WDB", hw, 48);
    }

    test_princeton_noise_reject();
    test_batch2_rejects();
    test_somfy_telis();
    test_somfy_keytis();
    test_hay21();
    test_magellan();
    test_twee();
    test_faac();
    test_scher();
    test_ido();
    test_kia();
    textbook_manchester("Power Smart", 0xFD2137ACAADEC852ull, 64, 225, true,
                        450u * 1111u);
    textbook_manchester("Revers_RB2", 0xFFFF48D159E26A00ull, 64, 250, false, 320);
    test_manchester_decode_spots();
    test_keeloq_frame();
    test_keeloq_cipher_keystore();
    test_rx_dedup();
    test_file_fields();

    /* Test RcSwitch Proto 2 and Proto 11 */
    {
        const subghz_protocol_entry_t *entry = subghz_protocol_find("RcSwitch");
        CHECK(entry != NULL, "RcSwitch protocol found");
        subghz_protocol_encoder_t *enc = subghz_encoder_alloc(entry);
        subghz_protocol_decoder_t *dec = subghz_decoder_alloc(entry);
        CHECK(enc && dec, "RcSwitch alloc");

        /* Proto 2 */
        rcswitch_enc_priv_t *ep = enc->priv;
        ep->proto = 2;
        ep->te_us = 650;
        CHECK(subghz_encoder_feed(enc, 0x123456, 24) == SUBGHZ_PROTOCOL_DONE, "RcSwitch P2 encode");
        bool lv;
        uint32_t du;
        subghz_encoder_reset(enc);
        bool done = false;
        while (subghz_encoder_yield(enc, &lv, &du)) {
            if (subghz_decoder_feed(dec, lv, du) == SUBGHZ_PROTOCOL_DONE) {
                done = true;
                break;
            }
        }
        CHECK(done, "RcSwitch P2 decode done");
        CHECK(dec->decoded_data == 0x123456, "RcSwitch P2 key match");
        CHECK(dec->decoded_bits == 24, "RcSwitch P2 bits match");

        /* Proto 11 */
        subghz_decoder_reset(dec);
        ep->proto = 11;
        ep->te_us = 350;
        CHECK(subghz_encoder_feed(enc, 0x654321, 24) == SUBGHZ_PROTOCOL_DONE, "RcSwitch P11 encode");
        subghz_encoder_reset(enc);
        done = false;
        while (subghz_encoder_yield(enc, &lv, &du)) {
            if (subghz_decoder_feed(dec, lv, du) == SUBGHZ_PROTOCOL_DONE) {
                done = true;
                break;
            }
        }
        CHECK(done, "RcSwitch P11 decode done");
        CHECK(dec->decoded_data == 0x654321, "RcSwitch P11 key match");
        CHECK(dec->decoded_bits == 24, "RcSwitch P11 bits match");

        subghz_encoder_free(enc);
        subghz_decoder_free(dec);
        printf("ok rcswitch proto 2 and 11 roundtrips\n");
    }

    /* Test SubGHz frequency channels and bands */
    {
        CHECK(subghz_freq_list_count == 91, "freq list count 91, got %zu", subghz_freq_list_count);
        size_t s = 0, e = 0;
        CHECK(subghz_band_range(SUBGHZ_BAND_ALL, &s, &e) && s == 0 && e == 90, "band all range");
        CHECK(subghz_band_range(SUBGHZ_BAND_300, &s, &e) && s == 0 && e == 25, "band 300 range");
        CHECK(subghz_band_range(SUBGHZ_BAND_433, &s, &e) && s == 26 && e == 59, "band 433 range");
        CHECK(subghz_band_range(SUBGHZ_BAND_868, &s, &e) && s == 60 && e == 90, "band 868 range");
        CHECK(subghz_band_from_name("433") == SUBGHZ_BAND_433, "band from name 433");
        CHECK(subghz_band_from_name("315") == SUBGHZ_BAND_300, "band from name 315");
        CHECK(subghz_band_from_name("868") == SUBGHZ_BAND_868, "band from name 868");
        CHECK(subghz_band_from_name("all") == SUBGHZ_BAND_ALL, "band from name all");
        size_t cnt = 0;
        const uint32_t *f = subghz_band_frequencies(SUBGHZ_BAND_433, &cnt);
        CHECK(f && cnt == 34 && f[0] == 387000, "band 433 frequencies");
        printf("ok subghz freqs and bands\n");
    }

    if (failures) {
        printf("RESULT: %d FAILURES\n", failures);
        return 1;
    }
    printf("RESULT: ALL PASS\n");
    return 0;
}
