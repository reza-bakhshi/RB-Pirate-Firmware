/**
 * @file lcd.c
 * @brief ST7789 display driver with asynchronous DMA framebuffer flushing.
 *
 * Implements non-blocking DMA transmission of pixel data to an ST7789 display
 * controller over SPI. Operates under the single static memory model with zero
 * runtime heap allocation. Leases a dedicated DMA channel under BP_RES_OWNER_SYSTEM.
 *
 * @author Bus Pirate Project
 * @date 2026
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "pirate/lcd.h"
#include "kernel/bp_resource.h"
#include "pico/stdlib.h"
/* Modified by Codex: bounded DMA completion and checked pixel transfers. */

#if !defined(BP_HOST_TEST)
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "pirate.h"
#endif

/* Leased DMA channel and status state */
static int32_t s_lcd_dma_ch = -1;
static volatile bool s_lcd_busy = false;
static bool s_lcd_ready = false;

/* Active drawing window coordinates */
static uint16_t s_window_x0 = 0;
static uint16_t s_window_y0 = 0;
static uint16_t s_window_x1 = BP_LCD_WIDTH - 1;
static uint16_t s_window_y1 = BP_LCD_HEIGHT - 1;

int32_t bp_lcd_get_dma_channel(void) {
    return s_lcd_dma_ch;
}

bool bp_lcd_is_ready(void) {
    return s_lcd_ready;
}

bool bp_lcd_is_busy(void) {
    if (!s_lcd_busy) {
        return false;
    }
#if !defined(BP_HOST_TEST)
    if (s_lcd_dma_ch >= 0 && !dma_channel_is_busy((uint)s_lcd_dma_ch)) {
        s_lcd_busy = false;
    }
#endif
    return s_lcd_busy;
}

bool bp_lcd_wait_idle_timeout(uint32_t timeout_us) {
    uint32_t start = time_us_32();
    for (uint32_t attempts = 0; attempts < 100000; ++attempts) {
        if (!bp_lcd_is_busy()) return true;
        if ((uint32_t)(time_us_32() - start) >= timeout_us) return false;
        tight_loop_contents();
    }
    /* Keep busy set: the caller must retain its buffer until cleanup/completion. */
    return false;
}

bool bp_lcd_wait_idle(void) {
    return bp_lcd_wait_idle_timeout(100000);
}

bool bp_lcd_init(void) {
    if (s_lcd_ready) {
        return true;
    }

    uint32_t ch = 0;
    if (bp_res_claim_unused_dma_ch(BP_RES_OWNER_SYSTEM, "lcd_tx", &ch) != BP_STATUS_OK) {
        return false;
    }

    s_lcd_dma_ch = (int32_t)ch;
    s_lcd_ready = true;
    s_lcd_busy = false;

    s_window_x0 = 0;
    s_window_y0 = 0;
    s_window_x1 = BP_LCD_WIDTH - 1;
    s_window_y1 = BP_LCD_HEIGHT - 1;

    return true;
}

void bp_lcd_cleanup(void) {
    if (s_lcd_busy) {
#if !defined(BP_HOST_TEST)
        if (s_lcd_dma_ch >= 0) {
            dma_channel_abort((uint)s_lcd_dma_ch);
        }
#endif
        s_lcd_busy = false;
    }

    if (s_lcd_dma_ch >= 0) {
        bp_res_release(BP_RES_CLASS_DMA_CH, (uint32_t)s_lcd_dma_ch, BP_RES_OWNER_SYSTEM);
        s_lcd_dma_ch = -1;
    }

    s_lcd_ready = false;
}

void bp_lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    if (!s_lcd_ready || bp_lcd_is_busy() || x0 > x1 || y0 > y1) return;
    if (x0 >= BP_LCD_WIDTH) x0 = BP_LCD_WIDTH - 1;
    if (x1 >= BP_LCD_WIDTH) x1 = BP_LCD_WIDTH - 1;
    if (y0 >= BP_LCD_HEIGHT) y0 = BP_LCD_HEIGHT - 1;
    if (y1 >= BP_LCD_HEIGHT) y1 = BP_LCD_HEIGHT - 1;

    s_window_x0 = x0;
    s_window_y0 = y0;
    s_window_x1 = x1;
    s_window_y1 = y1;

#if !defined(BP_HOST_TEST)
    /* Hardware window configuration over SPI */
    uint8_t caset_cmd[5] = {
        ST7789_CASET,
        (uint8_t)(x0 >> 8), (uint8_t)(x0 & 0xFF),
        (uint8_t)(x1 >> 8), (uint8_t)(x1 & 0xFF)
    };
    uint8_t raset_cmd[5] = {
        ST7789_RASET,
        (uint8_t)(y0 >> 8), (uint8_t)(y0 & 0xFF),
        (uint8_t)(y1 >> 8), (uint8_t)(y1 & 0xFF)
    };
    uint8_t ramwr_cmd = ST7789_RAMWR;

    spi_write_blocking(BP_SPI_PORT, caset_cmd, sizeof(caset_cmd));
    spi_write_blocking(BP_SPI_PORT, raset_cmd, sizeof(raset_cmd));
    spi_write_blocking(BP_SPI_PORT, &ramwr_cmd, 1);
#endif
}

bool bp_lcd_flush_async(const uint16_t* pixels, size_t count) {
    if (!s_lcd_ready || !pixels || count == 0 || ((uintptr_t)pixels & 1u) != 0 ||
        count > (size_t)(s_window_x1 - s_window_x0 + 1) * (s_window_y1 - s_window_y0 + 1)) {
        return false;
    }

    if (bp_lcd_is_busy()) {
        return false;
    }

    s_lcd_busy = true;

#if !defined(BP_HOST_TEST)
    if (s_lcd_dma_ch >= 0) {
        dma_channel_config c = dma_channel_get_default_config((uint)s_lcd_dma_ch);
        channel_config_set_transfer_data_size(&c, DMA_SIZE_16);
        channel_config_set_dreq(&c, spi_get_dreq(BP_SPI_PORT, true));
        channel_config_set_read_increment(&c, true);
        channel_config_set_write_increment(&c, false);

        dma_channel_configure(
            (uint)s_lcd_dma_ch,
            &c,
            &spi_get_hw(BP_SPI_PORT)->dr,
            pixels,
            count,
            true /* start immediately */
        );
    }
#endif

    return true;
}

#ifdef BP_HOST_TEST
void bp_lcd_test_step_dma(void) {
    s_lcd_busy = false;
}
#endif
