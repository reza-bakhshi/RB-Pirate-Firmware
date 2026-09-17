#include <stdint.h>
#include <string.h>
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
    /* Retune and calibrate without touching PATABLE.  TX power is stateful
     * and must survive frequency scans/hops until explicitly changed. */
    if (!cc_strobe(CC_SCAL)) return false;
    sleep_us(50);
    return cc_wait_state(CC_STATE_IDLE, 5u);
}

/* Register addresses (subset used by the presets below). */
#define CC_IOCFG0 0x02u
#define CC_FIFOTHR 0x03u
#define CC_SYNC1 0x04u
#define CC_SYNC0 0x05u
#define CC_PKTCTRL1 0x07u
#define CC_PKTCTRL0 0x08u
#define CC_FSCTRL1 0x0bu
#define CC_FSCTRL0 0x0cu
#define CC_MDMCFG4 0x10u
#define CC_MDMCFG3 0x11u
#define CC_MDMCFG2 0x12u
#define CC_MDMCFG1 0x13u
#define CC_MDMCFG0 0x14u
#define CC_DEVIATN 0x15u
#define CC_MCSM0 0x18u
#define CC_FOCCFG 0x19u
#define CC_BSCFG 0x1au
#define CC_AGCCTRL2 0x1bu
#define CC_AGCCTRL1 0x1cu
#define CC_AGCCTRL0 0x1du
#define CC_WORCTRL 0x20u
#define CC_FREND1 0x21u
#define CC_FREND0 0x22u
#define CC_FSTEST 0x29u
#define CC_PATABLE 0x3eu

/* dBm to PATABLE byte map (from Flipper cc1101_configs.c comments).
 * OOK uses PATABLE[1]; (G)FSK/MSK use PATABLE[0]. */
typedef struct {
    int8_t dbm;
    uint8_t ook;
    uint8_t fsk;
} cc_pa_entry_t;

static const cc_pa_entry_t cc_pa_table[] = {
    { 12, 0xc0, 0xc0 }, { 10, 0xc5, 0xc0 }, { 7, 0xcd, 0xc8 }, { 5, 0x86, 0x84 },
    { 0, 0x50, 0x60 },  { -6, 0x37, 0x34 }, { -10, 0x26, 0x34 }, { -15, 0x1d, 0x1d },
    { -20, 0x17, 0x0e }, { -30, 0x03, 0x12 },
};

/* Custom preset storage (Flipper Custom_preset_data equivalent:
 * register pairs + optional 8-byte PA table after the 00 00 terminator). */
static uint8_t custom_pairs[CC1101_CUSTOM_MAX_PAIRS * 2];
static size_t custom_pair_count;
static uint8_t custom_pa[CC1101_CUSTOM_PA_SIZE];
static bool custom_have_pa;
static bool custom_is_ook = true;

bool cc1101_custom_preset_set_full(const uint8_t *pairs, size_t pair_count,
                                   const uint8_t *pa_table) {
    if (!pairs || !pair_count || pair_count > CC1101_CUSTOM_MAX_PAIRS) {
        return false;
    }
    memcpy(custom_pairs, pairs, pair_count * 2u);
    custom_pair_count = pair_count;
    if (pa_table) {
        memcpy(custom_pa, pa_table, CC1101_CUSTOM_PA_SIZE);
        custom_have_pa = true;
    } else {
        custom_have_pa = false;
    }
    /* Sniff MDMCFG2 (0x12) modulation format: 000 = ASK/OOK. */
    custom_is_ook = true;
    for (size_t i = 0; i < pair_count; i++) {
        if (pairs[i * 2u] == 0x12u) {
            custom_is_ook = ((pairs[i * 2u + 1u] >> 4) & 7u) == 0u;
        }
    }
    return true;
}

bool cc1101_custom_preset_set(const uint8_t *pairs, size_t pair_count) {
    return cc1101_custom_preset_set_full(pairs, pair_count, NULL);
}

bool cc1101_custom_preset_get_pa(uint8_t *pa_table) {
    if (!pa_table || !custom_have_pa) {
        return false;
    }
    memcpy(pa_table, custom_pa, CC1101_CUSTOM_PA_SIZE);
    return true;
}

