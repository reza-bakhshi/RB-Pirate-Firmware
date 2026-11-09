/**
 * @file ir_jam.h
 * @brief INFRARED mode `jam` command - transmit IR noise bursts.
 */

#ifndef _IR_JAM_H
#define _IR_JAM_H

void ir_jam_handler(struct command_result *res);
extern const struct bp_command_def ir_jam_def;

#endif
