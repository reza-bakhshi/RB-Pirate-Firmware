/**
 * @file ir_send.h
 * @brief INFRARED mode `send` command - transmit a protocol command frame.
 */

#ifndef _IR_SEND_H
#define _IR_SEND_H

void ir_send_handler(struct command_result *res);
extern const struct bp_command_def ir_send_def;

#endif
