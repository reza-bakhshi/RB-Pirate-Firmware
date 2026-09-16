#include <stdint.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "pirate.h"
#include "pirate/bio.h"
#include "pirate/hwspi.h"
#include "pirate/cc1101.h"

#define CC_WRITE_BURST 0x40u
#define CC_READ_SINGLE 0x80u
#define CC_READ_BURST  0xc0u
#define CC_SRES  0x30u
#define CC_SRX   0x34u
#define CC_STX   0x35u
#define CC_SIDLE 0x36u
#define CC_SCAL  0x33u
#define CC_MARCSTATE 0x35u

#define CC_STATE_IDLE 0x01u
#define CC_STATE_RX   0x0du
#define CC_STATE_TX   0x13u

static uint8_t cc_transfer(uint8_t value) {
    return (uint8_t)hwspi_write_read(value);
}

static bool cc_wait_ready_selected(uint32_t timeout_us) {
    absolute_time_t timeout = make_timeout_time_us(timeout_us);
    while (bio_get(M_SPI_CDI)) {
        if (time_reached(timeout)) return false;
        tight_loop_contents();
    }
    return true;
}

static bool cc_select_ready(void) {
    hwspi_select();
    if (cc_wait_ready_selected(2000u)) return true;
    hwspi_deselect();
    return false;
}

static bool cc_strobe(uint8_t command) {
    if (!cc_select_ready()) return false;
    cc_transfer(command);
    hwspi_deselect();
    return true;
}

static bool cc_write(uint8_t address, uint8_t value) {
    if (!cc_select_ready()) return false;
    cc_transfer(address);
    cc_transfer(value);
    hwspi_deselect();
    return true;
}

static bool cc_write_burst(uint8_t address, const uint8_t *values, uint8_t count) {
    if (!cc_select_ready()) return false;
    cc_transfer(address | CC_WRITE_BURST);
    for (uint8_t i = 0; i < count; i++) cc_transfer(values[i]);
    hwspi_deselect();
    return true;
}

static bool cc_read(uint8_t address, bool status, uint8_t *value) {
    if (!cc_select_ready()) return false;
    cc_transfer(address | (status ? CC_READ_BURST : CC_READ_SINGLE));
    *value = cc_transfer(0xff);
    hwspi_deselect();
    return true;
}

static bool cc_wait_state(uint8_t expected, uint32_t timeout_ms) {
    absolute_time_t timeout = make_timeout_time_ms(timeout_ms);
    do {
        uint8_t state;
        if (!cc_read(CC_MARCSTATE, true, &state)) return false;
        if ((state & 0x1fu) == expected) return true;
        tight_loop_contents();
    } while (!time_reached(timeout));
    return false;
}

static bool cc_enter_idle(void) {
    return cc_strobe(CC_SIDLE) && cc_wait_state(CC_STATE_IDLE, 5u);
}

static bool cc_valid_frequency(uint32_t khz) {
    return (khz >= 300000u && khz <= 348000u) ||
           (khz >= 387000u && khz <= 464000u) ||
           (khz >= 779000u && khz <= 928000u);
}

bool cc1101_present(uint8_t *part, uint8_t *version) {
    uint8_t p = 0xff, v = 0xff;
    if (!cc_read(0x30, true, &p) || !cc_read(0x31, true, &v)) return false;
    if (part) *part = p;
    if (version) *version = v;
    return p == 0x00 && v != 0x00 && v != 0xff;
}

