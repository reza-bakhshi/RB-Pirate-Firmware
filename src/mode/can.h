/**
 * @file can.h
 * @brief Controller Area Network (CAN) protocol mode for Bus Pirate
 */

#ifndef BP_MODE_CAN_H
#define BP_MODE_CAN_H

#include <stdint.h>
#include <stdbool.h>
#include "command_struct.h"

struct _bytecode;

void can_start(struct _bytecode* result, struct _bytecode* next);
void can_start_alt(struct _bytecode* result, struct _bytecode* next);
void can_stop(struct _bytecode* result, struct _bytecode* next);
void can_stop_alt(struct _bytecode* result, struct _bytecode* next);
void can_write(struct _bytecode* result, struct _bytecode* next);
void can_read(struct _bytecode* result, struct _bytecode* next);
void can_clkh(struct _bytecode* result, struct _bytecode* next);
void can_clkl(struct _bytecode* result, struct _bytecode* next);
void can_dath(struct _bytecode* result, struct _bytecode* next);
void can_datl(struct _bytecode* result, struct _bytecode* next);
void can_dats(struct _bytecode* result, struct _bytecode* next);
void can_tick_clock(struct _bytecode* result, struct _bytecode* next);
void can_bitr(struct _bytecode* result, struct _bytecode* next);
void can_periodic(void);
void can_macro(uint32_t macro);
uint32_t can_setup(void);
uint32_t can_setup_exc(void);
void can_cleanup(void);
void can_settings(void);
void can_help(void);
uint32_t can_get_speed(void);
bool can_preflight_sanity_check(void);

extern const struct _mode_command_struct can_commands[];
extern const uint32_t can_commands_count;
extern const struct bp_command_def can_setup_def;

#endif // BP_MODE_CAN_H
