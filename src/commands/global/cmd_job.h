#ifndef BP_CMD_JOB_H
#define BP_CMD_JOB_H

#include "command_struct.h"
#include "lib/bp_args/bp_cmd.h"

extern const bp_command_def_t cmd_job_def;

void cmd_job_handler(struct command_result* res);

#endif // BP_CMD_JOB_H
