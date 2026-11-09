#ifndef BP_FAULT_H
#define BP_FAULT_H

#include "command_struct.h"
#include "lib/bp_args/bp_cmd.h"

extern const bp_command_def_t fault_def;

void fault_handler(struct command_result* res);

#endif
