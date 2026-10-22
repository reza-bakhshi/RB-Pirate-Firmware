/**
 * @file rfid.h
 * @brief RFID / NFC mode interface for Bus Pirate
 * @author Bus Pirate Project
 * @date 2026
 */

#ifndef BP_MODE_RFID_H
#define BP_MODE_RFID_H

#include <stdbool.h>
#include <stdint.h>

struct _mode_command_struct;
struct bp_command_def;

extern const struct _mode_command_struct rfid_commands[];
extern const uint32_t rfid_commands_count;
extern const struct bp_command_def rfid_setup_def;

uint32_t rfid_setup(void);
uint32_t rfid_setup_exc(void);
void rfid_cleanup(void);
void rfid_settings(void);
void rfid_help(void);
bool rfid_preflight_sanity_check(void);

#endif // BP_MODE_RFID_H
