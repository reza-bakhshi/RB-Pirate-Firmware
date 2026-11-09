/**
 * @file ir_remote.h
 * @brief INFRARED mode `remote` command - universal remote shell.
 */

#ifndef _IR_REMOTE_H
#define _IR_REMOTE_H

void ir_remote_handler(struct command_result *res);
extern const struct bp_command_def ir_remote_def;

#endif
