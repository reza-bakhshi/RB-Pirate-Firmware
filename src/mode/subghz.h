#ifndef BP_MODE_SUBGHZ_H
#define BP_MODE_SUBGHZ_H

#include <stdbool.h>
#include <stdint.h>

struct _mode_command_struct;
struct bp_command_def;

extern const struct _mode_command_struct subghz_commands[];
extern const uint32_t subghz_commands_count;
extern const struct bp_command_def subghz_setup_def;

uint32_t subghz_setup(void);
uint32_t subghz_setup_exc(void);
void subghz_cleanup(void);
void subghz_settings(void);
void subghz_help(void);
bool subghz_preflight_sanity_check(void);

#endif
