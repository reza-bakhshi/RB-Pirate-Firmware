/**
 * @file ir_config.h
 * @brief INFRARED mode `config` command - configure IR TX and RX pins.
 */

#ifndef _IR_CONFIG_H
#define _IR_CONFIG_H

void ir_config_handler(struct command_result *res);
extern const struct bp_command_def ir_config_def;

#endif
