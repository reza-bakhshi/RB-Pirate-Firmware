/**
 * @file cmd_glitch.c
 * @brief Global hardware fault injection (voltage crowbar and clock glitch) command
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "pirate.h"
#include "system_config.h"
#include "command_struct.h"
#include "ui/ui_term.h"
#include "ui/ui_help.h"
#include "lib/bp_args/bp_cmd.h"
#include "pirate/bio.h"
#include "pirate/button.h"
#include "usb_rx.h"
#include "commands/global/cmd_glitch.h"

#if !defined(BP_HOST_TEST)
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "platform/rb-pirate.h"

static void delay_ns(uint32_t ns) {
    if (ns == 0) return;
    uint32_t sys_hz = clock_get_hz(clk_sys);
    uint64_t cycles = ((uint64_t)ns * (uint64_t)sys_hz) / 1000000000ULL;
    if (cycles < 3) cycles = 3;
    busy_wait_at_least_cycles(cycles);
}
#endif

static const char* const usage[] = {
    "glitch [-p] [-c <pin>] [-t <trig>] [-d <delay_ns>] [-w <width_ns>] [-n <count>] [-m]",
    "Power rail crowbar pulse (100ns):%s glitch -p -w 100",
    "Logic glitch on BIO2 with 50ns delay on BIO0 trigger:%s glitch -c 2 -t 0 -d 50 -w 20",
};

static const bp_command_opt_t glitch_opts[] = {
    { "power",   'p', BP_ARG_NONE,     NULL, 0, NULL, "Target power rail override (voltage brownout/crowbar)" },
    { "pin",     'c', BP_ARG_REQUIRED, "pin", 0, NULL, "Target BIO pin (0..7) for clock/logic glitch" },
    { "trigger", 't', BP_ARG_REQUIRED, "trig", 0, NULL, "Trigger input BIO pin (0..7)" },
    { "delay",   'd', BP_ARG_REQUIRED, "ns", 0, NULL, "Delay after trigger in nanoseconds (default 0)" },
    { "width",   'w', BP_ARG_REQUIRED, "ns", 0, NULL, "Glitch pulse width in nanoseconds (default 100)" },
    { "count",   'n', BP_ARG_REQUIRED, "cnt", 0, NULL, "Number of glitch pulses (default 1)" },
    { "manual",  'm', BP_ARG_NONE,     NULL, 0, NULL, "Manual software trigger (fire immediately)" },
    { 0 }
};

const bp_command_def_t cmd_glitch_def = {
    .name        = "glitch",
    .description = 0x00,
    .opts        = glitch_opts,
    .usage       = usage,
    .usage_count = count_of(usage),
};

void cmd_glitch_handler(struct command_result* res) {
    if (bp_cmd_help_check(&cmd_glitch_def, res->help_flag)) {
        return;
    }

    bool is_power = bp_cmd_find_flag(&cmd_glitch_def, 'p');
    bool is_manual = bp_cmd_find_flag(&cmd_glitch_def, 'm');

    uint32_t target_pin = 0;
    bool has_target_pin = bp_cmd_get_uint32(&cmd_glitch_def, 'c', &target_pin);

    uint32_t trig_pin = 0;
    bool has_trigger = bp_cmd_get_uint32(&cmd_glitch_def, 't', &trig_pin);

    uint32_t delay_val_ns = 0;
    bp_cmd_get_uint32(&cmd_glitch_def, 'd', &delay_val_ns);

    uint32_t width_val_ns = 100;
    bp_cmd_get_uint32(&cmd_glitch_def, 'w', &width_val_ns);
    if (width_val_ns == 0) width_val_ns = 10;

    uint32_t count = 1;
    bp_cmd_get_uint32(&cmd_glitch_def, 'n', &count);
    if (count == 0) count = 1;

    if (!is_power && !has_target_pin) {
        printf("%sError: Specify either -p (power glitch) or -c <pin> (logic pin glitch)%s\r\n",
               ui_term_color_error(), ui_term_color_reset());
        res->error = true;
        return;
    }

    if (!is_manual && !has_trigger) {
        // Default to manual trigger if no trigger pin provided
        is_manual = true;
    }

    if (has_target_pin && target_pin >= BIO_MAX_PINS) {
        printf("%sError: Invalid target BIO pin %u%s\r\n",
               ui_term_color_error(), target_pin, ui_term_color_reset());
        res->error = true;
        return;
    }

    if (has_trigger && trig_pin >= BIO_MAX_PINS) {
        printf("%sError: Invalid trigger BIO pin %u%s\r\n",
               ui_term_color_error(), trig_pin, ui_term_color_reset());
        res->error = true;
        return;
    }

    printf("%sFault Injection Glitch Engine%s\r\n", ui_term_color_info(), ui_term_color_reset());
    if (is_power) {
        printf(" Target : PSU Override (CURRENT_EN_OVERRIDE / Vout crowbar)\r\n");
    } else {
        printf(" Target : BIO%u\r\n", target_pin);
    }
    printf(" Delay  : %u ns\r\n", delay_val_ns);
    printf(" Width  : %u ns\r\n", width_val_ns);
    printf(" Pulses : %u\r\n", count);
    if (is_manual) {
        printf(" Trigger: Manual (immediate)\r\n");
    } else {
        printf(" Trigger: Edge on BIO%u\r\n", trig_pin);
    }

#if !defined(BP_HOST_TEST)
    uint32_t glitch_gpio = 0;
    if (is_power) {
        glitch_gpio = CURRENT_EN_OVERRIDE;
        gpio_set_function(glitch_gpio, GPIO_FUNC_SIO);
        gpio_set_dir(glitch_gpio, GPIO_OUT);
        gpio_put(glitch_gpio, 0);
    } else {
        glitch_gpio = bio2bufiopin[target_pin];
        bio_buf_output(target_pin);
        gpio_set_function(glitch_gpio, GPIO_FUNC_SIO);
        gpio_set_dir(glitch_gpio, GPIO_OUT);
        gpio_put(glitch_gpio, 0);
    }

    uint32_t trig_gpio = 0;
    if (!is_manual) {
        trig_gpio = bio2bufiopin[trig_pin];
        bio_buf_input(trig_pin);
        gpio_set_function(trig_gpio, GPIO_FUNC_SIO);
        gpio_set_dir(trig_gpio, GPIO_IN);
    }

    for (uint32_t iter = 0; iter < count; iter++) {
        if (!is_manual) {
            bool initial = gpio_get(trig_gpio);
            // Wait for trigger transition
            while (gpio_get(trig_gpio) == initial) {
                char ch;
                if (rx_fifo_try_get(&ch) || button_get(0)) {
                    printf("%sGlitch sequence cancelled.%s\r\n", ui_term_color_notice(), ui_term_color_reset());
                    return;
                }
            }
        }

        // Critical timing section
        uint32_t ints = save_and_disable_interrupts();

        if (delay_val_ns > 0) {
            delay_ns(delay_val_ns);
        }

        // Assert glitch pulse
        gpio_put(glitch_gpio, 1);
        delay_ns(width_val_ns);
        gpio_put(glitch_gpio, 0);

        restore_interrupts(ints);

        if (count > 1) {
            busy_wait_us_32(100);
        }
    }

    if (is_power) {
        gpio_put(CURRENT_EN_OVERRIDE, 0);
    }
#endif

    printf("%sGlitch pulse(s) delivered.%s\r\n", ui_term_color_info(), ui_term_color_reset());
    res->error = false;
}
