/**
 * @file cmd_glitch.c
 * @brief Global hardware fault injection (voltage crowbar and clock glitch) command
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "pirate.h"
#include "system_config.h"
#include "command_struct.h"
#include "ui/ui_term.h"
#include "ui/ui_help.h"
#include "lib/bp_args/bp_cmd.h"
#include "pirate/bio.h"
#include "pirate/button.h"
#include "services/bp_terminal.h"
#include "commands/global/cmd_glitch.h"

#if !defined(BP_HOST_TEST)
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "platform/rb-pirate.h"

static void delay_ns(uint32_t ns) {
    if (ns == 0) return;
    uint32_t sys_hz = clock_get_hz(clk_sys);
    uint64_t cycles = ((uint64_t)ns * (uint64_t)sys_hz) / 1000000000ULL;
    if (cycles < 3) cycles = 3;
    busy_wait_at_least_cycles(cycles);
}
#endif

/* Modified by Codex: bounded worker pulses; never use PSU protection as crowbar. */
#include "services/bp_protocol_call.h"
#include "kernel/bp_resource.h"

#define GLITCH_OWNER BP_RES_OWNER_GLITCH
#define GLITCH_RESOURCE BP_RES_PERIPH_GLITCH
#define GLITCH_MAX_NS 100000u
#define GLITCH_MAX_COUNT 1000u
#define GLITCH_TRIGGER_TIMEOUT_US 1000000u

typedef struct {
    uint32_t pin, trigger, delay_ns, width_ns, count, completed, armed_us;
    bool manual, falling, saw_inactive;
} glitch_context_t;

static bp_status_t glitch_step(void *opaque, uint8_t *percent) {
    glitch_context_t *ctx = opaque;
    char key;
    if (bp_job_check_cancelled() || button_get(0) || bp_term_job_getc(&key) == BP_STATUS_OK)
        return BP_STATUS_CANCELLED;
    if (!ctx->manual) {
        bool active = bio_get(ctx->trigger) != ctx->falling;
        if (!active) ctx->saw_inactive = true;
        if (!active || !ctx->saw_inactive) {
            if ((uint32_t)(time_us_32() - ctx->armed_us) >= GLITCH_TRIGGER_TIMEOUT_US)
                return BP_STATUS_TIMEOUT;
            return BP_STATUS_BUSY;
        }
    }
#if !defined(BP_HOST_TEST)
    uint32_t ints = save_and_disable_interrupts();
    delay_ns(ctx->delay_ns);
    bio_put(ctx->pin, true);
    delay_ns(ctx->width_ns);
    bio_put(ctx->pin, false);
    restore_interrupts(ints);
#endif
    ctx->completed++;
    *percent = (uint8_t)(ctx->completed * 100 / ctx->count);
    ctx->saw_inactive = false;
    ctx->armed_us = time_us_32();
    return ctx->completed == ctx->count ? BP_STATUS_OK : BP_STATUS_BUSY;
}

static const char* const usage[] = {
    "glitch -c <pin> [-t <trigger>] [-d <ns>] [-w <ns>] [-n <count>] [-m] [-f]",
    "Logic pulse on BIO2:%s glitch -c 2 -w 100",
    "Falling edge trigger on BIO0:%s glitch -c 2 -t 0 -f",
};
static const bp_command_opt_t glitch_opts[] = {
    { "power", 'p', BP_ARG_NONE, NULL, 0, NULL, "Unsupported: board has no dedicated crowbar output" },
    { "pin", 'c', BP_ARG_REQUIRED, "pin", 0, NULL, "Target BIO pin (0..7), HiZ mode only" },
    { "trigger", 't', BP_ARG_REQUIRED, "pin", 0, NULL, "Trigger BIO pin (0..7), 1 second timeout per pulse" },
    { "delay", 'd', BP_ARG_REQUIRED, "ns", 0, NULL, "Delay (0..100000 ns)" },
    { "width", 'w', BP_ARG_REQUIRED, "ns", 0, NULL, "Width (1..100000 ns), software timing is approximate" },
    { "count", 'n', BP_ARG_REQUIRED, "count", 0, NULL, "Pulse count (1..1000)" },
    { "manual", 'm', BP_ARG_NONE, NULL, 0, NULL, "Immediate software trigger" },
    { "falling", 'f', BP_ARG_NONE, NULL, 0, NULL, "Falling edge instead of rising edge" },
    { 0 }
};
const bp_command_def_t cmd_glitch_def = {
    .name = "glitch", .description = 0, .opts = glitch_opts,
    .usage = usage, .usage_count = count_of(usage),
};