bool cc1101_set_frequency(uint32_t khz) {
    if (!cc_valid_frequency(khz)) return false;
    uint32_t word = (uint32_t)(((uint64_t)khz * 65536u + 13000u) / 26000u);
    if (!cc_enter_idle() ||
        !cc_write(0x0d, (word >> 16) & 0xff) ||
        !cc_write(0x0e, (word >> 8) & 0xff) ||
        !cc_write(0x0f, word & 0xff)) return false;
    /* OOK uses PATABLE[0] for logic 0 and PATABLE[1] for logic 1. */
    uint8_t pa = khz <= 348000u ? 0x51 : khz <= 464000u ? 0x60 : khz < 900000u ? 0x50 : 0x8e;
    uint8_t pa_table[8] = { 0x00, pa, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    if (!cc_write_burst(0x3e, pa_table, count_of(pa_table)) || !cc_strobe(CC_SCAL)) return false;
    sleep_us(50);
    return cc_wait_state(CC_STATE_IDLE, 5u);
}

bool cc1101_set_ook_bandwidth(uint16_t bandwidth_khz) {
    if (bandwidth_khz != 270 && bandwidth_khz != 650) return false;
    if (!cc_enter_idle()) return false;

    /* Canonical Flipper CC1101 OOK async presets. */
    const uint8_t fifothr = bandwidth_khz == 270 ? 0x47 : 0x07;
    const uint8_t mdmcfg4 = bandwidth_khz == 270 ? 0x67 : 0x17;
    const uint8_t agcctrl0 = bandwidth_khz == 270 ? 0x40 : 0x91;
    const uint8_t agcctrl2 = bandwidth_khz == 270 ? 0x03 : 0x07;
    static const uint8_t common[][2] = {
        {0x02,0x0d}, /* GDO0 asynchronous serial data. */
        {0x08,0x32}, /* Infinite length, asynchronous serial format. */
        {0x0b,0x06}, {0x14,0x00}, {0x13,0x00},
        {0x12,0x30}, /* ASK/OOK, no preamble or sync. */
        {0x11,0x32}, /* 3.79kbaud. */
        {0x18,0x18}, {0x19,0x18}, {0x1c,0x00},
        {0x20,0xfb}, {0x22,0x11}, {0x21,0xb6},
    };
    for (uint i = 0; i < count_of(common); i++) {
        if (!cc_write(common[i][0], common[i][1])) return false;
    }
    return cc_write(0x03, fifothr) &&
           cc_write(0x10, mdmcfg4) &&
           cc_write(0x1b, agcctrl2) &&
           cc_write(0x1d, agcctrl0);
}

bool cc1101_init(uint32_t frequency_khz) {
    spi_init(M_SPI_PORT, 4000000u);
    hwspi_init(8, 0, 0);
    hwspi_deselect();
    sleep_us(40);
    hwspi_select();
    sleep_us(10);
    hwspi_deselect();
    sleep_us(40);
    hwspi_select();
    if (!cc_wait_ready_selected(10000u)) {
        hwspi_deselect();
        hwspi_deinit();
        return false;
    }
    cc_transfer(CC_SRES);
    sleep_ms(1);
    if (!cc_wait_ready_selected(10000u)) {
        hwspi_deselect();
        hwspi_deinit();
        return false;
    }
    hwspi_deselect();

    uint8_t part, version;
    if (!cc1101_present(&part, &version)) {
        hwspi_deinit();
        return false;
    }

    /* Start from reset defaults, then apply only the selected async OOK preset. */
    if (!cc1101_set_ook_bandwidth(270) || !cc1101_set_frequency(frequency_khz)) {
        hwspi_deinit();
        return false;
    }
    if (!cc1101_rx()) {
        hwspi_deinit();
        return false;
    }
    return true;
}

void cc1101_deinit(void) {
    (void)cc1101_idle();
    hwspi_deinit();
}

bool cc1101_rx(void) { return cc_strobe(CC_SRX) && cc_wait_state(CC_STATE_RX, 5u); }
bool cc1101_tx(void) { return cc_strobe(CC_STX) && cc_wait_state(CC_STATE_TX, 5u); }
bool cc1101_idle(void) { return cc_enter_idle(); }

int16_t cc1101_rssi(void) {
    uint8_t value;
    if (!cc_read(0x34, true, &value)) return -127;
    int16_t raw = value;
    if (raw >= 128) raw -= 256;
    return (raw / 2) - 74;
}
