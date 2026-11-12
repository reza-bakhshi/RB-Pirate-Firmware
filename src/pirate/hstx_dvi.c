/**
 * @file hstx_dvi.c
 * @brief RP2350 HSTX DVI Digital Video Generator & Terminal Mirror.
 */

#include "pirate/hstx_dvi.h"
#include "pirate/font8x16.h"
#include <string.h>
#include <stdio.h>

#if __has_include("hardware/structs/hstx_ctrl.h") && !defined(BP_HOST_TEST)
#include "pico/stdlib.h"
#include "hardware/structs/hstx_ctrl.h"
#include "hardware/structs/hstx_fifo.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"
#endif

// Static memory buffers (Zero runtime heap allocation)
// 80x30 text buffer: 2400 bytes
static char s_text[HSTX_DVI_TEXT_ROWS][HSTX_DVI_TEXT_COLS];
// Row attribute buffer (fg/bg color): 30 bytes
static uint8_t s_row_attr[HSTX_DVI_TEXT_ROWS];

// 2 x 640-byte ping-pong scanline buffers: 1280 bytes
static uint8_t s_line_buf[2][HSTX_DVI_WIDTH];
static uint8_t s_active_buf = 0;

static bool s_supported = false;
static bool s_enabled = false;
static uint8_t s_cursor_x = 0;
static uint8_t s_cursor_y = 0;
static uint8_t s_fg_color = HSTX_COLOR_BRIGHT_WHITE;
static uint8_t s_bg_color = HSTX_COLOR_BLACK;
static uint16_t s_scanline = 0;
static int s_dma_chan = -1;

void hstx_dvi_clear(void) {
    for (uint8_t r = 0; r < HSTX_DVI_TEXT_ROWS; r++) {
        memset(s_text[r], ' ', HSTX_DVI_TEXT_COLS);
        s_row_attr[r] = HSTX_ATTR_DEFAULT;
    }
    s_cursor_x = 0;
    s_cursor_y = 0;
}

static void hstx_dvi_scroll_up(void) {
    for (uint8_t r = 0; r < HSTX_DVI_TEXT_ROWS - 1; r++) {
        memcpy(s_text[r], s_text[r + 1], HSTX_DVI_TEXT_COLS);
        s_row_attr[r] = s_row_attr[r + 1];
    }
    memset(s_text[HSTX_DVI_TEXT_ROWS - 1], ' ', HSTX_DVI_TEXT_COLS);
    s_row_attr[HSTX_DVI_TEXT_ROWS - 1] = HSTX_ATTR_DEFAULT;
}

bool hstx_dvi_init(void) {
    hstx_dvi_clear();
    s_enabled = false;

#if defined(PICO_RP2350) && !defined(BP_HOST_TEST)
    s_supported = true;

    // Configure HSTX peripheral for TMDS serialization
    // Clock divider: 150 MHz / 25 MHz pixel clock = 6
    hstx_ctrl_hw->csr = 0;
    hstx_ctrl_hw->csr = (1u << 0) |              // EN
                        (1u << 1) |              // EXPAND_EN
                        (6u << 8) |              // SHIFT (6 bits)
                        (5u << 16) |             // N_SHIFTS (5)
                        (1u << 28);              // CLKDIV

    // Configure TMDS expander for 8-bit to 10-bit TMDS symbol encoding
    hstx_ctrl_hw->expand_tmds = (8u << 0)  |     // Lane 0: 8 bits
                                (8u << 13) |     // Lane 1: 8 bits
                                (8u << 21);      // Lane 2: 8 bits

    // Claim DMA channel for scanline feeding
    s_dma_chan = dma_claim_unused_channel(false);
    if (s_dma_chan >= 0) {
        dma_channel_config c = dma_channel_get_default_config(s_dma_chan);
        channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
        channel_config_set_read_increment(&c, true);
        channel_config_set_write_increment(&c, false);
        channel_config_set_dreq(&c, 52); // DREQ_HSTX

        dma_channel_configure(
            s_dma_chan,
            &c,
            &hstx_fifo_hw->fifo,
            s_line_buf[0],
            HSTX_DVI_WIDTH / 4,
            false
        );
    }
    return true;
#else
    // Host test or RP2040 fallback
#if defined(BP_HOST_TEST)
    s_supported = true;
    return true;
#else
    s_supported = false;
    return false;
#endif
#endif
}

bool hstx_dvi_is_supported(void) {
    return s_supported;
}

bool hstx_dvi_is_enabled(void) {
    return s_enabled;
}

void hstx_dvi_enable(bool enable) {
    if (!s_supported) {
        s_enabled = false;
        return;
    }
    s_enabled = enable;
    if (enable) {
        s_scanline = 0;
    }
}

void hstx_dvi_set_cursor(uint8_t col, uint8_t row) {
    if (col < HSTX_DVI_TEXT_COLS) s_cursor_x = col;
    if (row < HSTX_DVI_TEXT_ROWS) s_cursor_y = row;
}

void hstx_dvi_set_color(uint8_t fg, uint8_t bg) {
    s_fg_color = fg & 0x0F;
    s_bg_color = bg & 0x0F;
}