void cmd_glitch_handler(struct command_result* res) {
    if (bp_cmd_help_check(&cmd_glitch_def, res->help_flag)) return;
    res->error = true;
    if (bp_cmd_find_flag(&cmd_glitch_def, 'p')) {
        printf("Power glitch unsupported: no dedicated crowbar output on this board.\r\n");
        return;
    }
    glitch_context_t ctx = { .width_ns = 100, .count = 1 };
    bool target = bp_cmd_get_uint32(&cmd_glitch_def, 'c', &ctx.pin);
    bool trigger = bp_cmd_get_uint32(&cmd_glitch_def, 't', &ctx.trigger);
    bool valid = true;
    const char flags[] = { 'd', 'w', 'n', 't' };
    uint32_t *values[] = { &ctx.delay_ns, &ctx.width_ns, &ctx.count, &ctx.trigger };
    for (unsigned i = 0; i < sizeof(flags); ++i) {
        if (bp_cmd_find_flag(&cmd_glitch_def, flags[i]) &&
            !bp_cmd_get_uint32(&cmd_glitch_def, flags[i], values[i])) valid = false;
    }
    ctx.manual = bp_cmd_find_flag(&cmd_glitch_def, 'm') || !trigger;
    ctx.falling = bp_cmd_find_flag(&cmd_glitch_def, 'f');
    if (!valid || !target || ctx.pin >= BIO_MAX_PINS || (trigger && ctx.trigger >= BIO_MAX_PINS) ||
        (trigger && ctx.pin == ctx.trigger) || ctx.delay_ns > GLITCH_MAX_NS ||
        ctx.width_ns == 0 || ctx.width_ns > GLITCH_MAX_NS ||
        ctx.count == 0 || ctx.count > GLITCH_MAX_COUNT) {
        printf("Invalid glitch pins or timing/count limits.\r\n");
        return;
    }
    if (bp_job_get_active_id() != 0 || system_config.mode != 0 || system_config.pin_func[ctx.pin + 1] != BP_PIN_IO ||
        (trigger && system_config.pin_func[ctx.trigger + 1] != BP_PIN_IO)) {
        printf("Glitch requires HiZ mode and unused pins.\r\n");
        return;
    }
    if (bp_res_acquire(BP_RES_CLASS_PERIPH, GLITCH_RESOURCE, GLITCH_OWNER, "glitch") != BP_STATUS_OK) {
        printf("Glitch engine busy.\r\n");
        return;
    }
    system_bio_update_purpose_and_label(true, ctx.pin, BP_PIN_MODE, "GLITCH");
    if (trigger) system_bio_update_purpose_and_label(true, ctx.trigger, BP_PIN_MODE, "TRIGGER");
    bio_set_function(ctx.pin, GPIO_FUNC_SIO);
    bio_put(ctx.pin, false);
    bio_output(ctx.pin);
    if (trigger) bio_set_function(ctx.trigger, GPIO_FUNC_SIO);
    if (trigger) bio_input(ctx.trigger);
    ctx.armed_us = time_us_32();
    bp_status_t status = bp_protocol_call("glitch", glitch_step, &ctx);
    bio_put(ctx.pin, false);
    bio_input(ctx.pin);
    if (trigger) bio_input(ctx.trigger);
    system_bio_update_purpose_and_label(false, ctx.pin, BP_PIN_MODE, NULL);
    if (trigger) system_bio_update_purpose_and_label(false, ctx.trigger, BP_PIN_MODE, NULL);
    bp_res_release(BP_RES_CLASS_PERIPH, GLITCH_RESOURCE, GLITCH_OWNER);
    res->error = status != BP_STATUS_OK;
    printf("Glitch: %lu pulse(s), %s. Timing is approximate.\r\n",
           (unsigned long)ctx.completed, bp_status_str(status));
}
