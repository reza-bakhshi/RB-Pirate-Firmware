#ifndef CMD_RES_H
#define CMD_RES_H

#include "lib/bp_args/bp_cmd.h"
#include "command_struct.h"

extern const bp_command_def_t cmd_res_def;

void cmd_res_handler(struct command_result* res);

#endif /* CMD_RES_H */
