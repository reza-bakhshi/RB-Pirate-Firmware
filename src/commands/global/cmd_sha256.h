#ifndef _CMD_SHA256_H
#define _CMD_SHA256_H

#include "lib/bp_args/bp_cmd.h"
#include "command_struct.h"

extern const bp_command_def_t cmd_sha256_def;
void cmd_sha256_handler(struct command_result* res);

#endif // _CMD_SHA256_H
