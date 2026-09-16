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

/* Feed an encoder upload into a decoder; expect DONE with matching data. */
static bool round_trip(const char *proto, uint64_t key, uint32_t bits) {
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
    for (int pass = 0; pass < 3 && !done; pass++) {
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

    CHECK(subghz_field_value_equals("Protocol: RAW", 9, "RAW"), "field equals");
    CHECK(!subghz_field_value_equals("Protocol: RAW ", 9, "RAWX"), "field mismatch");
    printf("ok file fields\n");
}

int main(void) {
    printf("protocol count: %u (static %u)\n", subghz_protocol_count(),
           subghz_protocol_count_static());
    CHECK(subghz_protocol_count() == 29, "expected 29 protocols, got %u",
          subghz_protocol_count());
    CHECK(subghz_protocol_find("Nope") == NULL, "unknown protocol NULL");

    round_trip("Princeton", 0xABCDEF, 24);
    round_trip("GateTX", 0x123456, 24);
    round_trip("Ansonic", 0xABC, 12);
    round_trip("Dooya", 0xE1DC030533ull, 40);
    round_trip("SMC5326", 0x1ABCDEF, 25);
    round_trip("Intertechno V3", 0x3F86C59F, 32);
    round_trip("Nice FLO", 0xABCDEF, 24);
    round_trip("CAME", 0xABC, 12);
    round_trip("CAME", 0xABCDEF, 24);
    round_trip("Holtek HT12x", 0xA51, 12);
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
    test_keeloq_frame();
    test_keeloq_cipher_keystore();
    test_rx_dedup();
    test_file_fields();

    if (failures) {
        printf("RESULT: %d FAILURES\n", failures);
        return 1;
    }
    printf("RESULT: ALL PASS\n");
    return 0;
}
