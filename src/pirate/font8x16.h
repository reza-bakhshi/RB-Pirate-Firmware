/**
 * @file font8x16.h
 * @brief Standard 8x16 monospace font bitmap table for terminal and DVI video.
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FONT8X16_WIDTH       8
#define FONT8X16_HEIGHT      16
#define FONT8X16_FIRST_CHAR  0x20
#define FONT8X16_LAST_CHAR   0x8F

extern const uint8_t font8x16_bitmap[];

static inline const uint8_t* font8x16_get_glyph(char c) {
    uint8_t ch = (uint8_t)c;
    if (ch < FONT8X16_FIRST_CHAR || ch > FONT8X16_LAST_CHAR) {
        ch = ' ';
    }
    return &font8x16_bitmap[(ch - FONT8X16_FIRST_CHAR) * FONT8X16_HEIGHT];
}

#ifdef __cplusplus
}
#endif
