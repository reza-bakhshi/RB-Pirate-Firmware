#ifndef _CMD_RAND_H
#define _CMD_RAND_H

#include "lib/bp_args/bp_cmd.h"
#include "command_struct.h"

extern const bp_command_def_t cmd_rand_def;
void cmd_rand_handler(struct command_result* res);

#endif // _CMD_RAND_H
