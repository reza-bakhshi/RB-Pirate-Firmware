/**
 * @file test_cmd_completion.c
 * @brief Unit tests for command completion, hints, and diagnostic command descriptions.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>

#include "lib/bp_args/bp_cmd.h"
#include "translation/base.h"

/* Stubs for UI functions required by bp_cmd */
const char* ui_term_color_info(void) { return ""; }
const char* ui_term_color_reset(void) { return ""; }
const char* ui_term_color_prompt(void) { return ""; }
const char* ui_term_color_warning(void) { return ""; }
const char* ui_term_color_error(void) { return ""; }

bool ui_prompt_user_input(char *buf, size_t buflen) { (void)buf; (void)buflen; return false; }
bool ui_parse_get_bool(bool *val) { (void)val; return false; }
bool ui_parse_get_float(float *val) { (void)val; return false; }
bool ui_parse_get_uint32(uint32_t *val) { (void)val; return false; }

static const char *s_cmdln_buf = "";
const char *ln_cmdln_current(void) { return s_cmdln_buf; }
size_t ln_cmdln_remaining(void) { return strlen(s_cmdln_buf); }

/* Mock translations for GET_T */
static const char *mock_translations[] = {
    [T_CMDLN_INFO]    = "i - show Bus Pirate info and status screen.",
    [T_CMDLN_REBOOT]  = "reboot - reboot and restart the Bus Pirate.",
    [T_CMDLN_BOOTLOAD]= "$ - reset and enter bootloader mode for updates.",
    [T_CMDLN_HEALTH]  = "health - show terminal, binary transport, and system health counters.",
    [T_CMDLN_FAULT]   = "fault - show crash record, fault state, and stack watermarks.",
    [T_CMDLN_MEMORY]  = "memory - show SRAM layout, static memory budget, and stack headroom.",
    [T_CMDLN_KERNEL]  = "kernel - show cooperative kernel metrics, queues, and diagnostics.",
    [T_CMDLN_JOB]     = "job - show active protocol job status, submit jobs, or cancel.",
    [T_CMDLN_RES]     = "res - show hardware peripheral resource leases and cleanup.",
    [T_CMDLN_STORAGE] = "storage - show storage service status, mount/unmount filesystems.",
    [T_CMDLN_BUG]     = "bug - test RP2040/RP2350 hardware errata.",
    [T_CMDLN_OVRCLK]  = "ovrclk - overclock system frequency and core voltage.",
    [T_CMDLN_EDIT]    = "edit <file> - simple text editor for on-disk files.",
    [T_CMDLN_HEXEDIT] = "hexedit <file> - hex editor for on-disk binary files.",
};

const char *GET_T(enum T_translations index) {
    if (index < sizeof(mock_translations) / sizeof(mock_translations[0]) && mock_translations[index]) {
        return mock_translations[index];
    }
    return "Unknown";
}

/* Command definitions under test */
static const bp_command_action_t health_actions[] = {
    { 1, "status", 0 },
    { 2, "reset",  0 },
};
static const bp_command_opt_t health_opts[] = {
    { "status", 's', BP_ARG_NONE, NULL, 0, NULL, NULL },
    { "reset",  'r', BP_ARG_NONE, NULL, 0, NULL, NULL },
    { 0 }
};
static const bp_command_def_t health_def = {
    .name         = "health",
    .description  = T_CMDLN_HEALTH,
    .actions      = health_actions,
    .action_count = 2,
    .opts         = health_opts,
};

static const bp_command_action_t fault_actions[] = {
    { 1, "status", 0 },
    { 2, "clear",  0 },
    { 3, "crash",  0 },
};
static const bp_command_opt_t fault_opts[] = {
    { "status", 's', BP_ARG_NONE, NULL, 0, NULL, NULL },
    { "clear",  'c', BP_ARG_NONE, NULL, 0, NULL, NULL },
    { "crash",  'x', BP_ARG_NONE, NULL, 0, NULL, NULL },
    { 0 }
};
static const bp_command_def_t fault_def = {
    .name         = "fault",
    .description  = T_CMDLN_FAULT,
    .actions      = fault_actions,
    .action_count = 3,
    .opts         = fault_opts,
};

static const bp_command_action_t memory_actions[] = {
    { 1, "status", 0 },
};
static const bp_command_opt_t memory_opts[] = {
    { "status", 's', BP_ARG_NONE, NULL, 0, NULL, NULL },
    { 0 }
};
static const bp_command_def_t memory_def = {
    .name         = "memory",
    .description  = T_CMDLN_MEMORY,
    .actions      = memory_actions,
    .action_count = 1,
    .opts         = memory_opts,
};

