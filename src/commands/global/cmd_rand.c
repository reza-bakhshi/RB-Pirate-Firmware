#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "pirate.h"
#include "system_config.h"
#include "command_struct.h"
#include "ui/ui_term.h"
#include "ui/ui_help.h"
#include "lib/bp_args/bp_cmd.h"
#include "pirate/trng.h"
#include "commands/global/cmd_rand.h"

static const char* const usage[] = {
    "rand [-c <count>]",
    "Generate random 32-bit words using RP2350 hardware entropy:%s rand",
    "Output 8 random words:%s rand -c 8",
};

static const bp_command_opt_t rand_opts[] = {
    { "count", 'c', BP_ARG_REQUIRED, "Number of random 32-bit words (1-64)", 0 },
    { 0 }
};

const bp_command_def_t cmd_rand_def = {
    .name         = "rand",
    .description  = 0x00,
    .opts         = rand_opts,
    .usage        = usage,
    .usage_count  = count_of(usage),
};

void cmd_rand_handler(struct command_result* res) {
    if (bp_cmd_help_check(&cmd_rand_def, res->help_flag)) {
        return;
    }

    uint32_t count = 4;
    uint32_t user_count = 0;
    if (bp_cmd_get_uint32(&cmd_rand_def, 'c', &user_count)) {
        if (user_count > 0 && user_count <= 64) {
            count = user_count;
        } else {
            printf("%sError:%s count must be between 1 and 64\r\n", ui_term_color_error(), ui_term_color_reset());
            return;
        }
    }

    printf("\r\n%sRP2350 Hardware TRNG Entropy:%s\r\n", ui_term_color_prompt(), ui_term_color_reset());
    for (uint32_t i = 0; i < count; i++) {
        uint32_t val = bp_trng_get_u32();
        printf("  [%2lu] %s0x%08X%s (%10lu)\r\n",
               (unsigned long)(i + 1),
               ui_term_color_num_float(),
               (unsigned int)val,
               ui_term_color_reset(),
               (unsigned long)val);
    }
    printf("\r\n");
}
