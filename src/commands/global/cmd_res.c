#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "pirate.h"
#include "system_config.h"
#include "command_struct.h"
#include "ui/ui_term.h"
#include "ui/ui_help.h"
#include "lib/bp_args/bp_cmd.h"
#include "kernel/bp_status.h"
#include "kernel/bp_resource.h"
#include "commands/global/cmd_res.h"

enum res_actions {
    RES_ACTION_STATUS = 1,
    RES_ACTION_CLEAN,
};

static const bp_command_action_t res_action_defs[] = {
    { RES_ACTION_STATUS, "status", 0 },
    { RES_ACTION_CLEAN,  "clean",  0 },
};

static const char* const usage[] = {
    "res [status|clean] [-s] [-c]",
    "Show active hardware resource leases (default):%s res",
    "Show active hardware resource leases:%s res status",
    "Release all leases held by the active mode:%s res clean",
};

static const bp_command_opt_t res_opts[] = {
    { "status", 's', BP_ARG_NONE, NULL, 0 },
    { "clean",  'c', BP_ARG_NONE, NULL, 0 },
    { 0 }
};

const bp_command_def_t cmd_res_def = {
    .name         = "res",
    .description  = T_CMDLN_RES,
    .actions      = res_action_defs,
    .action_count = count_of(res_action_defs),
    .opts         = res_opts,
    .usage        = usage,
    .usage_count  = count_of(usage),
};

void cmd_res_handler(struct command_result* res) {
    if (bp_cmd_help_check(&cmd_res_def, res->help_flag)) {
        return;
    }

    uint32_t action = 0;
    bp_cmd_get_action(&cmd_res_def, &action);

    if (action == RES_ACTION_CLEAN || bp_cmd_find_flag(&cmd_res_def, 'c')) {
        uint32_t owner = BP_RES_OWNER_MODE_BASE + system_config.mode;
        uint32_t count = bp_res_release_all(owner);
        printf("Cleaned up and released %lu resource lease(s) for active mode (%lu).\r\n",
               (unsigned long)count, (unsigned long)owner);
        return;
    }

    bp_res_print_table();
}
