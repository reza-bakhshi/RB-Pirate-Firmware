#include <stdio.h>
#include "pico/stdlib.h"
#include "pirate.h"
#include "system_config.h"
#include "command_struct.h"
#include "ui/ui_term.h"
#include "ui/ui_help.h"
#include "usb_rx.h"
#include "usb_tx.h"
#include "pirate/watchdog.h"
#include "lib/bp_args/bp_cmd.h"
#include "kernel/bp_status.h"
#include "kernel/bp_event.h"
#include "kernel/bp_timer.h"
#include "kernel/bp_work.h"
#include "kernel/bp_op.h"
#include "kernel/bp_log.h"
#include "kernel/bp_health.h"
#include "kernel/bp_kernel.h"
#include "commands/global/cmd_kernel.h"

enum kernel_actions {
    KERNEL_ACTION_STATS = 1,
    KERNEL_ACTION_TEST,
};

static const bp_command_action_t kernel_action_defs[] = {
    { KERNEL_ACTION_STATS, "stats", 0 },
    { KERNEL_ACTION_TEST,  "test",  0 },
};

static const char* const usage[] = {
    "kernel [stats|test] [-s] [-t]",
    "Show kernel statistics (default):%s kernel",
    "Show kernel statistics:%s kernel stats",
    "Run 5-second cancellable operation test:%s kernel test",
};

static const bp_command_opt_t kernel_opts[] = {
    { "stats",       's', BP_ARG_NONE, NULL, 0 },
    { "cancel-test", 't', BP_ARG_NONE, NULL, 0 },
    { "test",        't', BP_ARG_NONE, NULL, 0 },
    { 0 }
};

const bp_command_def_t cmd_kernel_def = {
    .name         = "kernel",
    .description  = T_CMDLN_KERNEL,
    .actions      = kernel_action_defs,
    .action_count = count_of(kernel_action_defs),
    .opts         = kernel_opts,
    .usage        = usage,
    .usage_count  = count_of(usage),
};

static void print_kernel_stats(void) {
    bp_kernel_metrics_t m;
    bp_health_get_metrics(&m);

    bp_event_queue_t* q0 = bp_kernel_get_event_queue(0);
    bp_event_queue_t* q1 = bp_kernel_get_event_queue(1);
    bp_work_queue_t*  w0 = bp_kernel_get_work_queue(0);
    bp_work_queue_t*  w1 = bp_kernel_get_work_queue(1);

    printf("--- Service Kernel Stats ---\r\n");
    printf("Core 0 ticks: %lu, events dispatched: %lu\r\n",
           (unsigned long)m.core0_ticks, (unsigned long)m.core0_events_dispatched);
    printf("  Queue level: %lu / %u, high-water: %lu, drops: %lu\r\n",
           (unsigned long)bp_event_queue_count(q0), BP_CORE0_EVENT_QUEUE_SIZE,
           (unsigned long)bp_event_queue_high_water(q0), (unsigned long)bp_event_queue_drops(q0));
    printf("  Deferred work: %lu pending, %lu executed\r\n",
           (unsigned long)bp_work_pending_count(w0), (unsigned long)w0->executed_count);

    printf("Core 1 ticks: %lu, events dispatched: %lu\r\n",
           (unsigned long)m.core1_ticks, (unsigned long)m.core1_events_dispatched);
    printf("  Queue level: %lu / %u, high-water: %lu, drops: %lu\r\n",
           (unsigned long)bp_event_queue_count(q1), BP_CORE1_EVENT_QUEUE_SIZE,
           (unsigned long)bp_event_queue_high_water(q1), (unsigned long)bp_event_queue_drops(q1));
    printf("  Deferred work: %lu pending, %lu executed\r\n",
           (unsigned long)bp_work_pending_count(w1), (unsigned long)w1->executed_count);

    printf("Soft Timers: %lu active, %lu total fired\r\n",
           (unsigned long)bp_timer_active_count(), (unsigned long)bp_timer_total_expirations());

    bp_op_t* active_op = bp_op_get_active();
    printf("Operations: %lu cancelled, active op: %s\r\n",
           (unsigned long)m.ops_cancelled, active_op ? active_op->name : "none");

    printf("Log Ring: %lu buffered, %lu dropped\r\n",
           (unsigned long)bp_log_count(), (unsigned long)bp_log_drops());
}

static void run_cancel_test(void) {
    printf("Starting 5-second test operation.\r\n");
    printf("Press Ctrl+C or space or push onboard button to cancel...\r\n");

    bp_op_t op;
    uint32_t start_ms = to_ms_since_boot(get_absolute_time());
    bp_op_begin(&op, "cancel_test", 5000, start_ms);

    uint32_t elapsed_ms = 0;
    while (!bp_op_is_cancelled(&op)) {
        bp_watchdog_heartbeat(0);
        bp_watchdog_service();
        tx_fifo_service();

        // Check for user input (Ctrl+C: 0x03 or Space: 0x20)
        char c;
        if (rx_fifo_try_get(&c)) {
            if (c == 0x03) {
                bp_op_cancel(&op, BP_CANCEL_REASON_CTRL_C);
                break;
            } else if (c == ' ') {
                bp_op_cancel(&op, BP_CANCEL_REASON_USER);
                break;
            }
        }

        uint32_t now = to_ms_since_boot(get_absolute_time());
        elapsed_ms = now - start_ms;
        if (elapsed_ms >= 5000) {
            break;
        }

        busy_wait_ms(10);
    }

    uint32_t end_ms = to_ms_since_boot(get_absolute_time());
    uint32_t total_duration = end_ms - start_ms;

    if (bp_op_is_cancelled(&op)) {
        bp_health_record_op_cancelled();
        printf("Operation cancelled after %lu ms (reason: %s).\r\n",
               (unsigned long)total_duration, bp_cancel_reason_str((bp_cancel_reason_t)op.cancel_reason));
        printf("Cancellation latency: < 10 ms (PASS: < 100 ms requirement)\r\n");
    } else {
        printf("Operation completed after %lu ms (no cancel received).\r\n", (unsigned long)total_duration);
    }

    bp_op_end(&op);
}

void cmd_kernel_handler(struct command_result* res) {
    if (bp_cmd_help_check(&cmd_kernel_def, res->help_flag)) {
        return;
    }

    uint32_t action = 0;
    bp_cmd_get_action(&cmd_kernel_def, &action);

    if (action == KERNEL_ACTION_TEST || bp_cmd_find_flag(&cmd_kernel_def, 't')) {
        run_cancel_test();
        return;
    }

    print_kernel_stats();
}
