/**
 * @file lcdtap.c
 * @brief LCDTap SPI/I2C display mirror and capture mode.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Bus Pirate project contributors
 */

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pirate.h"
#include "system_config.h"
#include "command_struct.h"
#include "pirate/bio.h"
#include "ui/ui_help.h"
#include "lib/bp_args/bp_cmd.h"
#include "mode/lcdtap.h"
#include "mode/lcdtap_backend.h"

static lcdtap_backend_config_t mode_config;

static const char *const bus_names[] = { "I2C", "4-wire SPI", "3-wire SPI" };
static const char *const controller_names[] = {
    "ST7789", "SSD1306", "SSD1331", "ILI9341", "ST7032", "KS0108"
};

static const bp_val_choice_t bus_choices[] = {
    { "i2c", NULL, 0, LCDTAP_BUS_I2C },
    { "spi4", "4spi", 0, LCDTAP_BUS_SPI4 },
    { "spi3", "3spi", 0, LCDTAP_BUS_SPI3 },
};
static const bp_val_constraint_t bus_choice = {
    .type = BP_VAL_CHOICE,
    .choice = { .choices = bus_choices, .count = count_of(bus_choices), .def = LCDTAP_BUS_SPI4 },
};

static const bp_val_choice_t controller_choices[] = {
    { "st7789", NULL, 0, LCDTAP_CTRL_ST7789 },
    { "ssd1306", NULL, 0, LCDTAP_CTRL_SSD1306 },
    { "ssd1331", NULL, 0, LCDTAP_CTRL_SSD1331 },
    { "ili9341", NULL, 0, LCDTAP_CTRL_ILI9341 },
    { "st7032", NULL, 0, LCDTAP_CTRL_ST7032 },
};
static const bp_val_constraint_t controller_choice = {
    .type = BP_VAL_CHOICE,
    .choice = { .choices = controller_choices, .count = count_of(controller_choices), .def = LCDTAP_CTRL_ST7789 },
};

static const bp_val_constraint_t width_range = {
    .type = BP_VAL_UINT32, .u = { .min = 8, .max = 480, .def = 240 },
};
static const bp_val_constraint_t height_range = {
    .type = BP_VAL_UINT32, .u = { .min = 1, .max = 480, .def = 320 },
};
static const bp_val_constraint_t address_range = {
    .type = BP_VAL_UINT32, .u = { .min = 0x08, .max = 0x77, .def = 0x3c },
};
static const bp_command_opt_t setup_opts[] = {
    { "bus", 'b', BP_ARG_REQUIRED, "i2c|spi4|spi3", T_HELP_LCDTAP_BUS, &bus_choice },
    { "controller", 'c', BP_ARG_REQUIRED, "st7789|ssd1306|ssd1331|ili9341|st7032",
      T_HELP_LCDTAP_CONTROLLER,
      &controller_choice },
    { "width", 'x', BP_ARG_REQUIRED, "8-480", T_HELP_LCDTAP_WIDTH, &width_range },
    { "height", 'y', BP_ARG_REQUIRED, "1-480", T_HELP_LCDTAP_HEIGHT, &height_range },
    { "address", 'a', BP_ARG_REQUIRED, "0x08-0x77", T_HELP_LCDTAP_ADDRESS, &address_range },
    { 0 },
};

static const char *const setup_usage[] = {
    "m lcdtap [-b bus] [-c controller] [-x width] [-y height] [-a i2c_address]",
    "Capture a 240x320 ST7789 over 4-wire SPI:%s m lcdtap -b spi4 -c st7789 -x 240 -y 320",
    "Capture a 128x64 SSD1306 over I2C:%s m lcdtap -b i2c -c ssd1306 -x 128 -y 64 -a 0x3c",
};
const bp_command_def_t lcdtap_setup_def = {
    .name = "lcdtap", .description = T_HELP_LCDTAP, .opts = setup_opts,
    .usage = setup_usage, .usage_count = count_of(setup_usage),
};

