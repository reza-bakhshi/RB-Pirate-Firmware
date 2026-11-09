/**
 * @file ir_receive.h
 * @brief INFRARED mode `receive` command - listen and decode IR signals.
 */

#ifndef _IR_RECEIVE_H
#define _IR_RECEIVE_H

void ir_receive_handler(struct command_result *res);
extern const struct bp_command_def ir_receive_def;

#endif
