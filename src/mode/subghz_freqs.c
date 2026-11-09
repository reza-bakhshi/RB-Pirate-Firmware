/* SubGHz frequency list and bands matching Flipper Zero / ESP32-Bit-Pirate.
 * Copyright (c) 2026 Bus Pirate contributors. SPDX-License-Identifier: MIT
 */
#include "mode/subghz_freqs.h"
#include <string.h>
#include <strings.h>

const char *const subghz_scan_band_names[SUBGHZ_BAND_COUNT] = {
    "All ranges",
    "300-348 MHz",
    "387-464 MHz",
    "779-928 MHz",
};

const int subghz_range_limits[SUBGHZ_BAND_COUNT][2] = {
    { 0,  90 },
    { 0,  25 },
    { 26, 59 },
    { 60, 90 },
};

/* Discrete sub-GHz channels in kHz (91 frequencies). */
const uint32_t subghz_freq_list[] = {
    /* 300-348 MHz (26 frequencies) */
    300000, 302757, 303875, 303900, 304250, 307000, 307500, 307800,
    309000, 310000, 312000, 312100, 312200, 313000, 313850, 314000,
    314350, 314960, 314980, 315000, 315020, 318000, 330000, 345000,
    348000, 350000,
    /* 387-464 MHz (34 frequencies) */
    387000, 389900, 390000, 390100, 418000, 418500, 430000, 430500,
    431000, 431500, 433050, 433075, 433220, 433420, 433657, 433800,
    433889, 433900, 433920, 433940, 434000, 434075, 434177, 434190,
    434390, 434420, 434500, 434620, 434775, 434790, 438900, 440175,
    464000, 467750,
    /* 779-928 MHz (31 frequencies) */
    779000,
    868000, 868350, 868400, 868600, 868800, 868950, 869000, 869200,
    869400, 869500, 869650, 869700, 869850,
    902000, 904000, 906400, 908000, 910000, 912000, 914000, 915000,
    916000, 918000, 920000, 922000, 924000, 925000, 926000, 927500,
    928000,
};

const size_t subghz_freq_list_count = sizeof(subghz_freq_list) / sizeof(subghz_freq_list[0]);

subghz_scan_band_t subghz_band_from_name(const char *name) {
    if (!name || !*name) return SUBGHZ_BAND_ALL;
    if (strcasecmp(name, "all") == 0 || strcasecmp(name, "0") == 0 ||
        strcasecmp(name, "All ranges") == 0) {
        return SUBGHZ_BAND_ALL;
    }
    if (strcasecmp(name, "300") == 0 || strcasecmp(name, "315") == 0 ||
        strcasecmp(name, "1") == 0 || strstr(name, "300-348")) {
        return SUBGHZ_BAND_300;
    }
    if (strcasecmp(name, "433") == 0 || strcasecmp(name, "387") == 0 ||
        strcasecmp(name, "2") == 0 || strstr(name, "387-464")) {
        return SUBGHZ_BAND_433;
    }
    if (strcasecmp(name, "868") == 0 || strcasecmp(name, "915") == 0 ||
        strcasecmp(name, "3") == 0 || strstr(name, "779-928")) {
        return SUBGHZ_BAND_868;
    }
    return SUBGHZ_BAND_ALL;
}

bool subghz_band_range(subghz_scan_band_t band, size_t *start_idx, size_t *end_idx) {
    if (band >= SUBGHZ_BAND_COUNT) return false;
    if (start_idx) *start_idx = (size_t)subghz_range_limits[band][0];
    if (end_idx) *end_idx = (size_t)subghz_range_limits[band][1];
    return true;
}

const uint32_t *subghz_band_frequencies(subghz_scan_band_t band, size_t *count) {
    size_t s, e;
    if (!subghz_band_range(band, &s, &e)) {
        if (count) *count = 0;
        return NULL;
    }
    if (count) *count = (e - s + 1);
    return &subghz_freq_list[s];
}
