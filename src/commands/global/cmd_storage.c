#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "pirate.h"
#include "system_config.h"
#include "command_struct.h"
#include "ui/ui_term.h"
#include "ui/ui_help.h"
#include "lib/bp_args/bp_cmd.h"
#include "services/bp_storage.h"
#include "commands/global/cmd_storage.h"

enum storage_actions {
    STORAGE_ACTION_STATUS = 1,
    STORAGE_ACTION_MOUNT,
    STORAGE_ACTION_UNMOUNT,
};

static const bp_command_action_t storage_action_defs[] = {
    { STORAGE_ACTION_STATUS,  "status",  0 },
    { STORAGE_ACTION_MOUNT,   "mount",   0 },
    { STORAGE_ACTION_UNMOUNT, "unmount", 0 },
};

static const char* const usage[] = {
    "storage [status|mount|unmount] [-s] [-m] [-u]",
    "Show storage health and telemetry (default):%s storage",
    "Show storage telemetry:%s storage status",
    "Mount filesystem:%s storage mount",
    "Unmount filesystem:%s storage unmount",
};

static const bp_command_opt_t storage_opts[] = {
    { "status",  's', BP_ARG_NONE, NULL, 0 },
    { "mount",   'm', BP_ARG_NONE, NULL, 0 },
    { "unmount", 'u', BP_ARG_NONE, NULL, 0 },
    { 0 }
};

const bp_command_def_t cmd_storage_def = {
    .name         = "storage",
    .description  = T_CMDLN_STORAGE,
    .actions      = storage_action_defs,
    .action_count = count_of(storage_action_defs),
    .opts         = storage_opts,
    .usage        = usage,
    .usage_count  = count_of(usage),
};

static const char* const s_state_names[] = {
    [BP_STORAGE_STATE_UNMOUNTED]  = "Unmounted",
    [BP_STORAGE_STATE_MOUNTING]   = "Mounting",
    [BP_STORAGE_STATE_MOUNTED]    = "Mounted",
    [BP_STORAGE_STATE_UNMOUNTING] = "Unmounting",
    [BP_STORAGE_STATE_ERROR]      = "Error",
};

void cmd_storage_handler(struct command_result* res) {
    if (bp_cmd_help_check(&cmd_storage_def, res->help_flag)) {
        return;
    }

    uint32_t action = 0;
    bp_cmd_get_action(&cmd_storage_def, &action);

    if (action == STORAGE_ACTION_MOUNT || bp_cmd_find_flag(&cmd_storage_def, 'm')) {
        bp_status_t st = bp_storage_mount();
        if (st == BP_STATUS_OK) {
            printf("%sStorage mounted successfully.%s\r\n", ui_term_color_info(), ui_term_color_reset());
        } else {
            printf("%sStorage mount failed (error code %d).%s\r\n",
                   ui_term_color_error(), system_config.storage_mount_error, ui_term_color_reset());
        }
        return;
    }

    if (action == STORAGE_ACTION_UNMOUNT || bp_cmd_find_flag(&cmd_storage_def, 'u')) {
        bp_status_t st = bp_storage_unmount();
        if (st == BP_STATUS_OK) {
            printf("%sStorage unmounted successfully.%s\r\n", ui_term_color_info(), ui_term_color_reset());
        } else if (st == BP_STATUS_BUSY) {
            printf("%sCannot unmount: files are currently open (%u open handle(s)).%s\r\n",
                   ui_term_color_error(), (unsigned)bp_storage_get_open_count(), ui_term_color_reset());
        } else {
            printf("%sStorage unmount failed.%s\r\n", ui_term_color_error(), ui_term_color_reset());
        }
        return;
    }

    bp_storage_stats_t stats;
    bp_storage_get_stats(&stats);

    printf("\r\n%sStorage Subsystem Health & Telemetry:%s\r\n", ui_term_color_prompt(), ui_term_color_reset());
    printf("  State:           %s%s%s\r\n",
           (stats.state == BP_STORAGE_STATE_MOUNTED) ? ui_term_color_info() :
           (stats.state == BP_STORAGE_STATE_ERROR) ? ui_term_color_error() : ui_term_color_grey(),
           (stats.state < count_of(s_state_names) && s_state_names[stats.state]) ? s_state_names[stats.state] : "Unknown",
           ui_term_color_reset());

    if (stats.state == BP_STORAGE_STATE_MOUNTED) {
        const char* type_str = (stats.fat_type >= 1 && stats.fat_type <= 4) ?
                               storage_fat_type_labels[stats.fat_type - 1] : "Unknown";
        printf("  Filesystem:      %s\r\n", type_str);
        printf("  Capacity:        %7.2f GB\r\n", stats.capacity_gb);
    }

    printf("  Card Present:    %s\r\n", stats.card_detected ? "Yes" : "No");
    printf("  Open Files:      %u\r\n", (unsigned)stats.open_file_count);
    printf("  Mount Attempts:  %u (successes: %u, errors: %u)\r\n",
           (unsigned)stats.mount_attempts, (unsigned)stats.mount_successes, (unsigned)stats.mount_errors);
    printf("  Unmounts:        %u\r\n", (unsigned)stats.unmount_count);
    printf("  I/O Errors:      read: %u, write: %u\r\n",
           (unsigned)stats.read_errors, (unsigned)stats.write_errors);
    printf("  Storage RPC:     handled: %u, drops: %u, timeouts: %u\r\n",
           (unsigned)stats.rpc_requests, (unsigned)stats.rpc_drops, (unsigned)stats.rpc_timeouts);
    printf("  Capture Stream:  chunks written: %u, dropped: %u\r\n",
           (unsigned)stats.stream_chunks_written, (unsigned)stats.stream_chunks_dropped);

    if (stats.state == BP_STORAGE_STATE_ERROR) {
        printf("\r\n%sGuidance:%s Storage is in error state (code %u).\r\n",
               ui_term_color_error(), ui_term_color_reset(), (unsigned)stats.last_error_code);
        printf("  - If no filesystem exists, execute 'format' to initialize a new FAT volume.\r\n");
        printf("  - Verify that the storage card is inserted correctly into the slot.\r\n");
    }
    printf("\r\n");
}
