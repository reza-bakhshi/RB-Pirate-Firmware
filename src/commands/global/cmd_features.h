#ifndef CMD_FEATURES_H
#define CMD_FEATURES_H

#include <stdbool.h>
#include <stdint.h>
#include "command_struct.h"

extern const bp_command_def_t cmd_features_def;
void cmd_features_handler(struct command_result* res);

#endif // CMD_FEATURES_H
