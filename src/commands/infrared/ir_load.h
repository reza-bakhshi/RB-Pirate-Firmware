/**
 * @file ir_load.h
 * @brief INFRARED mode `load` command - load and send .ir remote files.
 */

#ifndef _IR_LOAD_H
#define _IR_LOAD_H

void ir_load_handler(struct command_result *res);
extern const struct bp_command_def ir_load_def;

#endif
