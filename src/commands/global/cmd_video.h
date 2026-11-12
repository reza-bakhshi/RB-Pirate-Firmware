/**
 * @file cmd_video.h
 * @brief CLI command interface for RP2350 HSTX DVI digital video output.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "command_struct.h"
#include "lib/bp_args/bp_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const bp_command_def_t cmd_video_def;

void cmd_video_handler(struct command_result* res);

#ifdef __cplusplus
}
#endif
