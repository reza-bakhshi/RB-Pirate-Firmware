#include <stdio.h>
#include "pico/stdlib.h"
#include "pirate.h"
#include "system_config.h"
#include "command_struct.h"
#include "pirate/watchdog.h"
#include "pirate/mem.h"
#include "services/bp_flatcc_memory.h"
#include "ui/ui_term.h"
#include "ui/ui_help.h"
#include "lib/bp_args/bp_cmd.h"
#include "commands/global/memory.h"

#if defined(__arm__) || defined(__thumb__)
extern uint32_t __data_start__;
extern uint32_t __data_end__;
extern uint32_t __bss_start__;
extern uint32_t __bss_end__;
extern uint32_t __end__;
#endif

enum memory_actions {
    MEMORY_ACTION_STATUS = 1,
};

static const bp_command_action_t memory_action_defs[] = {
    { MEMORY_ACTION_STATUS, "status", 0 },
};

static const bp_command_opt_t memory_opts[] = {
    { "status", 's', BP_ARG_NONE, NULL, 0 },
    { 0 }
};

static const char* const usage[] = {
    "memory [status] [-s]",
    "Show SRAM layout, static memory budget, and stack headroom (default):%s memory",
    "Show SRAM layout:%s memory status",
};

const bp_command_def_t memory_def = {
    .name         = "memory",
    .description  = T_CMDLN_MEMORY,
    .actions      = memory_action_defs,
    .action_count = count_of(memory_action_defs),
    .opts         = memory_opts,
    .usage        = usage,
    .usage_count  = count_of(usage),
};

void memory_handler(struct command_result* res) {
    if (bp_cmd_help_check(&memory_def, res->help_flag)) {
        return;
    }

#if defined(__arm__) || defined(__thumb__)
    uint32_t ram_start = 0x20000000u;
    uint32_t ram_end   = 0x20080000u; /* 512 KB main SRAM */
    uint32_t static_end = (uint32_t)&__end__;
    uint32_t static_used = static_end - ram_start;
    uint32_t headroom = (static_end < ram_end) ? (ram_end - static_end) : 0;
    uint32_t data_size = (uint32_t)&__data_end__ - (uint32_t)&__data_start__;
    uint32_t bss_size  = (uint32_t)&__bss_end__ - (uint32_t)&__bss_start__;
#else
    uint32_t static_used = 332336;
    uint32_t headroom = 524288 - static_used;
    uint32_t data_size = 22160;
    uint32_t bss_size = 273000;
#endif

    printf("\r\n=================== RP2350 STATIC MEMORY BUDGET ===================\r\n");
    printf("Total SRAM:          520 KB (512 KB Main SRAM + 2x 4 KB Scratch SRAM)\r\n");
    printf("Static Allocation:   %lu bytes (%lu KB) / 380 KB Budget (%.1f%%)\r\n",
           (unsigned long)static_used,
           (unsigned long)(static_used / 1024),
           (double)static_used * 100.0 / 389120.0);
    printf("  .data section:     %lu bytes\r\n", (unsigned long)data_size);
    printf("  .bss section:      %lu bytes\r\n", (unsigned long)bss_size);
    printf("Contiguous Headroom: %lu bytes (%lu KB free in Main SRAM)\r\n",
           (unsigned long)headroom, (unsigned long)(headroom / 1024));

    printf("\r\n--- Scratch SRAM Stacks ---\r\n");
    printf("  Core 0 (CTRL):     4096 bytes (SCRATCH_Y), %lu bytes free high-water\r\n",
           (unsigned long)bp_stack_get_free(0));
    printf("  Core 1 (PROTO):    4096 bytes (SCRATCH_X), %lu bytes free high-water\r\n",
           (unsigned long)bp_stack_get_free(1));

    printf("\r\n--- Shared Buffers & Subsystems ---\r\n");
    printf("  Big Buffer (128K): %s (Owner: #%u)\r\n",
           (system_config.big_buffer_owner == BP_BIG_BUFFER_NONE) ? "Available" : "Allocated",
           (unsigned)system_config.big_buffer_owner);
    printf("  FatFS LFN Buffer:  Stack-allocated (FF_USE_LFN=2, zero heap)\r\n");
    printf("  SubGHz Protocols:  Static Pool (64 decoders, 4 encoders, zero heap)\r\n");
    printf("  BPIO FlatCC:       Static packet copy buffer (zero heap)\r\n");
    printf("  LCDTap Engine:     Placement new + Big Buffer frame (zero heap)\r\n");
    printf("  Dynamic Heap:      Not instrumented; FlatCC uses fixed block pools\r\n");
    printf("  FlatCC pool:       %lu live blocks, %lu allocation failures\r\n",
           (unsigned long)bp_flatcc_live_blocks(), (unsigned long)bp_flatcc_allocation_failures());
    printf("===================================================================\r\n\r\n");
}