static const bp_command_action_t kernel_actions[] = {
    { 1, "stats", 0 },
    { 2, "test",  0 },
};
static const bp_command_opt_t kernel_opts[] = {
    { "stats",       's', BP_ARG_NONE, NULL, 0, NULL, NULL },
    { "cancel-test", 't', BP_ARG_NONE, NULL, 0, NULL, NULL },
    { "test",        't', BP_ARG_NONE, NULL, 0, NULL, NULL },
    { 0 }
};
static const bp_command_def_t cmd_kernel_def = {
    .name         = "kernel",
    .description  = T_CMDLN_KERNEL,
    .actions      = kernel_actions,
    .action_count = 2,
    .opts         = kernel_opts,
};

static const bp_command_action_t job_actions[] = {
    { 1, "status", 0 },
    { 2, "run",    0 },
    { 3, "cancel", 0 },
};
static const bp_command_opt_t job_opts[] = {
    { "status", 's', BP_ARG_NONE,     NULL, 0, NULL, NULL },
    { "run",    'r', BP_ARG_REQUIRED, "ms", 0, NULL, NULL },
    { "cancel", 'c', BP_ARG_NONE,     NULL, 0, NULL, NULL },
    { 0 }
};
static const bp_command_def_t cmd_job_def = {
    .name         = "job",
    .description  = T_CMDLN_JOB,
    .actions      = job_actions,
    .action_count = 3,
    .opts         = job_opts,
};

static const bp_command_action_t res_actions[] = {
    { 1, "status", 0 },
    { 2, "clean",  0 },
};
static const bp_command_opt_t res_opts[] = {
    { "status", 's', BP_ARG_NONE, NULL, 0, NULL, NULL },
    { "clean",  'c', BP_ARG_NONE, NULL, 0, NULL, NULL },
    { 0 }
};
static const bp_command_def_t cmd_res_def = {
    .name         = "res",
    .description  = T_CMDLN_RES,
    .actions      = res_actions,
    .action_count = 2,
    .opts         = res_opts,
};

static const bp_command_action_t storage_actions[] = {
    { 1, "status",  0 },
    { 2, "mount",   0 },
    { 3, "unmount", 0 },
};
static const bp_command_opt_t storage_opts[] = {
    { "status",  's', BP_ARG_NONE, NULL, 0, NULL, NULL },
    { "mount",   'm', BP_ARG_NONE, NULL, 0, NULL, NULL },
    { "unmount", 'u', BP_ARG_NONE, NULL, 0, NULL, NULL },
    { 0 }
};
static const bp_command_def_t cmd_storage_def = {
    .name         = "storage",
    .description  = T_CMDLN_STORAGE,
    .actions      = storage_actions,
    .action_count = 3,
    .opts         = storage_opts,
};

static const bp_command_def_t reboot_def = {
    .name         = "reboot",
    .description  = T_CMDLN_REBOOT,
};

#define MAX_COMPLETIONS 32
static char s_completions[MAX_COMPLETIONS][64];
static size_t s_completion_count = 0;

static void test_add_completion(const char *text, void *ud) {
    (void)ud;
    if (s_completion_count < MAX_COMPLETIONS) {
        strncpy(s_completions[s_completion_count], text, sizeof(s_completions[0]) - 1);
        s_completions[s_completion_count][sizeof(s_completions[0]) - 1] = '\0';
        s_completion_count++;
    }
}

static void clear_completions(void) {
    s_completion_count = 0;
    memset(s_completions, 0, sizeof(s_completions));
}

static bool has_completion(const char *expected) {
    for (size_t i = 0; i < s_completion_count; i++) {
        if (strcmp(s_completions[i], expected) == 0) return true;
    }
    return false;
}

