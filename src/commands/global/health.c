#include <stdio.h>
#include "pico/stdlib.h"
#include "pirate.h"
#include "system_config.h"
#include "command_struct.h"
#include "bytecode.h"
#include "modes.h"
#include "usb_tx.h"
#include "ui/ui_term.h"
#include "ui/ui_help.h"
#include "lib/bp_args/bp_cmd.h"
#include "pirate/watchdog.h"
#include "pirate/psu.h"
#include "pirate/amux.h"
#include "pirate/crash_record.h"
#include "services/bp_bin_transport.h"
#include "kernel/bp_protocol.h"
#include "commands/global/health.h"

enum health_actions {
    HEALTH_ACTION_STATUS = 1,
    HEALTH_ACTION_RESET,
    HEALTH_ACTION_PSU,
};

static const bp_command_action_t health_action_defs[] = {
    { HEALTH_ACTION_STATUS, "status", 0 },
    { HEALTH_ACTION_RESET,  "reset",  0 },
    { HEALTH_ACTION_PSU,    "psu",    0 },
};

static const bp_command_opt_t health_opts[] = {
    { "status", 's', BP_ARG_NONE, NULL, 0 },
    { "reset",  'r', BP_ARG_NONE, NULL, 0 },
    { "psu",    'p', BP_ARG_NONE, NULL, 0 },
    { 0 }
};

static const char* const usage[] = {
    "health [status|reset|psu] [-s] [-r] [-p]",
    "Show terminal and system health counters (default):%s health",
    "Show health counters:%s health status",
    "Reset terminal health counters:%s health reset",
    "Run PSU hardware circuit diagnostic:%s health psu",
};

const bp_command_def_t health_def = {
    .name         = "health",
    .description  = T_CMDLN_HEALTH,
    .actions      = health_action_defs,
    .action_count = count_of(health_action_defs),
    .opts         = health_opts,
    .usage        = usage,
    .usage_count  = count_of(usage),
};

