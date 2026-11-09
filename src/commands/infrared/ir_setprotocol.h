/**
 * @file ir_setprotocol.h
 * @brief INFRARED mode `setprotocol` command - select the IR protocol.
 */

#ifndef _IR_SETPROTOCOL_H
#define _IR_SETPROTOCOL_H

void ir_setprotocol_handler(struct command_result *res);
extern const struct bp_command_def ir_setprotocol_def;

#endif
