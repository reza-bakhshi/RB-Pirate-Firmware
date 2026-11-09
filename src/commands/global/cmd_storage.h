#ifndef CMD_STORAGE_H
#define CMD_STORAGE_H

#include "lib/bp_args/bp_cmd.h"
#include "command_struct.h"

extern const bp_command_def_t cmd_storage_def;

void cmd_storage_handler(struct command_result* res);

#endif /* CMD_STORAGE_H */
