/**
 * @file ir_devicebgone.h
 * @brief INFRARED mode `devicebgone` command - blast power-off sequences.
 */

#ifndef _IR_DEVICEBGONE_H
#define _IR_DEVICEBGONE_H

void ir_devicebgone_handler(struct command_result *res);
extern const struct bp_command_def ir_devicebgone_def;

#endif
