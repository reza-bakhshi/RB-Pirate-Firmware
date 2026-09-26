// Host-side tests for the IR protocol engine (not part of the firmware build)
// Part 1: round-trip encode/decode of every hand-written protocol
// Part 2: cross-validation of the MakeHex IRP encoder against the hand
//         written encoders (two independent implementations of the same
//         wire formats must agree)
// Run with tests/test_ir_protocols.sh
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "pirate.h"
#include "pirate/ir_protocols.h"
#include "lib/makehex/MakeHex.h"

// stubs for pico/firmware dependencies
void irio_pio_tx_frame_write(float mod_freq, uint16_t pairs, uint32_t *buffer) {
    (void)mod_freq; (void)pairs; (void)buffer;
}

static int failures = 0;

// ---------------------------------------------------------------------------
// part 1: round trip
// ---------------------------------------------------------------------------

// Convert transmitter output into what the RX engine actually captures:
// capture starts at the first carrier burst (mark), and any silent gap
// (mark=0 pairs emitted between split half-bits) merges into the previous
// pair's space duration. The final space is the RX timeout.
static uint16_t normalize(const uint32_t *pairs, uint16_t count, uint32_t *out) {
    uint16_t n = 0;
    for (uint16_t i = 0; i < count; i++) {
        uint32_t mark = pairs[i] >> 16;
        uint32_t space = pairs[i] & 0xffff;
        if (mark == 0) {
            if (n == 0) {
                continue; // leading silence: not captured
            }
            out[n - 1] += space; // merge silence into the previous space
        } else {
            out[n++] = (mark << 16) | space;
        }
    }
    out[n - 1] |= 0xffff;
    return n;
}

static void round_trip(uint8_t proto, uint32_t dev, uint32_t sub, uint32_t cmd) {
    const ir_protocol_def_t *p = ir_protocols_get(proto);
    uint32_t encoded[IR_MAX_PAIRS];
    uint16_t encoded_count;
    if (!p->encode(dev, sub, cmd, encoded, &encoded_count)) {
        printf("FAIL %s encode(dev=%lx sub=%lx cmd=%lx)\r\n", p->display, (unsigned long)dev, (unsigned long)sub, (unsigned long)cmd);
        failures++;
        return;
    }
    static uint32_t capture[IR_MAX_PAIRS + 8];
    uint16_t capture_count = normalize(encoded, encoded_count, capture);

    int8_t idx;
    uint32_t rdev, rsub, rcmd;
    if (!ir_protocols_decode(capture, capture_count, &idx, &rdev, &rsub, &rcmd)) {
        printf("FAIL %s decode(dev=%lx sub=%lx cmd=%lx): no match\r\n", p->display, (unsigned long)dev, (unsigned long)sub, (unsigned long)cmd);
        failures++;
        return;
    }
    if (idx != (int8_t)proto) {
        printf("FAIL %s decoded as %s\r\n", p->display, ir_protocols_get((uint8_t)idx)->display);
        failures++;
        return;
    }
    if (rdev != dev || rcmd != cmd || (proto == 6 && rsub != sub)) { // RC5 checks toggle too
        printf("FAIL %s round trip: dev %lx!=%lx cmd %lx!=%lx sub %lx!=%lx\r\n", p->display,
               (unsigned long)rdev, (unsigned long)dev, (unsigned long)rcmd, (unsigned long)cmd, (unsigned long)rsub, (unsigned long)sub);
        failures++;
        return;
    }
    printf("OK   %s dev=0x%lx sub=0x%lx cmd=0x%lx (%u pairs)\r\n", p->display, (unsigned long)dev, (unsigned long)sub, (unsigned long)cmd, capture_count);
}

static void noise_reject(const char *desc) {
    // short random-ish frame should not decode
    uint32_t noise[8] = { 0x0000003C, 0x00000030, 0x0002003C, 0x00000030, 0x00000A32, 0x00000030, 0x0000003C, 0xffffffff };
    int8_t idx;
    uint32_t dev, sub, cmd;
    if (ir_protocols_decode(noise, 8, &idx, &dev, &sub, &cmd)) {
        printf("FAIL noise rejected by %s? (decoded as %s)\r\n", desc, ir_protocols_get((uint8_t)idx)->display);
        failures++;
    } else {
        printf("OK   noise rejected (%s)\r\n", desc);
    }
}

