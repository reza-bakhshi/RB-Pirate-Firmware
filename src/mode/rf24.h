/**
 * @file rf24.h
 * @brief nRF24L01 mode interface.
 *
 * @author Bus Pirate Project
 * @date 2026
 */

#ifndef BP_MODE_RF24_H
#define BP_MODE_RF24_H

#include <stdbool.h>
#include <stdint.h>

struct _mode_command_struct;
struct bp_command_def;

extern const struct _mode_command_struct rf24_commands[];
extern const uint32_t rf24_commands_count;
extern const struct bp_command_def rf24_setup_def;

uint32_t rf24_setup(void);
uint32_t rf24_setup_exc(void);
void rf24_cleanup(void);
void rf24_settings(void);
void rf24_help(void);

#endif