void hstx_dvi_write_char(char c) {
    if (c == '\r') {
        s_cursor_x = 0;
        return;
    }
    if (c == '\n') {
        s_cursor_x = 0;
        s_cursor_y++;
        if (s_cursor_y >= HSTX_DVI_TEXT_ROWS) {
            hstx_dvi_scroll_up();
            s_cursor_y = HSTX_DVI_TEXT_ROWS - 1;
        }
        return;
    }
    if (c == '\b') {
        if (s_cursor_x > 0) s_cursor_x--;
        return;
    }
    if (c == '\t') {
        s_cursor_x = (s_cursor_x + 8) & ~7;
        if (s_cursor_x >= HSTX_DVI_TEXT_COLS) {
            s_cursor_x = 0;
            s_cursor_y++;
            if (s_cursor_y >= HSTX_DVI_TEXT_ROWS) {
                hstx_dvi_scroll_up();
                s_cursor_y = HSTX_DVI_TEXT_ROWS - 1;
            }
        }
        return;
    }

    if (s_cursor_x < HSTX_DVI_TEXT_COLS && s_cursor_y < HSTX_DVI_TEXT_ROWS) {
        s_text[s_cursor_y][s_cursor_x] = c;
        s_row_attr[s_cursor_y] = (s_bg_color << 4) | s_fg_color;
        s_cursor_x++;
        if (s_cursor_x >= HSTX_DVI_TEXT_COLS) {
            s_cursor_x = 0;
            s_cursor_y++;
            if (s_cursor_y >= HSTX_DVI_TEXT_ROWS) {
                hstx_dvi_scroll_up();
                s_cursor_y = HSTX_DVI_TEXT_ROWS - 1;
            }
        }
    }
}

void hstx_dvi_write_string(const char *str) {
    if (!str) return;
    while (*str) {
        hstx_dvi_write_char(*str++);
    }
}

void hstx_dvi_render_scanline(uint16_t line_y, uint8_t *line_buf) {
    if (!line_buf || line_y >= HSTX_DVI_HEIGHT) return;

    uint8_t row = line_y / FONT8X16_HEIGHT;
    uint8_t glyph_row = line_y % FONT8X16_HEIGHT;
    uint8_t attr = s_row_attr[row];
    uint8_t fg = attr & 0x0F;
    uint8_t bg = (attr >> 4) & 0x0F;

    for (uint8_t col = 0; col < HSTX_DVI_TEXT_COLS; col++) {
        char ch = s_text[row][col];
        const uint8_t *glyph = font8x16_get_glyph(ch);
        uint8_t row_bits = glyph[glyph_row];

        uint16_t pixel_base = col * FONT8X16_WIDTH;
        for (uint8_t b = 0; b < 8; b++) {
            bool on = (row_bits & (0x80 >> b)) != 0;
            line_buf[pixel_base + b] = on ? fg : bg;
        }
    }
}

void hstx_dvi_service(void) {
    if (!s_enabled) return;

    // Render active scanline into background ping-pong buffer
    uint8_t back_buf = s_active_buf ^ 1;
    hstx_dvi_render_scanline(s_scanline, s_line_buf[back_buf]);

#if defined(PICO_RP2350) && !defined(BP_HOST_TEST)
    if (s_dma_chan >= 0 && !dma_channel_is_busy(s_dma_chan)) {
        dma_channel_set_read_addr(s_dma_chan, s_line_buf[back_buf], true);
        s_active_buf = back_buf;
    }
#else
    s_active_buf = back_buf;
#endif

    s_scanline++;
    if (s_scanline >= HSTX_DVI_HEIGHT) {
        s_scanline = 0;
    }
}

void hstx_dvi_test_pattern(void) {
    hstx_dvi_clear();
    hstx_dvi_set_cursor(0, 0);

    hstx_dvi_set_color(HSTX_COLOR_YELLOW, HSTX_COLOR_BLUE);
    hstx_dvi_write_string("=== RB-Pirate RP2350 HSTX DVI Digital Video Generator (640x480@60Hz) ===\r\n");

    hstx_dvi_set_color(HSTX_COLOR_BRIGHT_CYAN, HSTX_COLOR_BLACK);
    hstx_dvi_write_string("Status: Online | VT100 Text Buffer: 80x30 Monospace | Dual-Core Synchronized\r\n\r\n");

    // Color bars
    hstx_dvi_write_string("Color Palette Verification:\r\n");
    for (uint8_t c = 0; c < 16; c++) {
        hstx_dvi_set_color(c ^ 0x0F, c);
        char bar[16];
        snprintf(bar, sizeof(bar), "  [%02X]  ", c);
        hstx_dvi_write_string(bar);
        if (c == 7) hstx_dvi_write_string("\r\n");
    }
    hstx_dvi_write_string("\r\n\r\n");

    hstx_dvi_set_color(HSTX_COLOR_BRIGHT_GREEN, HSTX_COLOR_BLACK);
    hstx_dvi_write_string("Ready for terminal console mirroring, logic analyzer waveforms, and LCDTAP.\r\n");
    hstx_dvi_set_color(HSTX_COLOR_BRIGHT_WHITE, HSTX_COLOR_BLACK);
}

const char* hstx_dvi_get_text_buffer(void) {
    return (const char*)s_text;
}
