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
#include "kernel/bp_job.h"
#include "commands/global/cmd_job.h"

enum job_actions {
    JOB_ACTION_STATUS = 1,
    JOB_ACTION_RUN,
    JOB_ACTION_CANCEL,
};

static const bp_command_action_t job_action_defs[] = {
    { JOB_ACTION_STATUS, "status", 0 },
    { JOB_ACTION_RUN,    "run",    0 },
    { JOB_ACTION_CANCEL, "cancel", 0 },
};

static const char* const usage[] = {
    "job [status|run|cancel] [-s] [-r <ms>] [-c]",
    "Show active protocol job status (default):%s job",
    "Show active protocol job status:%s job status",
    "Submit simulated protocol job to Core 1:%s job run 5000",
    "Cancel active protocol job:%s job cancel",
};

static const bp_command_opt_t job_opts[] = {
    { "status", 's', BP_ARG_NONE,     NULL, 0 },
    { "run",    'r', BP_ARG_REQUIRED, "ms", 0 },
    { "cancel", 'c', BP_ARG_NONE,     NULL, 0 },
    { 0 }
};

const bp_command_def_t cmd_job_def = {
    .name         = "job",
    .description  = T_CMDLN_JOB,
    .actions      = job_action_defs,
    .action_count = count_of(job_action_defs),
    .opts         = job_opts,
    .usage        = usage,
    .usage_count  = count_of(usage),
};

typedef struct sim_job_ctx {
    uint32_t step;
    uint32_t total_steps;
} sim_job_ctx_t;

static sim_job_ctx_t s_sim_ctx;

static bp_status_t sim_slice_fn(void* ctx_ptr, uint8_t* out_percent) {
    sim_job_ctx_t* ctx = (sim_job_ctx_t*)ctx_ptr;
    ctx->step++;
    if (ctx->total_steps == 0) ctx->total_steps = 1;
    *out_percent = (uint8_t)((ctx->step * 100) / ctx->total_steps);

    if (ctx->step >= ctx->total_steps) {
        return BP_STATUS_OK;
    }
    return BP_STATUS_BUSY;
}

static void print_job_status(void) {
    uint32_t jid = bp_job_get_active_id();
    bp_job_state_t state;
    bp_status_t status;
    uint8_t pct;

    if (bp_job_get_state(jid, &state, &status, &pct) == BP_STATUS_OK) {
        printf("--- Protocol Job Status ---\r\n");
        printf("Job ID: %lu\r\n", (unsigned long)jid);
        printf("State: %s (%u%%)\r\n", bp_job_state_str(state), (unsigned)pct);
        printf("Status: %s\r\n", bp_status_str(status));

        // Drain pending output records
        bp_job_output_t out;
        while (bp_job_poll_output(&out) == BP_STATUS_OK) {
            printf("  [%u%%] %s\r\n", (unsigned)out.percent, out.text);
        }
    } else {
        printf("No active or recent protocol job.\r\n");
    }
}

void cmd_job_handler(struct command_result* res) {
    if (bp_cmd_help_check(&cmd_job_def, res->help_flag)) {
        return;
    }

    uint32_t action = 0;
    bp_cmd_get_action(&cmd_job_def, &action);

    if (action == JOB_ACTION_CANCEL || bp_cmd_find_flag(&cmd_job_def, 'c')) {
        uint32_t jid = bp_job_get_active_id();
        if (jid == 0) {
            printf("No active protocol job to cancel.\r\n");
            return;
        }
        if (bp_job_cancel(jid) == BP_STATUS_OK) {
            printf("Cancellation requested for job %lu.\r\n", (unsigned long)jid);
        } else {
            printf("Failed to cancel job %lu.\r\n", (unsigned long)jid);
        }
        return;
    }

    uint32_t duration_ms = 5000;
    bool has_run = (action == JOB_ACTION_RUN) || bp_cmd_get_uint32(&cmd_job_def, 'r', &duration_ms);
    if (!has_run && bp_cmd_find_flag(&cmd_job_def, 'r')) {
        has_run = true;
    }

    if (has_run) {
        if (duration_ms == 0) duration_ms = 5000;

        s_sim_ctx.step = 0;
        s_sim_ctx.total_steps = duration_ms / 50;
        if (s_sim_ctx.total_steps == 0) s_sim_ctx.total_steps = 1;

        uint32_t now = to_ms_since_boot(get_absolute_time());
        uint32_t jid = 0;
        bp_status_t st = bp_job_submit("sim_proto", BP_JOB_PROTO_TEST, duration_ms * 2,
                                       sim_slice_fn, &s_sim_ctx, now, &jid);
        if (st == BP_STATUS_OK) {
            printf("Protocol job %lu (\"sim_proto\", %lu ms) submitted to Core 1.\r\n",
                   (unsigned long)jid, (unsigned long)duration_ms);
            printf("Core 0 terminal remains responsive. Type 'job' to check status or 'job -c' to cancel.\r\n");
        } else if (st == BP_STATUS_BUSY) {
            printf("Error: Core 1 is busy executing job %lu.\r\n",
                   (unsigned long)bp_job_get_active_id());
        } else {
            printf("Error: Failed to submit job (%s).\r\n", bp_status_str(st));
        }
        return;
    }

    print_job_status();
}
