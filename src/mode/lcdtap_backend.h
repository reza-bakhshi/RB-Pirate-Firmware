/**
 * @file lcdtap_backend.h
 * @brief C interface to the C++ LcdTap capture engine.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Bus Pirate project contributors
 */
#ifndef LCDTAP_BACKEND_H
#define LCDTAP_BACKEND_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum lcdtap_bus {
    LCDTAP_BUS_I2C = 0,
    LCDTAP_BUS_SPI4,
    LCDTAP_BUS_SPI3,
};

enum lcdtap_controller {
    LCDTAP_CTRL_ST7789 = 0,
    LCDTAP_CTRL_SSD1306,
    LCDTAP_CTRL_SSD1331,
    LCDTAP_CTRL_ILI9341,
    LCDTAP_CTRL_ST7032,
    LCDTAP_CTRL_KS0108,
};

typedef struct lcdtap_backend_config {
    uint32_t bus;
    uint32_t controller;
    uint32_t width;
    uint32_t height;
    uint32_t i2c_address;
} lcdtap_backend_config_t;

bool lcdtap_backend_start(const lcdtap_backend_config_t *config);
void lcdtap_backend_stop(void);
void lcdtap_backend_process(void);
void lcdtap_backend_reset(void);
void lcdtap_backend_status(void);
void lcdtap_backend_dump(void);
bool lcdtap_backend_save_bmp(const char *filename);

#ifdef __cplusplus
}
#endif

#endif