size_t cc1101_custom_preset_get(uint8_t *pairs, size_t max_pairs) {
    if (!pairs) {
        return custom_pair_count;
    }
    size_t n = custom_pair_count < max_pairs ? custom_pair_count : max_pairs;
    memcpy(pairs, custom_pairs, n * 2u);
    return n;
}

bool cc1101_read_config(uint8_t start_addr, uint8_t *values, size_t count) {
    if (!values) {
        return false;
    }
    if (start_addr == CC_PATABLE) {
        /* PATABLE is a FIFO-style burst region; single reads do not step it. */
        return false;
    }
    for (size_t i = 0; i < count; i++) {
        if (!cc_read((uint8_t)(start_addr + i), false, &values[i])) {
            return false;
        }
    }
    return true;
}

bool cc1101_read_patable(uint8_t *pa_table) {
    if (!pa_table) {
        return false;
    }
    if (!cc_select_ready()) {
        return false;
    }
    cc_transfer(CC_PATABLE | CC_READ_BURST);
    for (uint i = 0; i < CC1101_CUSTOM_PA_SIZE; i++) {
        pa_table[i] = cc_transfer(0xff);
    }
    hwspi_deselect();
    return true;
}

static const cc1101_preset_info_t preset_info_table[] = {
    [CC1101_PRESET_OOK_270]    = { "Ook270Async",    "OOK 270kHz BW async",    CC1101_PRESET_OOK_270 },
    [CC1101_PRESET_OOK_650]    = { "Ook650Async",    "OOK 650kHz BW async",    CC1101_PRESET_OOK_650 },
    [CC1101_PRESET_2FSK_DEV238] = { "2FSKDev238",    "2FSK deviation 2.38kHz", CC1101_PRESET_2FSK_DEV238 },
    [CC1101_PRESET_2FSK_DEV12K] = { "2FSKDev12K",    "2FSK deviation 12kHz",   CC1101_PRESET_2FSK_DEV12K },
    [CC1101_PRESET_2FSK_DEV476] = { "2FSKDev476",    "2FSK deviation 47.6kHz", CC1101_PRESET_2FSK_DEV476 },
    [CC1101_PRESET_GFSK_10K]   = { "GFSK10K",       "GFSK 9.99kbps",          CC1101_PRESET_GFSK_10K },
    [CC1101_PRESET_MSK_100K]   = { "MSK100K",        "MSK 99.97kbps",          CC1101_PRESET_MSK_100K },
    [CC1101_PRESET_CUSTOM]     = { "Custom",        "Custom register set",    CC1101_PRESET_CUSTOM },
};

const cc1101_preset_info_t *cc1101_preset_info(cc1101_preset_t preset) {
    if (preset >= CC1101_PRESET_COUNT) return NULL;
    return &preset_info_table[preset];
}

const char *cc1101_preset_name(cc1101_preset_t preset) {
    const cc1101_preset_info_t *info = cc1101_preset_info(preset);
    return info ? info->name : "Unknown";
}

cc1101_preset_t cc1101_preset_from_name(const char *name) {
    for (uint i = 0; i < CC1101_PRESET_COUNT; i++) {
        if (strcmp(preset_info_table[i].name, name) == 0) return (cc1101_preset_t)i;
    }
    return CC1101_PRESET_COUNT;
}

bool cc1101_preset_is_ook(cc1101_preset_t preset) {
    if (preset == CC1101_PRESET_CUSTOM) {
        return custom_is_ook;
    }
    return preset == CC1101_PRESET_OOK_270 || preset == CC1101_PRESET_OOK_650;
}

bool cc1101_preset_is_fsk(cc1101_preset_t preset) {
    if (preset == CC1101_PRESET_CUSTOM) {
        return !custom_is_ook;
    }
    return preset == CC1101_PRESET_2FSK_DEV238 || preset == CC1101_PRESET_2FSK_DEV12K ||
           preset == CC1101_PRESET_2FSK_DEV476 || preset == CC1101_PRESET_GFSK_10K ||
           preset == CC1101_PRESET_MSK_100K;
}

