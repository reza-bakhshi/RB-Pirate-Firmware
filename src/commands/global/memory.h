#ifndef BP_MEMORY_H
#define BP_MEMORY_H

#include "command_struct.h"
#include "lib/bp_args/bp_cmd.h"

extern const bp_command_def_t memory_def;

void memory_handler(struct command_result* res);

#endif
