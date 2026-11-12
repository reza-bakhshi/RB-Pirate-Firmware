/**
 * @file lcd.h
 * @brief ST7789 display driver with asynchronous DMA framebuffer flushing.
 *
 * Provides non-blocking DMA transmission of framebuffer chunks to an
 * ST7789 SPI color LCD panel, enabling high-speed refresh with zero CPU
 * spinloops during pixel transmission.
 *
 * @author Bus Pirate Project
 * @date 2026
 */

#ifndef PIRATE_LCD_H
#define PIRATE_LCD_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BP_LCD_WIDTH        240
#define BP_LCD_HEIGHT       320

/* ST7789 standard command registers */
#define ST7789_NOP          0x00
#define ST7789_SWRESET      0x01
#define ST7789_SLPIN        0x10
#define ST7789_SLPOUT       0x11
#define ST7789_NORON        0x13
#define ST7789_INVOFF       0x20
#define ST7789_INVON        0x21
#define ST7789_DISPOFF      0x28
#define ST7789_DISPON       0x29
#define ST7789_CASET        0x2A
#define ST7789_RASET        0x2B
#define ST7789_RAMWR        0x2C
#define ST7789_MADCTL       0x36
#define ST7789_COLMOD       0x3A

/**
 * @brief Initialize ST7789 display controller and claim DMA channel under BP_RES_OWNER_SYSTEM.
 * @return true on success, false if hardware lease acquisition fails.
 */
bool bp_lcd_init(void);

/**
 * @brief Deinitialize display and release claimed DMA channel.
 */
void bp_lcd_cleanup(void);

/**
 * @brief Check if LCD subsystem is initialized and DMA channel is claimed.
 */
bool bp_lcd_is_ready(void);

/**
 * @brief Check if a DMA framebuffer flush is currently in progress.
 * @return true if DMA is transmitting pixels, false if idle.
 */
bool bp_lcd_is_busy(void);

/**
 * @brief Block until any in-flight DMA framebuffer flush completes.
 */
void bp_lcd_wait_idle(void);

/**
 * @brief Configure active window coordinates (CASET / RASET).
 * @param x0 Start column (0..BP_LCD_WIDTH - 1)
 * @param y0 Start row (0..BP_LCD_HEIGHT - 1)
 * @param x1 End column (inclusive)
 * @param y1 End row (inclusive)
 */
void bp_lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);

/**
 * @brief Start non-blocking DMA transmission of pixel data to the display.
 *
 * Initiates DMA transfer from the provided pixel buffer to the SPI TX FIFO.
 * Returns immediately with `bp_lcd_is_busy() == true`.
 *
 * @param pixels Array of 16-bit RGB565 pixels (in big-endian / network format)
 * @param count Number of 16-bit pixels to transfer
 * @return true if transfer was successfully started, false if busy or not ready
 */
bool bp_lcd_flush_async(const uint16_t* pixels, size_t count);

/**
 * @brief Query claimed DMA channel number.
 * @return DMA channel (0..15), or -1 if unassigned.
 */
int32_t bp_lcd_get_dma_channel(void);

#ifdef BP_HOST_TEST
/**
 * @brief Host test helper to advance / complete an in-flight simulated DMA transfer.
 */
void bp_lcd_test_step_dma(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PIRATE_LCD_H */