bool cc1101_set_bandwidth(uint16_t bandwidth_khz) {
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

/* Legacy OOK bandwidth setter - delegates to preset system. */
bool cc1101_set_preset(cc1101_preset_t preset) {
    if (!cc_enter_idle()) return false;

    switch (preset) {
    case CC1101_PRESET_OOK_270: {
        /* Flipper OOK 270 async preset. */
        static const uint8_t regs[][2] = {
            {0x02,0x0d}, {0x03,0x47}, {0x08,0x32},
            {0x0b,0x06}, {0x10,0x67}, {0x11,0x32},
            {0x12,0x30}, {0x13,0x00}, {0x14,0x00},
            {0x18,0x18}, {0x19,0x18}, {0x1b,0x03},
            {0x1c,0x00}, {0x1d,0x40}, {0x20,0xfb},
            {0x21,0xb6}, {0x22,0x11},
        };
        for (uint i = 0; i < count_of(regs); i++) {
            if (!cc_write(regs[i][0], regs[i][1])) return false;
        }
        return true;
    }
    case CC1101_PRESET_OOK_650: {
        /* Flipper OOK 650 async preset. */
        static const uint8_t regs[][2] = {
            {0x02,0x0d}, {0x03,0x07}, {0x08,0x32},
            {0x0b,0x06}, {0x10,0x17}, {0x11,0x32},
            {0x12,0x30}, {0x13,0x00}, {0x14,0x00},
            {0x18,0x18}, {0x19,0x18}, {0x1b,0x07},
            {0x1c,0x00}, {0x1d,0x91}, {0x20,0xfb},
            {0x21,0xb6}, {0x22,0x11},
        };
        for (uint i = 0; i < count_of(regs); i++) {
            if (!cc_write(regs[i][0], regs[i][1])) return false;
        }
        return true;
    }
    case CC1101_PRESET_2FSK_DEV238: {
        /* Flipper 2FSK deviation 2.38kHz async preset. */
        static const uint8_t regs[][2] = {
            {CC_IOCFG0, 0x0d}, {CC_FSCTRL1, 0x06},
            {CC_PKTCTRL0, 0x32}, {CC_PKTCTRL1, 0x04},
            {CC_MDMCFG0, 0x00}, {CC_MDMCFG1, 0x02},
            {CC_MDMCFG2, 0x04}, /* 2-FSK, no preamble/sync. */
            {CC_MDMCFG3, 0x83}, /* 4.79kBaud. */
            {CC_MDMCFG4, 0x67}, /* 270.8kHz RX BW. */
            {CC_DEVIATN, 0x04}, /* 2.38kHz deviation. */
            {CC_MCSM0, 0x18}, {CC_FOCCFG, 0x16},
            {CC_AGCCTRL2, 0x07}, {CC_AGCCTRL1, 0x00}, {CC_AGCCTRL0, 0x91},
            {CC_WORCTRL, 0xfb},
            {CC_FREND0, 0x10}, {CC_FREND1, 0x56},
        };
        for (uint i = 0; i < count_of(regs); i++) {
            if (!cc_write(regs[i][0], regs[i][1])) return false;
        }
        return true;
    }
    case CC1101_PRESET_2FSK_DEV12K: {
        /* Flipper 2FSK deviation ~12kHz async preset. */
        static const uint8_t regs[][2] = {
            {CC_IOCFG0, 0x0d}, {CC_FSCTRL1, 0x06},
            {CC_PKTCTRL0, 0x32}, {CC_PKTCTRL1, 0x04},
            {CC_MDMCFG0, 0x00}, {CC_MDMCFG1, 0x02},
            {CC_MDMCFG2, 0x04}, /* 2-FSK, no preamble/sync. */
            {CC_MDMCFG3, 0x83}, /* 4.79kBaud. */
            {CC_MDMCFG4, 0x67}, /* 270.8kHz RX BW. */
            {CC_DEVIATN, 0x30}, /* ~12kHz deviation. */
            {CC_MCSM0, 0x18}, {CC_FOCCFG, 0x16},
            {CC_AGCCTRL2, 0x07}, {CC_AGCCTRL1, 0x00}, {CC_AGCCTRL0, 0x91},
            {CC_WORCTRL, 0xfb},
            {CC_FREND0, 0x10}, {CC_FREND1, 0x56},
        };
        for (uint i = 0; i < count_of(regs); i++) {
            if (!cc_write(regs[i][0], regs[i][1])) return false;
        }
        return true;
    }
    case CC1101_PRESET_2FSK_DEV476: {
        /* Flipper 2FSK deviation 47.6kHz async preset. */
        static const uint8_t regs[][2] = {
            {CC_IOCFG0, 0x0d}, {CC_FSCTRL1, 0x06},
            {CC_PKTCTRL0, 0x32}, {CC_PKTCTRL1, 0x04},
            {CC_MDMCFG0, 0x00}, {CC_MDMCFG1, 0x02},
            {CC_MDMCFG2, 0x04}, /* 2-FSK, no preamble/sync. */
            {CC_MDMCFG3, 0x83}, /* 4.79kBaud. */
            {CC_MDMCFG4, 0x67}, /* 270.8kHz RX BW. */
            {CC_DEVIATN, 0x47}, /* 47.6kHz deviation. */
            {CC_MCSM0, 0x18}, {CC_FOCCFG, 0x16},
            {CC_AGCCTRL2, 0x07}, {CC_AGCCTRL1, 0x00}, {CC_AGCCTRL0, 0x91},
            {CC_WORCTRL, 0xfb},
            {CC_FREND0, 0x10}, {CC_FREND1, 0x56},
        };
        for (uint i = 0; i < count_of(regs); i++) {
            if (!cc_write(regs[i][0], regs[i][1])) return false;
        }
        return true;
    }
    case CC1101_PRESET_GFSK_10K: {
        /* Flipper GFSK 9.99kBaud async preset (sync-word capable). */
        static const uint8_t regs[][2] = {
            {CC_IOCFG0, 0x06}, {CC_FIFOTHR, 0x47},
            {CC_PKTCTRL0, 0x05}, {CC_FSCTRL1, 0x06},
            {CC_SYNC1, 0x46}, {CC_SYNC0, 0x4c},
            {CC_MDMCFG4, 0xc8}, {CC_MDMCFG3, 0x93},
            {CC_MDMCFG2, 0x12}, /* GFSK, 16/16 sync word. */
            {CC_DEVIATN, 0x34}, /* 19.0kHz deviation. */
            {CC_MCSM0, 0x18}, {CC_FOCCFG, 0x16},
            {CC_AGCCTRL2, 0x43}, {CC_AGCCTRL1, 0x40}, {CC_AGCCTRL0, 0x91},
            {CC_WORCTRL, 0xfb},
        };
        for (uint i = 0; i < count_of(regs); i++) {
            if (!cc_write(regs[i][0], regs[i][1])) return false;
        }
        return true;
    }
    case CC1101_PRESET_MSK_100K: {
        /* Flipper MSK 99.97kBaud async preset. */
        static const uint8_t regs[][2] = {
            {CC_IOCFG0, 0x06}, {CC_FIFOTHR, 0x07},
            {CC_SYNC1, 0x46}, {CC_SYNC0, 0x4c},
            {CC_PKTCTRL0, 0x05},
            {CC_FSCTRL0, 0x23}, {CC_FSCTRL1, 0x06},
            {CC_MDMCFG0, 0xf8}, {CC_MDMCFG1, 0x22},
            {CC_MDMCFG2, 0x72}, /* MSK. */
            {CC_MDMCFG3, 0xf8}, {CC_MDMCFG4, 0x5b},
            {CC_DEVIATN, 0x47},
            {CC_MCSM0, 0x18}, {CC_FOCCFG, 0x16},
            {CC_AGCCTRL2, 0xc7}, {CC_AGCCTRL1, 0x00}, {CC_AGCCTRL0, 0xb2},
            {CC_FREND0, 0x10}, {CC_FREND1, 0x56},
            {CC_BSCFG, 0x1c}, {CC_FSTEST, 0x59},
        };
        for (uint i = 0; i < count_of(regs); i++) {
            if (!cc_write(regs[i][0], regs[i][1])) return false;
        }
        return true;
    }
    case CC1101_PRESET_CUSTOM: {
        if (!custom_pair_count) {
            return false;
        }
        for (size_t i = 0; i < custom_pair_count; i++) {
            if (!cc_write(custom_pairs[i * 2u], custom_pairs[i * 2u + 1u])) {
                return false;
            }
        }
        if (custom_have_pa) {
            if (!cc_write_burst(CC_PATABLE, custom_pa, CC1101_CUSTOM_PA_SIZE)) {
                return false;
            }
        }
        return true;
    }
    default:
        return false;
    }
}

/* Legacy API: delegates to OOK preset. */
bool cc1101_set_ook_bandwidth(uint16_t bandwidth_khz) {
    return cc1101_set_preset(bandwidth_khz == 650 ? CC1101_PRESET_OOK_650 : CC1101_PRESET_OOK_270);
}

bool cc1101_set_tx_power(uint8_t power) {
    /* power: 0-255 mapped to PA table. Default per-band values if 0. */
    if (!cc_enter_idle()) return false;
    /* PA table address 0x3E, 8 bytes burst write.
     * For OOK: PATABLE[0]=0x00 (off), PATABLE[1]=power.
     * For FSK: PATABLE[0]=power (used for both symbols). */
    uint8_t pa_table[8] = { 0 };
    pa_table[0] = power;
    pa_table[1] = power;
    return cc_write_burst(0x3e, pa_table, 8);
}

bool cc1101_set_tx_power_dbm(int8_t dbm, bool is_ook) {
    if (!cc_enter_idle()) {
        return false;
    }
    /* Pick the closest table entry at or below the requested level. */
    const cc_pa_entry_t *best = &cc_pa_table[count_of(cc_pa_table) - 1];
    for (uint i = 0; i < count_of(cc_pa_table); i++) {
        if (cc_pa_table[i].dbm <= dbm) {
            best = &cc_pa_table[i];
            break;
        }
    }
    uint8_t pa_table[8] = { 0 };
    if (is_ook) {
        pa_table[0] = 0x00;
        pa_table[1] = best->ook;
    } else {
        for (uint i = 0; i < 8; i++) {
            pa_table[i] = best->fsk;
        }
    }
    return cc_write_burst(CC_PATABLE, pa_table, 8);
}

int8_t cc1101_tx_power_dbm_default(uint32_t khz) {
    (void)khz;
    return 0; /* Conservative 0dBm default on all bands. */
}

bool cc1101_set_bitrate(uint32_t bitrate_baud) {
    if (bitrate_baud < 600 || bitrate_baud > 500000) return false;
    if (!cc_enter_idle()) return false;

    /* CC1101 bitrate is:
     *   Rb = (256 + DRATE_M) * 2^DRATE_E * FXOSC / 2^28
     * Search the complete legal E/M range and choose the closest rate. */
    const uint64_t fxosc = 26000000ULL;
    uint8_t best_exp = 0;
    uint8_t best_mantissa = 0;
    uint64_t best_error = UINT64_MAX;
    for (uint8_t exp = 0; exp <= 15; exp++) {
        uint64_t denominator = fxosc << exp;
        uint64_t numerator = (uint64_t)bitrate_baud << 28;
        uint64_t scaled = (numerator + denominator / 2) / denominator;
        int32_t mantissa = (int32_t)scaled - 256;
        if (mantissa < 0) mantissa = 0;
        if (mantissa > 255) mantissa = 255;
        uint64_t actual = ((uint64_t)(256 + mantissa) * (1ULL << exp) * fxosc) >> 28;
        uint64_t error = actual > bitrate_baud ? actual - bitrate_baud : bitrate_baud - actual;
        if (error < best_error) {
            best_error = error;
            best_exp = exp;
            best_mantissa = (uint8_t)mantissa;
        }
    }

    uint8_t mdmcfg4;
    if (!cc_read(0x10, false, &mdmcfg4)) return false;
    mdmcfg4 = (mdmcfg4 & 0x0F) | ((best_exp & 0x0F) << 4);
    if (!cc_write(0x10, mdmcfg4)) return false;
    if (!cc_write(0x11, best_mantissa)) return false;

    return true;
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
    if (!cc1101_set_preset(CC1101_PRESET_OOK_270) || !cc1101_set_frequency(frequency_khz)) {
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
