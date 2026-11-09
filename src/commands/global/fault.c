#include <stdio.h>
#include "pico/stdlib.h"
#include "pirate.h"
#include "system_config.h"
#include "command_struct.h"
#include "pirate/crash_record.h"
#include "pirate/watchdog.h"
#include "ui/ui_term.h"
#include "ui/ui_help.h"
#include "lib/bp_args/bp_cmd.h"
#include "commands/global/fault.h"

enum fault_actions {
    FAULT_ACTION_STATUS = 1,
    FAULT_ACTION_CLEAR,
    FAULT_ACTION_CRASH,
};

static const bp_command_action_t fault_action_defs[] = {
    { FAULT_ACTION_STATUS, "status", 0 },
    { FAULT_ACTION_CLEAR,  "clear",  0 },
    { FAULT_ACTION_CRASH,  "crash",  0 },
};

static const char* const usage[] = {
    "fault [status|clear|crash] [-s] [-c] [-x]",
    "Show crash record and stack watermarks (default):%s fault",
    "Show crash record and stack watermarks:%s fault status",
    "Clear crash record:%s fault clear",
    "Simulate HardFault crash (debug):%s fault crash",
};

static const bp_command_opt_t fault_opts[] = {
    { "status", 's', BP_ARG_NONE, NULL, 0 },
    { "clear",  'c', BP_ARG_NONE, NULL, 0 },
    { "crash",  'x', BP_ARG_NONE, NULL, 0 },
    { 0 }
};

const bp_command_def_t fault_def = {
    .name         = "fault",
    .description  = T_CMDLN_FAULT,
    .actions      = fault_action_defs,
    .action_count = count_of(fault_action_defs),
    .opts         = fault_opts,
    .usage        = usage,
    .usage_count  = count_of(usage),
};

void fault_handler(struct command_result* res) {
    if (bp_cmd_help_check(&fault_def, res->help_flag)) {
        return;
    }

    uint32_t action = 0;
    bp_cmd_get_action(&fault_def, &action);

    if (action == FAULT_ACTION_CLEAR || bp_cmd_find_flag(&fault_def, 'c')) {
        bp_crash_record_clear();
        printf("Crash record cleared.\r\n");
        return;
    }

    if (action == FAULT_ACTION_CRASH || bp_cmd_find_flag(&fault_def, 'x')) {
#if !defined(NDEBUG) || defined(BP_DEBUG) || defined(BP_FAULT_INJECTION)
        printf("Triggering diagnostic HardFault exception via invalid address write...\r\n");
        busy_wait_ms(50);
        volatile uint32_t *bad_ptr = (volatile uint32_t *)0xFFFFFFFFu;
        *bad_ptr = 0x42424242u;
        for (;;) {
            __asm volatile("nop");
        }
#else
        printf("Fault injection is disabled in this build.\r\n");
        return;
#endif
    }

    bp_crash_report_print();

    printf("Stack Free (Watermark 0x%08lX):\r\n", (unsigned long)BP_STACK_WATERMARK_WORD);
    printf("  Core 0 stack free: %lu bytes\r\n", (unsigned long)bp_stack_get_free(0));
    printf("  Core 1 stack free: %lu bytes\r\n", (unsigned long)bp_stack_get_free(1));

    if (system_config.safe_mode) {
        printf("Safe mode: ACTIVE (auto-scripts and power rails suppressed)\r\n");
    }
}
