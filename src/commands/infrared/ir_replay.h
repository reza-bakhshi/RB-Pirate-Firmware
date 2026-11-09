/**
 * @file ir_replay.h
 * @brief INFRARED mode `replay` command - record and replay IR frames.
 */

#ifndef _IR_REPLAY_H
#define _IR_REPLAY_H

void ir_replay_handler(struct command_result *res);
extern const struct bp_command_def ir_replay_def;

#endif
