#ifndef BP_CC1101_H
#define BP_CC1101_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    CC1101_PRESET_OOK_270,
    CC1101_PRESET_OOK_650,
    CC1101_PRESET_2FSK_DEV238,
    CC1101_PRESET_2FSK_DEV12K,
    CC1101_PRESET_2FSK_DEV476,
    CC1101_PRESET_GFSK_10K,
    CC1101_PRESET_MSK_100K,
    CC1101_PRESET_CUSTOM,
    CC1101_PRESET_COUNT,
} cc1101_preset_t;

typedef enum {
    CC1101_BAND_300 = 0,
    CC1101_BAND_433 = 1,
    CC1101_BAND_868 = 2,
} cc1101_band_t;

typedef struct {
    const char *name;
    const char *description;
    cc1101_preset_t preset;
} cc1101_preset_info_t;

bool cc1101_init(uint32_t frequency_khz);
void cc1101_deinit(void);
bool cc1101_present(uint8_t *part, uint8_t *version);
bool cc1101_set_frequency(uint32_t frequency_khz);
bool cc1101_set_preset(cc1101_preset_t preset);
bool cc1101_set_bandwidth(uint16_t bandwidth_khz);
bool cc1101_set_tx_power(uint8_t power);
bool cc1101_set_tx_power_dbm(int8_t dbm, bool is_ook);
int8_t cc1101_tx_power_dbm_default(uint32_t khz);

/* Custom preset: RAM-held register list applied like a preset. */
#define CC1101_CUSTOM_MAX_PAIRS 48u
#define CC1101_CUSTOM_PA_SIZE 8u
bool cc1101_custom_preset_set(const uint8_t *pairs, size_t pair_count);
bool cc1101_custom_preset_set_full(const uint8_t *pairs, size_t pair_count,
                                   const uint8_t *pa_table);
size_t cc1101_custom_preset_get(uint8_t *pairs, size_t max_pairs);
bool cc1101_custom_preset_get_pa(uint8_t *pa_table);
bool cc1101_read_config(uint8_t start_addr, uint8_t *values, size_t count);
bool cc1101_read_patable(uint8_t *pa_table);
bool cc1101_set_bitrate(uint32_t bitrate_baud);
bool cc1101_rx(void);
bool cc1101_tx(void);
bool cc1101_idle(void);
int16_t cc1101_rssi(void);

const cc1101_preset_info_t *cc1101_preset_info(cc1101_preset_t preset);
const char *cc1101_preset_name(cc1101_preset_t preset);
cc1101_preset_t cc1101_preset_from_name(const char *name);
bool cc1101_preset_is_ook(cc1101_preset_t preset);
bool cc1101_preset_is_fsk(cc1101_preset_t preset);

#endif
