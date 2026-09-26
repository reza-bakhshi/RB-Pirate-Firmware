// Host-side round-trip test for ir_protocols.c (not part of the firmware build)
// gcc -DHOST_TEST -I ../../../src -I . ir_protocols.c test_ir_protocols_host.c -o test
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "pirate/ir_protocols.h"

// stubs for pico/firmware dependencies
void irio_pio_tx_frame_write(float mod_freq, uint16_t pairs, uint32_t *buffer) {
    (void)mod_freq; (void)pairs; (void)buffer;
}

static uint32_t capture[IR_MAX_PAIRS];
static uint16_t capture_count;

// Convert transmitter output into what the RX engine actually captures:
// capture starts at the first carrier burst (mark), and any silent gap
// (mark=0 pairs emitted between split half-bits) merges into the previous
// pair's space duration. The final space is the RX timeout.
static uint16_t normalize(const uint32_t *pairs, uint16_t count) {
    static uint32_t out[IR_MAX_PAIRS + 8];
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
    memcpy(capture, out, n * sizeof(uint32_t));
    return n;
}

static int failures = 0;

static void round_trip(uint8_t proto, uint32_t dev, uint32_t sub, uint32_t cmd) {
    const ir_protocol_def_t *p = ir_protocols_get(proto);
    capture_count = 0;
    uint32_t encoded[IR_MAX_PAIRS];
    uint16_t encoded_count;
    if (!p->encode(dev, sub, cmd, encoded, &encoded_count)) {
        printf("FAIL %s encode(dev=%lx sub=%lx cmd=%lx)\r\n", p->display, (unsigned long)dev, (unsigned long)sub, (unsigned long)cmd);
        failures++;
        return;
    }
    capture_count = normalize(encoded, encoded_count);
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
    if (rdev != dev || rcmd != cmd || (proto == 2 && rsub != sub)) { // RC5 checks toggle too
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

int main(void) {
    printf("ir_protocols host round-trip test\r\n");
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

    if (failures) {
        printf("\r\n%d FAILURES\r\n", failures);
        return 1;
    }
    printf("\r\nALL PASSED\r\n");
    return 0;
}