int main(void) {
    printf("=== Running Command Completion & Diagnostic Descriptions Test ===\n");

    const bp_command_def_t *defs[] = {
        &health_def,
        &fault_def,
        &memory_def,
        &cmd_kernel_def,
        &cmd_job_def,
        &cmd_res_def,
        &cmd_storage_def,
        &reboot_def,
    };
    size_t count = sizeof(defs) / sizeof(defs[0]);

    /* 1. Verify all diagnostic commands have valid, non-zero descriptions */
    printf("[TEST] Verify command descriptions...\n");
    assert(health_def.description == T_CMDLN_HEALTH);
    assert(fault_def.description == T_CMDLN_FAULT);
    assert(memory_def.description == T_CMDLN_MEMORY);
    assert(cmd_kernel_def.description == T_CMDLN_KERNEL);
    assert(cmd_job_def.description == T_CMDLN_JOB);
    assert(cmd_res_def.description == T_CMDLN_RES);
    assert(cmd_storage_def.description == T_CMDLN_STORAGE);
    assert(reboot_def.description == T_CMDLN_REBOOT);

    for (size_t i = 0; i < count; i++) {
        const char *desc = GET_T(defs[i]->description);
        assert(desc != NULL && strlen(desc) > 5);
        printf("  %-10s -> %s\n", defs[i]->name, desc);
    }
    printf("[PASS] Command descriptions verified\n");

    /* 2. Verify command name completion appends a trailing space when unique */
    printf("[TEST] Command name Tab-completion...\n");

    clear_completions();
    bp_cmd_completion("hea", 3, defs, count, test_add_completion, NULL);
    assert(s_completion_count == 1);
    assert(strcmp(s_completions[0], "health ") == 0);

    clear_completions();
    bp_cmd_completion("fau", 3, defs, count, test_add_completion, NULL);
    assert(s_completion_count == 1);
    assert(strcmp(s_completions[0], "fault ") == 0);

    clear_completions();
    bp_cmd_completion("mem", 3, defs, count, test_add_completion, NULL);
    assert(s_completion_count == 1);
    assert(strcmp(s_completions[0], "memory ") == 0);

    clear_completions();
    bp_cmd_completion("ker", 3, defs, count, test_add_completion, NULL);
    assert(s_completion_count == 1);
    assert(strcmp(s_completions[0], "kernel ") == 0);

    clear_completions();
    bp_cmd_completion("jo", 2, defs, count, test_add_completion, NULL);
    assert(s_completion_count == 1);
    assert(strcmp(s_completions[0], "job ") == 0);

    /* When typing "re", matches both "res" and "reboot" -> no trailing space */
    clear_completions();
    bp_cmd_completion("re", 2, defs, count, test_add_completion, NULL);
    assert(s_completion_count == 2);
    assert(has_completion("res"));
    assert(has_completion("reboot"));

    /* When typing "res", unique match -> appends trailing space */
    clear_completions();
    bp_cmd_completion("res", 3, defs, count, test_add_completion, NULL);
    assert(s_completion_count == 1);
    assert(strcmp(s_completions[0], "res ") == 0);

    clear_completions();
    bp_cmd_completion("sto", 3, defs, count, test_add_completion, NULL);
    assert(s_completion_count == 1);
    assert(strcmp(s_completions[0], "storage ") == 0);
    printf("[PASS] Command name Tab-completion verified\n");

    /* 3. Verify subcommands / action suggestions on Tab after command name */
    printf("[TEST] Action verb Tab-suggestions...\n");

    clear_completions();
    bp_cmd_completion("job ", 4, defs, count, test_add_completion, NULL);
    assert(has_completion("job status"));
    assert(has_completion("job run"));
    assert(has_completion("job cancel"));

    clear_completions();
    bp_cmd_completion("res ", 4, defs, count, test_add_completion, NULL);
    assert(has_completion("res status"));
    assert(has_completion("res clean"));

    clear_completions();
    bp_cmd_completion("fault ", 6, defs, count, test_add_completion, NULL);
    assert(has_completion("fault status"));
    assert(has_completion("fault clear"));
    assert(has_completion("fault crash"));

    clear_completions();
    bp_cmd_completion("kernel ", 7, defs, count, test_add_completion, NULL);
    assert(has_completion("kernel stats"));
    assert(has_completion("kernel test"));

    clear_completions();
    bp_cmd_completion("storage ", 8, defs, count, test_add_completion, NULL);
    assert(has_completion("storage status"));
    assert(has_completion("storage mount"));
    assert(has_completion("storage unmount"));

    clear_completions();
    bp_cmd_completion("health ", 7, defs, count, test_add_completion, NULL);
    assert(has_completion("health status"));
    assert(has_completion("health reset"));
    printf("[PASS] Action verb Tab-suggestions verified\n");

    /* 4. Verify flag Tab-completion */
    printf("[TEST] Flag Tab-completion...\n");

    clear_completions();
    bp_cmd_completion("job -", 5, defs, count, test_add_completion, NULL);
    assert(has_completion("job -s"));
    assert(has_completion("job -r"));
    assert(has_completion("job -c"));
    assert(has_completion("job -h"));

    clear_completions();
    bp_cmd_completion("fault -", 7, defs, count, test_add_completion, NULL);
    assert(has_completion("fault -s"));
    assert(has_completion("fault -c"));
    assert(has_completion("fault -x"));
    assert(has_completion("fault -h"));

    clear_completions();
    bp_cmd_completion("res -", 5, defs, count, test_add_completion, NULL);
    assert(has_completion("res -s"));
    assert(has_completion("res -c"));
    assert(has_completion("res -h"));
    printf("[PASS] Flag Tab-completion verified\n");

    /* 5. Verify Ghost-Text Hints */
    printf("[TEST] Ghost-Text Hints...\n");

    const char *h_job = bp_cmd_hint("job ", 4, defs, count);
    assert(h_job != NULL && strcmp(h_job, " status") == 0);

    const char *h_res = bp_cmd_hint("res ", 4, defs, count);
    assert(h_res != NULL && strcmp(h_res, " status") == 0);

    const char *h_fau = bp_cmd_hint("fault ", 6, defs, count);
    assert(h_fau != NULL && strcmp(h_fau, " status") == 0);

    const char *h_ker = bp_cmd_hint("kernel ", 7, defs, count);
    assert(h_ker != NULL && strcmp(h_ker, " stats") == 0);

    printf("[PASS] Ghost-Text Hints verified\n");

    printf("=== ALL COMMAND COMPLETION & DESCRIPTION TESTS PASSED ===\n");
    return 0;
}
