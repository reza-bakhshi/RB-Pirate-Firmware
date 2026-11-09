#ifndef BP_HEALTH_H
#define BP_HEALTH_H

#include "command_struct.h"
#include "lib/bp_args/bp_cmd.h"

extern const bp_command_def_t health_def;

void health_handler(struct command_result* res);

#endif