// ---------------------------------------------------------------------------
// part 2: MakeHex cross-validation
// ---------------------------------------------------------------------------

// emulate the RX timeout on the last space of a hand-encoded frame
static uint16_t hand_to_capture(uint8_t proto, uint32_t dev, uint32_t sub, uint32_t cmd,
                                uint32_t *out, uint16_t max) {
    const ir_protocol_def_t *p = ir_protocols_get(proto);
    uint32_t encoded[IR_MAX_PAIRS];
    uint16_t count = 0;
    if (!p->encode(dev, sub, cmd, encoded, &count)) {
        printf("FAIL hand encode %s\r\n", p->display);
        failures++;
        return 0;
    }
    uint16_t n = normalize(encoded, count, out);
    if (n > max) n = max;
    return n;
}

static void compare(const char *what, const uint32_t *a, uint16_t an, const uint32_t *b, uint16_t bn) {
    // MakeHex sequences may append the repeat-frame prefix/suffix after the
    // single frame; only the single-frame portion is compared
    if (bn < an) {
        printf("FAIL %s: pair count %u vs %u\r\n", what, an, bn);
        failures++;
        return;
    }
    for (uint16_t i = 0; i < an; i++) {
        uint32_t am = a[i] >> 16, as = a[i] & 0xffff;
        uint32_t bm = b[i] >> 16, bs = b[i] & 0xffff;
        // the trailing gap (inter-frame delay) is an implementation detail
        if (as == 0xffff || bs == 0xffff) {
            if (am != bm) {
                uint32_t tol = am / 20 + 3;
                uint32_t dm = am > bm ? am - bm : bm - am;
                if (dm > tol) {
                    printf("FAIL %s: pair %u mark %lu vs %lu\r\n", what, i, (unsigned long)am, (unsigned long)bm);
                    failures++;
                    return;
                }
            }
            continue;
        }
        // 5% timing tolerance (MakeHex uses slightly rounded base values)
        uint32_t tol_m = am / 20 + 3, tol_s = as / 20 + 3;
        uint32_t dm = am > bm ? am - bm : bm - am;
        uint32_t ds = as > bs ? as - bs : bs - as;
        if (dm > tol_m || ds > tol_s) {
            printf("FAIL %s: pair %u mark %lu vs %lu, space %lu vs %lu\r\n", what, i,
                   (unsigned long)am, (unsigned long)bm, (unsigned long)as, (unsigned long)bs);
            failures++;
            return;
        }
    }
    printf("OK   %s (%u pairs match)\r\n", what, an);
}

static void cross_check(uint8_t proto, const char *mh_name, int32_t sub, uint32_t dev, uint32_t cmd) {
    uint32_t hand[IR_MAX_PAIRS], mh[IR_MAX_PAIRS];
    uint16_t hn = hand_to_capture(proto, dev, sub == -1 ? 0 : sub, cmd, hand, IR_MAX_PAIRS);
    uint32_t freq = 0;
    uint16_t mn = 0;
    char what[80];
    snprintf(what, sizeof(what), "%s hand vs MakeHex %s", ir_protocols_get(proto)->display, mh_name);
    if (!makehex_generate(mh_name, (int32_t)dev, sub, (int32_t)cmd, mh, &mn, IR_MAX_PAIRS, &freq)) {
        printf("FAIL makehex generate %s\r\n", mh_name);
        failures++;
        return;
    }
    if (mn > 0) {
        mh[mn - 1] |= 0xffff;
    }
    compare(what, hand, hn, mh, mn);
}

