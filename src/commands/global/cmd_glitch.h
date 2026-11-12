/**
 * @file cmd_glitch.h
 * @brief Global hardware fault injection (voltage crowbar and clock glitch) command
 */

#ifndef BP_CMD_GLITCH_H
#define BP_CMD_GLITCH_H

#include "command_struct.h"

extern const struct bp_command_def cmd_glitch_def;

void cmd_glitch_handler(struct command_result* res);

#endif // BP_CMD_GLITCH_H