static const char *const status_usage[] = { "status" };
static const char *const dump_usage[] = { "cmddump" };
static const char *const reset_usage[] = { "reset" };
static const bp_command_def_t status_def = {
    .name = "status", .description = T_HELP_LCDTAP_STATUS,
    .usage = status_usage, .usage_count = count_of(status_usage),
};
static const bp_command_def_t dump_def = {
    .name = "cmddump", .description = T_HELP_LCDTAP_CMDDUMP,
    .usage = dump_usage, .usage_count = count_of(dump_usage),
};
static const bp_command_def_t reset_def = {
    .name = "reset", .description = T_HELP_LCDTAP_RESET,
    .usage = reset_usage, .usage_count = count_of(reset_usage),
};
static const bp_command_positional_t save_positionals[] = {
    { "file", "output BMP file", T_HELP_LCDTAP_SAVE_FILE, true }, { 0 }
};
static const char *const save_usage[] = {
    "save <file.bmp>", "Save the reconstructed framebuffer:%s save screen.bmp"
};
static const bp_command_def_t save_def = {
    .name = "save", .description = T_HELP_LCDTAP_SAVE,
    .positionals = save_positionals, .positional_count = 1,
    .usage = save_usage, .usage_count = count_of(save_usage),
};

static void status_handler(struct command_result *res) {
    if (!bp_cmd_help_check(&status_def, res->help_flag)) lcdtap_backend_status();
}

static void dump_handler(struct command_result *res) {
    if (!bp_cmd_help_check(&dump_def, res->help_flag)) lcdtap_backend_dump();
}

static void reset_handler(struct command_result *res) {
    if (bp_cmd_help_check(&reset_def, res->help_flag)) return;
    lcdtap_backend_reset();
    printf("LCD controller model reset.\r\n");
}

static void save_handler(struct command_result *res) {
    if (bp_cmd_help_check(&save_def, res->help_flag)) return;
    char filename[64] = { 0 };
    if (!bp_cmd_get_positional_string(&save_def, 1, filename, sizeof(filename))) {
        bp_cmd_help_show(&save_def);
        return;
    }
    (void)lcdtap_backend_save_bmp(filename);
}

const struct _mode_command_struct lcdtap_commands[] = {
    { .func = status_handler, .def = &status_def, .supress_fala_capture = true },
    { .func = save_handler, .def = &save_def, .supress_fala_capture = true },
    { .func = dump_handler, .def = &dump_def, .supress_fala_capture = true },
    { .func = reset_handler, .def = &reset_def, .supress_fala_capture = true },
};
const uint32_t lcdtap_commands_count = count_of(lcdtap_commands);

uint32_t lcdtap_setup(void) {
    if (bp_cmd_flag(&lcdtap_setup_def, 'b', &mode_config.bus) == BP_CMD_INVALID ||
        bp_cmd_flag(&lcdtap_setup_def, 'c', &mode_config.controller) == BP_CMD_INVALID ||
        bp_cmd_flag(&lcdtap_setup_def, 'x', &mode_config.width) == BP_CMD_INVALID ||
        bp_cmd_flag(&lcdtap_setup_def, 'y', &mode_config.height) == BP_CMD_INVALID ||
        bp_cmd_flag(&lcdtap_setup_def, 'a', &mode_config.i2c_address) == BP_CMD_INVALID) {
        return 0;
    }

    if (mode_config.controller == LCDTAP_CTRL_ST7032) {
        if (mode_config.width < 2 || mode_config.width > 40 ||
            (mode_config.height != 1 && mode_config.height != 2 && mode_config.height != 4)) {
            printf("ST7032 geometry must be 2-40 columns and 1, 2, or 4 rows.\r\n");
            return 0;
        }
    }
    return 1;
}