static void makehex_sanity(void) {
    static const struct {
        const char *name;
        uint16_t min_pairs, max_pairs;
        uint32_t min_khz, max_khz;
    } checks[] = {
        { "mce",      20, 100, 30, 40 },
        { "rca",      10, 80,  30, 60 },
        { "rc6-M-L",  15, 100, 30, 40 },
        { "Denon-K",  30, 70,  30, 40 },
        { "xmp",     10, 130, 30, 60 },
        { "Kaseikyo", 40, 70,  30, 40 },
        { "f12",      15, 80,  30, 45 },
    };
    for (uint8_t i = 0; i < count_of(checks); i++) {
        uint32_t mh[IR_MAX_PAIRS];
        uint16_t count = 0;
        uint32_t freq = 0;
        if (!makehex_generate(checks[i].name, 1, 0, 0x10, mh, &count, IR_MAX_PAIRS, &freq)) {
            printf("FAIL makehex generate %s\r\n", checks[i].name);
            failures++;
            continue;
        }
        if (count < checks[i].min_pairs || count > checks[i].max_pairs ||
            freq < checks[i].min_khz * 1000 || freq > checks[i].max_khz * 1000) {
            printf("FAIL makehex %s sanity: %u pairs, %lu Hz\r\n", checks[i].name, count, (unsigned long)freq);
            failures++;
            continue;
        }
        printf("OK   makehex %s: %u pairs, %lu Hz\r\n", checks[i].name, count, (unsigned long)freq);
    }

    // every MakeHex table entry must generate for a sample command
    uint8_t gen_fail = 0;
    for (uint8_t i = IR_PROTOCOL_HAND_COUNT; i < ir_protocols_count(); i++) {
        uint32_t mh[IR_MAX_PAIRS];
        uint16_t count = 0;
        uint32_t freq = 0;
        if (!makehex_generate(ir_protocols_get(i)->makehex_name, 1, 0, 0x10, mh, &count, IR_MAX_PAIRS, &freq) || count == 0) {
            printf("FAIL makehex table entry '%s' does not generate\r\n", ir_protocols_get(i)->display);
            gen_fail++;
        }
    }
    if (gen_fail) {
        failures += gen_fail;
    } else {
        printf("OK   all %u MakeHex table entries generate\r\n", ir_protocols_count() - IR_PROTOCOL_HAND_COUNT);
    }
}

int main(void) {
    printf("ir_protocols host tests\r\n");
    // part 1: round trips
    // NEC: 8-bit address, extended address, and repeat frame
    round_trip(0, 0x04, 0, 0x08);
    round_trip(0, 0x1FE8, 0, 0x17); // extended 16-bit address (no inversion possible)
    round_trip(0, 0xFF, 0, 0x00);
    // SAMSUNG
    round_trip(1, 0x07, 0, 0x02);
    round_trip(1, 0x00, 0, 0xFF);
    // LG
    round_trip(2, 0x04, 0, 0x08);
    round_trip(2, 0x7F, 0, 0x80);
    // SONY (12, 15, 20 bit)
    round_trip(3, 0x01, 0, 0x15);
    round_trip(3, 0x2A, 0, 0x7F); // 15-bit (address > 5 bits)
    round_trip(3, 0x1ABC, 0, 0x55); // 20-bit
    // JVC
    round_trip(4, 0x03, 0, 0x8A);
    // PANASONIC (Kaseikyo, Bit-Pirate devicebgone power code dev=8 sub=0 func=61)
    round_trip(5, 8, 0, 61);
    round_trip(5, 0x0C, 1, 30);
    // RC5 (with toggle 0 and 1, plus RC5X command)
    round_trip(6, 0x00, 0, 0x0C);
    round_trip(6, 0x1F, 1, 0x3F);
    round_trip(6, 0x05, 0, 0x40 | 0x2A); // RC5X
    // RC6
    round_trip(7, 0x00, 0, 0x00);
    round_trip(7, 0xAB, 0, 0xCD);
    // DISH
    round_trip(8, 0x01, 0, 0x10);
    // SHARP
    round_trip(9, 0x02, 0, 0x40);
    // Kaseikyo vendor variants (Bit-Pirate devicebgone codes)
    round_trip(10, 4, 0, 8);     // DENON
    round_trip(11, 71, 0, 2);    // MITSUBISHI
    round_trip(12, 1, 0, 233);   // SHARP (Kaseikyo)
    // noise rejection
    noise_reject("NEC");
    noise_reject("SAMSUNG");
    noise_reject("PANASONIC");
    noise_reject("RC6");

    // part 2: MakeHex cross-validation + sanity
    // nec1 with sub unspecified: S defaults to ~D -> classic NEC wire
    cross_check(0, "nec1", -1, 4, 8);      // NEC
    cross_check(3, "sony12", 0, 1, 21);    // SONY
    cross_check(6, "rc5", 0, 0, 12);       // RC5
    // note: no SAMSUNG cross-check - Bit-Pirate sends SAMSUNG via IRremote
    // (Samsung32 wire = our hand encoder), while the IRDB "Samsung20"
    // definition is a different 20-bit convention
    makehex_sanity();

    if (failures) {
        printf("\r\n%d FAILURES\r\n", failures);
        return 1;
    }
    printf("\r\nALL PASSED\r\n");
    return 0;
}