void health_handler(struct command_result* res) {
    if (bp_cmd_help_check(&health_def, res->help_flag)) {
        return;
    }

    uint32_t action = 0;
    bp_cmd_get_action(&health_def, &action);

    if (action == HEALTH_ACTION_RESET || bp_cmd_find_flag(&health_def, 'r')) {
        bp_terminal_health = (bp_terminal_health_t){ 0 };
        printf("Terminal health counters reset.\r\n");
        return;
    }

    if (action == HEALTH_ACTION_PSU || bp_cmd_find_flag(&health_def, 'p')) {
        printf("--- PSU Pull Investigation & Inrush Test ---\r\n");

        // First test pad pulls on GPIO 17
        printf("Initial pads test...\r\n");
        gpio_disable_pulls(CURRENT_FUSE_DETECT);
        gpio_put(CURRENT_EN_OVERRIDE, 0);
        gpio_put(CURRENT_EN, 1);
        psu_dac_set(5667, 14000);
        busy_wait_ms(20);

        // Reset the latch
        gpio_put(CURRENT_RESET, 0);
        busy_wait_ms(5);
        gpio_put(CURRENT_RESET, 1);
        busy_wait_ms(5);
        printf("With pulls DISABLED: DETECT=%d, fuse ADC=%u\r\n",
               gpio_get(CURRENT_FUSE_DETECT), (unsigned)amux_read(HW_ADC_MUX_CURRENT_DETECT));

        gpio_pull_up(CURRENT_FUSE_DETECT);
        busy_wait_ms(5);
        printf("With PULL-UP enabled: DETECT=%d, fuse ADC=%u\r\n",
               gpio_get(CURRENT_FUSE_DETECT), (unsigned)amux_read(HW_ADC_MUX_CURRENT_DETECT));

        // Now test enabling VREG with override first, then current_en, then reset latch
        printf("Turning on OVERRIDE (GPIO 18=1)...\r\n");
        gpio_put(CURRENT_EN_OVERRIDE, 1);
        busy_wait_ms(50);
        printf("With OVERRIDE: VREG ADC=%u, VOUT ADC=%u, DETECT=%d, fuse ADC=%u\r\n",
               (unsigned)amux_read(HW_ADC_MUX_VREG_OUT), (unsigned)amux_read(HW_ADC_MUX_VREF_VOUT),
               gpio_get(CURRENT_FUSE_DETECT), (unsigned)amux_read(HW_ADC_MUX_CURRENT_DETECT));

        // Enable CURRENT_EN (GPIO 19=0)
        printf("Enabling CURRENT_EN (GPIO 19=0)...\r\n");
        gpio_put(CURRENT_EN, 0);
        busy_wait_ms(5);

        // Reset the latch while OVERRIDE is still on
        printf("Resetting latch while OVERRIDE on...\r\n");
        gpio_put(CURRENT_RESET, 0);
        busy_wait_ms(5);
        gpio_put(CURRENT_RESET, 1);
        busy_wait_ms(5);
        printf("Latch reset: DETECT=%d, fuse ADC=%u\r\n",
               gpio_get(CURRENT_FUSE_DETECT), (unsigned)amux_read(HW_ADC_MUX_CURRENT_DETECT));

        // Now turn off OVERRIDE!
        printf("Releasing OVERRIDE (GPIO 18=0)...\r\n");
        gpio_put(CURRENT_EN_OVERRIDE, 0);
        busy_wait_ms(50);
        printf("Result: VREG ADC=%u, VOUT ADC=%u, DETECT=%d, fuse ADC=%u\r\n",
               (unsigned)amux_read(HW_ADC_MUX_VREG_OUT), (unsigned)amux_read(HW_ADC_MUX_VREF_VOUT),
               gpio_get(CURRENT_FUSE_DETECT), (unsigned)amux_read(HW_ADC_MUX_CURRENT_DETECT));

        psu_disable();
        printf("--- End of Diagnostic ---\r\n");
        return;
    }

    printf("Terminal TX level: %lu / %lu bytes free\r\n",
           (unsigned long)tx_fifo_level(), (unsigned long)tx_fifo_free());
    printf("Terminal dropped bytes: %lu\r\n", (unsigned long)bp_terminal_health.dropped_bytes);
    printf("Terminal overflow events: %lu\r\n", (unsigned long)bp_terminal_health.overflow_events);
    printf("Terminal high-water: %lu bytes\r\n", (unsigned long)bp_terminal_health.high_water);
    printf("Terminal drain timeouts: %lu\r\n", (unsigned long)bp_terminal_health.drain_timeouts);

    bp_bin_transport_stats_t bstat;
    bp_bin_transport_get_stats(&bstat);
    printf("Binary TX level: %lu / %lu bytes free (dropped: %lu)\r\n",
           (unsigned long)bstat.tx_level, (unsigned long)bstat.tx_free, (unsigned long)bstat.tx_dropped);
    printf("Binary RX level: %lu / %lu bytes free (dropped: %lu)\r\n",
           (unsigned long)bstat.rx_level, (unsigned long)bstat.rx_free, (unsigned long)bstat.rx_dropped);
    printf("Binary CDC1: %s (packets TX: %lu, RX: %lu)\r\n",
           bstat.cdc1_connected ? "connected" : "disconnected",
           (unsigned long)bstat.tx_packets, (unsigned long)bstat.rx_packets);

    if (psu_status.last_fuse_sample_valid) {
        printf("PSU last fuse check: ADC %u (trip < 300), direct GPIO %u\r\n",
               (unsigned)psu_status.last_fuse_adc, (unsigned)psu_status.last_fuse_gpio);
    } else {
        printf("PSU last fuse check: no sample since enable attempt\r\n");
    }
    printf("PSU startup override: VREG %u, VOUT %u, current %u, fuse %u, control bits %u\r\n",
           psu_status.startup_vreg_adc, psu_status.startup_vout_adc, psu_status.startup_current_adc,
           psu_status.startup_fuse_adc, psu_status.startup_controls);
    printf("PSU fuse failure: VREG %u, VOUT %u, current %u, control bits %u; PWM V %u I %u\r\n",
           psu_status.fault_vreg_adc, psu_status.fault_vout_adc, psu_status.fault_current_adc,
           psu_status.fault_controls, psu_status.voltage_dac_value, psu_status.current_dac_value);
    printf("PSU ADC raw: USB %u, VREG %u, VOUT %u, fuse %u, current %u\r\n",
           (unsigned)hw_adc_raw[HW_ADC_MUX_VUSB], (unsigned)hw_adc_raw[HW_ADC_MUX_VREG_OUT],
           (unsigned)hw_adc_raw[HW_ADC_MUX_VREF_VOUT], (unsigned)hw_adc_raw[HW_ADC_MUX_CURRENT_DETECT],
           (unsigned)hw_adc_raw[HW_ADC_CURRENT_SENSE]);

    printf("Mode: %s, binmode: %u\r\n",
           modes[system_config.mode].protocol_name, (unsigned)system_config.binmode_select);

    bp_protocol_telemetry_t ptelem;
    bp_protocol_get_telemetry(&ptelem);
    const char *pstate_str = (ptelem.state == BP_PROTO_STATE_ACTIVE) ? "active" :
                             (ptelem.state == BP_PROTO_STATE_FAULT) ? "fault" : "idle";
    printf("Protocol Data Plane: state: %s, ticks: %lu, transitions: %lu, faults: %lu\r\n",
           pstate_str, (unsigned long)ptelem.heartbeat_ticks,
           (unsigned long)ptelem.transitions, (unsigned long)ptelem.faults);

    printf("Watchdog: %s (pause on debug: %s)\r\n",
           bp_watchdog_health.enabled ? "enabled" : "disabled",
           bp_watchdog_health.paused_on_debug ? "yes" : "no");
    printf("Watchdog feeds: %lu\r\n", (unsigned long)bp_watchdog_health.feeds);
    printf("Core 0 ticks: %lu, missed: %lu\r\n",
           (unsigned long)bp_watchdog_health.core0_ticks,
           (unsigned long)bp_watchdog_health.core0_missed);
    printf("Core 1 ticks: %lu, missed: %lu\r\n",
           (unsigned long)bp_watchdog_health.core1_ticks,
           (unsigned long)bp_watchdog_health.core1_missed);
    printf("Core 0 stack free: %lu bytes\r\n", (unsigned long)bp_stack_get_free(0));
    printf("Core 1 stack free: %lu bytes\r\n", (unsigned long)bp_stack_get_free(1));
    if (bp_crash_record_is_valid(&bp_crash_record) && bp_crash_record.consecutive_crashes > 0) {
        printf("Consecutive crashes: %u (safe mode: %s)\r\n",
               (unsigned)bp_crash_record.consecutive_crashes,
               system_config.safe_mode ? "ACTIVE" : "inactive");
    }
}
