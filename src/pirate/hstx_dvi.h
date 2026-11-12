/**
 * @file hstx_dvi.h
 * @brief RP2350 HSTX DVI Digital Video Generator & VT100 Terminal Mirror.
 * @details Generates 640x480 60Hz DVI bitstreams utilizing the RP2350 HSTX
 *          high-speed serial transmit serializer and DMA scanline ping-pong buffers.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HSTX_DVI_WIDTH          640
#define HSTX_DVI_HEIGHT         480
#define HSTX_DVI_TEXT_COLS      80
#define HSTX_DVI_TEXT_ROWS      30

// Standard 16-color ANSI palette codes (packed 4-bit nibbles: high=bg, low=fg)
#define HSTX_COLOR_BLACK        0x0
#define HSTX_COLOR_BLUE         0x1
#define HSTX_COLOR_GREEN        0x2
#define HSTX_COLOR_CYAN         0x3
#define HSTX_COLOR_RED          0x4
#define HSTX_COLOR_MAGENTA      0x5
#define HSTX_COLOR_BROWN        0x6
#define HSTX_COLOR_WHITE        0x7
#define HSTX_COLOR_GRAY         0x8
#define HSTX_COLOR_BRIGHT_BLUE  0x9
#define HSTX_COLOR_BRIGHT_GREEN 0xA
#define HSTX_COLOR_BRIGHT_CYAN  0xB
#define HSTX_COLOR_BRIGHT_RED   0xC
#define HSTX_COLOR_BRIGHT_MAG   0xD
#define HSTX_COLOR_YELLOW       0xE
#define HSTX_COLOR_BRIGHT_WHITE 0xF

#define HSTX_ATTR_DEFAULT       ((HSTX_COLOR_BLACK << 4) | HSTX_COLOR_BRIGHT_WHITE)

/**
 * @brief Initialize the HSTX DVI video subsystem.
 * @return true if hardware HSTX initialized successfully, false if unsupported.
 */
bool hstx_dvi_init(void);

/**
 * @brief Query if HSTX DVI hardware is supported on the active platform.
 */
bool hstx_dvi_is_supported(void);

/**
 * @brief Query if HSTX DVI video generation is currently enabled.
 */
bool hstx_dvi_is_enabled(void);

/**
 * @brief Enable or disable HSTX DVI digital video generation.
 */
void hstx_dvi_enable(bool enable);

/**
 * @brief Clear the external video screen and reset cursor.
 */
void hstx_dvi_clear(void);

/**
 * @brief Set the video cursor position.
 * @param col Column (0 to 79)
 * @param row Row (0 to 29)
 */
void hstx_dvi_set_cursor(uint8_t col, uint8_t row);

/**
 * @brief Set current text color attribute.
 * @param fg Foreground color index (0-15)
 * @param bg Background color index (0-15)
 */
void hstx_dvi_set_color(uint8_t fg, uint8_t bg);

/**
 * @brief Write a character to the video console (processes \r, \n, \b, scrolling).
 */
void hstx_dvi_write_char(char c);

/**
 * @brief Write a null-terminated string to the video console.
 */
void hstx_dvi_write_string(const char *str);

/**
 * @brief Render one scanline into a 640-byte destination buffer.
 * @param line_y Scanline index (0 to 479)
 * @param line_buf Destination line buffer (must be >= 640 bytes)
 */
void hstx_dvi_render_scanline(uint16_t line_y, uint8_t *line_buf);

/**
 * @brief Background service called periodically to pump DMA scanlines.
 */
void hstx_dvi_service(void);

/**
 * @brief Render a standard color test pattern and banner on external display.
 */
void hstx_dvi_test_pattern(void);

/**
 * @brief Get read-only pointer to the 80x30 text buffer.
 */
const char* hstx_dvi_get_text_buffer(void);

#ifdef __cplusplus
}
#endif
