/* SubGHz frequency list and bands matching Flipper Zero / ESP32-Bit-Pirate.
 * Copyright (c) 2026 Bus Pirate contributors. SPDX-License-Identifier: MIT
 */
#ifndef BP_SUBGHZ_FREQS_H
#define BP_SUBGHZ_FREQS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    SUBGHZ_BAND_ALL = 0,
    SUBGHZ_BAND_300 = 1,
    SUBGHZ_BAND_433 = 2,
    SUBGHZ_BAND_868 = 3,
    SUBGHZ_BAND_COUNT = 4,
} subghz_scan_band_t;

extern const char *const subghz_scan_band_names[SUBGHZ_BAND_COUNT];
extern const int subghz_range_limits[SUBGHZ_BAND_COUNT][2];
extern const uint32_t subghz_freq_list[];
extern const size_t subghz_freq_list_count;

subghz_scan_band_t subghz_band_from_name(const char *name);
bool subghz_band_range(subghz_scan_band_t band, size_t *start_idx, size_t *end_idx);
const uint32_t *subghz_band_frequencies(subghz_scan_band_t band, size_t *count);

#endif
