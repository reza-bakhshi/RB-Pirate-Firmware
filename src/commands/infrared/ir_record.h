/**
 * @file ir_record.h
 * @brief INFRARED mode `record` command - record IR signals to a .ir file.
 */

#ifndef _IR_RECORD_H
#define _IR_RECORD_H

void ir_record_handler(struct command_result *res);
extern const struct bp_command_def ir_record_def;

#endif
