/**
 * @file lcdtap.h
 * @brief LCD bus capture mode for RB-Pirate.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Bus Pirate project contributors
 */
#ifndef MODE_LCDTAP_H
#define MODE_LCDTAP_H

#include <stdbool.h>
#include <stdint.h>

struct bp_command_def;
struct _mode_command_struct;

uint32_t lcdtap_setup(void);
uint32_t lcdtap_setup_exc(void);
void lcdtap_cleanup(void);
void lcdtap_periodic(void);
void lcdtap_macro(uint32_t macro);
void lcdtap_settings(void);
void lcdtap_help(void);
uint32_t lcdtap_get_speed(void);
bool lcdtap_preflight_sanity_check(void);

extern const struct bp_command_def lcdtap_setup_def;
extern const struct _mode_command_struct lcdtap_commands[];
extern const uint32_t lcdtap_commands_count;

#endif