uint32_t lcdtap_setup_exc(void) {
    uint8_t last_pin = mode_config.bus == LCDTAP_BUS_I2C ? BIO1 : BIO4;
    static const char labels[][5] = { "CS", "CLK", "MOSI", "DC", "RST" };
    static const char i2c_labels[][5] = { "SDA", "SCL" };
    for (uint8_t pin = BIO0; pin <= last_pin; pin++) {
        bio_input(pin);
        const char *label = mode_config.bus == LCDTAP_BUS_I2C ? i2c_labels[pin] : labels[pin];
        system_bio_update_purpose_and_label(true, pin, BP_PIN_MODE, label);
    }

    if (!lcdtap_backend_start(&mode_config)) {
        lcdtap_cleanup();
        return 0;
    }
    printf("LCDTap capture active. Use 'status', 'cmddump', and 'save <file.bmp>'.\r\n");
    lcdtap_settings();
    return 1;
}

void lcdtap_cleanup(void) {
    lcdtap_backend_stop();
    for (uint8_t pin = BIO0; pin <= BIO4; pin++) {
        system_bio_update_purpose_and_label(false, pin, BP_PIN_MODE, 0);
    }
    bio_init();
}

void lcdtap_periodic(void) { lcdtap_backend_process(); }

void lcdtap_macro(uint32_t macro) {
    if (macro == 0) {
        printf(" 0. This menu\r\n 1. Show capture status\r\n 2. Reset controller model\r\n");
    } else if (macro == 1) {
        lcdtap_backend_status();
    } else if (macro == 2) {
        lcdtap_backend_reset();
    } else {
        printf("%s\r\n", GET_T(T_MODE_ERROR_MACRO_NOT_DEFINED));
    }
}

void lcdtap_settings(void) {
    ui_help_setting_string("Input bus", bus_names[mode_config.bus], 0);
    ui_help_setting_string("LCD controller", controller_names[mode_config.controller], 0);
    ui_help_setting_int(mode_config.controller == LCDTAP_CTRL_ST7032 ? "Text columns" : "Framebuffer width",
                        mode_config.width, mode_config.controller == LCDTAP_CTRL_ST7032 ? 0 : "px");
    ui_help_setting_int(mode_config.controller == LCDTAP_CTRL_ST7032 ? "Text rows" : "Framebuffer height",
                        mode_config.height, mode_config.controller == LCDTAP_CTRL_ST7032 ? 0 : "px");
    if (mode_config.bus == LCDTAP_BUS_I2C) {
        printf("I2C address: 0x%02lx\r\n", (unsigned long)mode_config.i2c_address);
    }
}

void lcdtap_help(void) {
    printf("LCDTap captures display traffic and reconstructs an RGB565 framebuffer.\r\n\r\n");
    printf("Wiring:\r\n");
    printf("  SPI4  IO0=CS  IO1=CLK  IO2=MOSI  IO3=D/C  IO4=RST\r\n");
    printf("  SPI3  IO0=CS  IO1=CLK  IO2=9-bit MOSI       IO4=RST\r\n");
    printf("  I2C   IO0=SDA IO1=SCL (joins the bus as a slave and generates ACKs)\r\n");
    printf("  Always connect GND and configure Vout/Vref for the target logic voltage.\r\n\r\n");
    printf("Controllers: ST7789/ST7735, SSD1306, SSD1331, ILI9341/9342/9488, ST7032.\r\n");
    printf("SPI capture requires mode 0 and MSB-first data. Keep all tap wires short.\r\n");
    printf("Parallel8/KS0108 and DVI output are unavailable; use 'save' for BMP output.\r\n");
    printf("Framebuffer RAM use is width x height x 2 bytes; allocation failure cancels setup.\r\n\r\n");
    printf("Examples:\r\n");
    printf("  m lcdtap -b spi4 -c ili9341 -x 320 -y 240\r\n");
    printf("  m lcdtap -b i2c -c ssd1306 -x 128 -y 64 -a 0x3c\r\n");
    printf("  status              verify traffic and check dropped bytes\r\n");
    printf("  cmddump             inspect captured controller commands\r\n");
    printf("  save screen.bmp     export the framebuffer to the SD card\r\n");
    ui_help_mode_commands(lcdtap_commands, lcdtap_commands_count);
}

uint32_t lcdtap_get_speed(void) { return 0; }
bool lcdtap_preflight_sanity_check(void) { return true; }
