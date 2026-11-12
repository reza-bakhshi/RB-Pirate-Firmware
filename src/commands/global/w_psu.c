/**
 * @file w_psu.c
 * @brief Power supply (PSU) control command implementation.
 * @details Implements the w/W commands for controlling the programmable power supply:
 *          - w: Disable PSU
 *          - W: Enable PSU with interactive menu or command-line arguments
 *          
 *          Command syntax:
 *          - W <volts> <mA> [-u <percent>]
 *          - Voltage range: 0.8V to 5.0V
 *          - Current range: 0 to 500mA (0 disables current limiting)
 *          - Undervoltage: 1-100% (100 disables undervoltage protection)
 *          
 *          Features:
 *          - Overcurrent protection with automatic shutdown
 *          - Configurable undervoltage monitoring
 *          - Real-time voltage and current measurement
 *          - Integration with system status bar
 */

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pirate.h"
#include "command_struct.h"
#include "system_config.h"
#include "ui/ui_term.h"
#include "ui/ui_prompt.h"
#include "ui/ui_const.h"
#include "system_monitor.h"
#include "commands/global/w_psu.h"
#include "lib/bp_args/bp_cmd.h"
#include "pirate/psu.h"
#include "pirate/amux.h"
#include "ui/ui_help.h"
#include "usb_rx.h"
#include "kernel/bp_job.h"
#include "kernel/bp_kernel.h"
#if !defined(BP_HOST_TEST)
#include "fatfs/ff.h"
#else
#include "../../tests/stubs/storage_fs.h"
#endif

const char* const psucmd_usage[] = {
    "w|W\t[volts] [current] [-u <%%>]",
    "Disable:%s w",
    "Enable, with menu:%s W",
    "Enable 5v, 50mA fuse, 10%% undervoltage limit:%s W 5 50",
    "Enable 3.3v, 300mA default fuse, 10%% undervoltage limit:%s W 3.3",
    "Enable 3.3v, 100mA fuse, 20%% undervoltage limit:%s W 3.3 100 -u 20",
    "Enable 3.3v, no fuse, no undervoltage limit:%s W 3.3 0 -u 100",
};

static const bp_val_constraint_t voltage_range = {
    .type = BP_VAL_FLOAT,
    .f = { .min = 0.8f, .max = 5.0f, .def = 3.3f },
    .prompt = T_HELP_GCMD_W_VOLTS,
};

static const bp_val_constraint_t current_range = {
    .type = BP_VAL_FLOAT,
    .f = { .min = 0.0f, .max = 500.0f, .def = 300.0f },
    .prompt = T_HELP_GCMD_W_CURRENT_LIMIT,
};

static const bp_val_constraint_t undervoltage_range = {
    .type = BP_VAL_UINT32,
    .u = { .min = 1, .max = 100, .def = 10 },
    .prompt = 0,
};

static const bp_command_opt_t psucmd_opts[] = {
    { "undervoltage", 'u', BP_ARG_REQUIRED, "%", T_HELP_GCMD_W_UNDERVOLTAGE, &undervoltage_range },
    { 0 }
};

static const bp_command_positional_t psucmd_enable_positionals[] = {
    { "volts", NULL, T_HELP_GCMD_W_VOLTS, false, &voltage_range },
    { "current", "mA",   T_HELP_GCMD_W_CURRENT_LIMIT, false, &current_range },
    { 0 }
};

const bp_command_def_t psucmd_enable_def = {
    .name         = "W",
    .description  = T_CMDLN_PSU_EN,
    .actions      = NULL,
    .action_count = 0,
    .opts         = psucmd_opts,
    .positionals      = psucmd_enable_positionals,
    .positional_count = 2,
    .usage        = psucmd_usage,
    .usage_count  = count_of(psucmd_usage),
};

const bp_command_def_t psucmd_disable_def = {
    .name = "w",
    .description = T_CMDLN_PSU_DIS,
    .usage = psucmd_usage,
    .usage_count = count_of(psucmd_usage)
};

const char* const psucmd_cal_usage[] = {
    "w.cal\t[-d(efault)] [-s(how)]",
    "Interactive calibration wizard:%s w.cal",
    "Show current calibration:%s w.cal -s",
    "Reset to factory defaults:%s w.cal -d",
};

static const bp_command_opt_t psucmd_cal_opts[] = {
    { "default", 'd', BP_ARG_NONE, NULL, 0, NULL },
    { "show",    's', BP_ARG_NONE, NULL, 0, NULL },
    { 0 }
};

const bp_command_def_t psucmd_cal_def = {
    .name = "w.cal",
    .description = 0,
    .description_text = "PSU two-point voltage and current calibration wizard",
    .actions = NULL,
    .action_count = 0,
    .opts = psucmd_cal_opts,
    .positionals = NULL,
    .positional_count = 0,
    .usage = psucmd_cal_usage,
    .usage_count = count_of(psucmd_cal_usage),
};

const char* const psucmd_profile_usage[] = {
    "w.profile\t[volts] [current] [-f <file>] [-t <sec>]",
    "Profile active PSU to console:%s w.profile",
    "Profile with 3.3V 100mA and log to TF card:%s w.profile 3.3 100 -f log.csv",
    "Profile for 60 seconds with CSV export:%s w.profile -f run.csv -t 60",
};

static const bp_val_constraint_t profile_duration_range = {
    .type = BP_VAL_UINT32,
    .u = { .min = 1, .max = 86400, .def = 0 },
    .prompt = 0,
};

static const bp_command_opt_t psucmd_profile_opts[] = {
    { "file", 'f', BP_ARG_REQUIRED, "file", 0, NULL },
    { "time", 't', BP_ARG_REQUIRED, "seconds", 0, &profile_duration_range },
    { 0 }
};

static const bp_command_positional_t psucmd_profile_positionals[] = {
    { "volts", NULL, T_HELP_GCMD_W_VOLTS, false, &voltage_range },
    { "current", "mA", T_HELP_GCMD_W_CURRENT_LIMIT, false, &current_range },
    { 0 }
};

const bp_command_def_t psucmd_profile_def = {
    .name = "w.profile",
    .description = 0,
    .description_text = "Continuous PSU power profiler & Coulomb counter (mAh/mWh)",
    .actions = NULL,
    .action_count = 0,
    .opts = psucmd_profile_opts,
    .positionals = psucmd_profile_positionals,
    .positional_count = 2,
    .usage = psucmd_profile_usage,
    .usage_count = count_of(psucmd_profile_usage),
};

// current limit fuse tripped
void psucmd_irq_callback(void) {
    //psu_disable(); // also sets system_config.psu=
    system_config.error = true;
    system_config.info_bar_changed = true;
    system_pin_update_purpose_and_label(true, BP_VOUT, BP_PIN_VREF, ui_const_pin_states[5]);
}

// zero return code = success
uint32_t psucmd_enable(float volts, float current, bool current_limit_override, uint8_t undervoltage_percent) {
    system_config.pin_labels[0] = 0;
    system_config.pin_changed = 0xff;
    system_pin_update_purpose_and_label(false, BP_VOUT, 0, 0);

    uint32_t psu_result = psu_enable(volts, current, current_limit_override, undervoltage_percent);

    // any error codes starting the PSU?
    if (psu_result != PSU_OK) {
        psucmd_disable();
        return psu_result;
    }

    system_config.info_bar_changed = true;
    system_pin_update_purpose_and_label(true, BP_VOUT, BP_PIN_VOUT, ui_const_pin_states[1]);
    monitor_clear_current(); // reset current so the terminal status gets all characters

    return psu_result; // should be PSU OK
}

void psucmd_enable_handler(struct command_result* res) {
    float volts, current;
    bool current_limit_override = false;

    // check help
    if (bp_cmd_help_check(&psucmd_enable_def, res->help_flag)) {
        return;
    }

    char subcmd[16] = {0};
    if (bp_cmd_get_positional_string(&psucmd_enable_def, 1, subcmd, sizeof(subcmd))) {
        if (strcmp(subcmd, "cal") == 0) {
            psucmd_cal_handler(res);
            return;
        }
        if (strcmp(subcmd, "profile") == 0) {
            psucmd_profile_handler(res);
            return;
        }
    }

    // undervoltage flag: parse + validate, default if absent
    uint32_t undervoltage_percent;
    bp_cmd_status_t s = bp_cmd_flag(&psucmd_enable_def, 'u', &undervoltage_percent);
    if (s == BP_CMD_INVALID) {
        res->error = true;
        return;
    }

    // Get voltage from command line
    s = bp_cmd_positional(&psucmd_enable_def, 1, &volts);
    if (s == BP_CMD_MISSING || s == BP_CMD_INVALID) {
        // not on command line — prompt interactively
        if (bp_cmd_prompt(&voltage_range, &volts) != BP_CMD_OK) {
            res->error = true;
            return;
        }

        // interactive mode: also prompt for current
        if (bp_cmd_prompt(&current_range, &current) != BP_CMD_OK) {
            res->error = true;
            return;
        }
    } else {
        // voltage was on command line — try current from command line too
        // return default if missing, prints error if invalid
        if (bp_cmd_positional(&psucmd_enable_def, 2, &current) == BP_CMD_INVALID) {
            res->error = true;
            return;
        }
    }
    // explicit 0 for current means to disable the overcurrent limiting
    if (current == 0.0f) {
        current_limit_override = true;
    }

    uint32_t psu_result = psucmd_enable(volts, current, current_limit_override, undervoltage_percent);

    // x.xV requested, closest value: x.xV
    printf("%s%1.2f%sV%s requested, closest value: %s%1.2f%sV\r\n",
           ui_term_color_num_float(),
           psu_status.voltage_requested_float,
           ui_term_color_reset(),
           ui_term_color_info(),
           ui_term_color_num_float(),
           psu_status.voltage_actual_float,
           ui_term_color_reset());

    if (current_limit_override) {
        // current limit disabled
        printf("%s%s: %s%s\r\n",
               ui_term_color_notice(),
               GET_T(T_INFO_CURRENT_LIMIT),
               ui_term_color_reset(),
               GET_T(T_MODE_DISABLED));
    } else {
        // x.xmA requested, closest value: x.xmA
        printf("%s%1.1f%smA%s requested, closest value: %s%3.1f%smA\r\n",
               ui_term_color_num_float(),
               psu_status.current_requested_float,
               ui_term_color_reset(),
               ui_term_color_info(),
               ui_term_color_num_float(),
               psu_status.current_actual_float,
               ui_term_color_reset());
    }

    printf("%sUndervoltage limit: ", ui_term_color_notice());

    if(undervoltage_percent == 100) {
        printf("%s%s\r\n", ui_term_color_reset(), GET_T(T_MODE_DISABLED));
    } else {
        printf("%s%d.%02d%sV (%d%%)\r\n",
            ui_term_color_num_float(),
            psu_status.undervoltage_limit_int/10000,
            ( psu_status.undervoltage_limit_int%10000)/100,
            ui_term_color_reset(),
            undervoltage_percent);
    }

    // any error codes starting the PSU?
    if (psu_result != PSU_OK) {
        printf("\r\n%s%s:  %s%s\r\n",
               ui_term_color_notice(),
               GET_T(T_MODE_POWER_SUPPLY),
               ui_term_color_error(),
               GET_T(T_MODE_DISABLED));
        switch (psu_result) {
            case PSU_ERROR_FUSE_TRIPPED:
                ui_help_error(T_PSU_CURRENT_LIMIT_ERROR);
                break;
            case PSU_ERROR_VOUT_LOW:
                ui_help_error(T_PSU_SHORT_ERROR);
                break;
            case PSU_ERROR_BACKFLOW:
                printf("%s\r\nError: Vout > on-board power supply. Backflow prevention activated\r\n\tIs an external "
                       "voltage connected to Vout/Vref pin?\r\n%s",
                       ui_term_color_warning(),
                       ui_term_color_reset());
                break;
        }
        res->error = true;
        return;
    }

    // power supply: enabled
    printf("\r\n%s%s:  %s%s\r\n",
           ui_term_color_notice(),
           GET_T(T_MODE_POWER_SUPPLY),
           ui_term_color_reset(),
           GET_T(T_MODE_ENABLED));

    // Vreg output: x.xV, Vref/Vout pin: x.xV, Current sense: x.xmA
    uint32_t vout, isense, vreg;
    bool fuse;
    psu_measure(&vout, &isense, &vreg, &fuse);
    printf("%sVreg output: %s%d.%d%sV%s, Vref/Vout pin: %s%d.%d%sV%s, Current: %s%d.%d%smA%s\r\n%s",
           ui_term_color_notice(),
           ui_term_color_num_float(),
           ((vreg) / 1000),
           (((vreg) % 1000) / 100),
           ui_term_color_reset(),
           ui_term_color_notice(),
           ui_term_color_num_float(),
           ((vout) / 1000),
           (((vout) % 1000) / 100),
           ui_term_color_reset(),
           ui_term_color_notice(),
           ui_term_color_num_float(),
           (isense / 1000),
           ((isense % 1000) / 100),
           ui_term_color_reset(),
           ui_term_color_notice(),
           ui_term_color_reset());
}

// cleanup on mode exit, etc
void psucmd_disable(void) {
    //there is a chance for a race condition if an ADC conversion is ongoing when we disable the PSU
    //this causes a spurious short circuit warning because the ADC reads 0V on VOUT during the disable process
    psu_disable();
    system_pin_update_purpose_and_label(true, BP_VOUT, BP_PIN_VREF, ui_const_pin_states[0]); // change back to vref type pin
    monitor_clear_current(); // reset current so the terminal status gets all characters next time
    system_config.info_bar_changed = true;
}

void psucmd_disable_handler(struct command_result* res) {
    // check help
    if (bp_cmd_help_check(&psucmd_disable_def, res->help_flag)) {
        return;
    }

    char subcmd[16] = {0};
    if (bp_cmd_get_positional_string(&psucmd_disable_def, 1, subcmd, sizeof(subcmd))) {
        if (strcmp(subcmd, "cal") == 0) {
            psucmd_cal_handler(res);
            return;
        }
        if (strcmp(subcmd, "profile") == 0) {
            psucmd_profile_handler(res);
            return;
        }
    }

    psucmd_disable();
    printf("%s%s: %s%s\r\n",
           ui_term_color_notice(),
           GET_T(T_MODE_POWER_SUPPLY),
           ui_term_color_reset(),
           GET_T(T_MODE_DISABLED));
}

void psucmd_cal_handler(struct command_result* res) {
    if (bp_cmd_help_check(&psucmd_cal_def, res->help_flag)) {
        return;
    }

    bool opt_default = bp_cmd_find_flag(&psucmd_cal_def, 'd');
    bool opt_show = bp_cmd_find_flag(&psucmd_cal_def, 's');

    if (opt_default) {
        psu_cal_t def;
        psu_cal_reset_defaults(&def);
        psu_cal_set(&def);
        psu_cal_save(&def);
        printf("%sPSU calibration reset to nominal factory defaults and saved.%s\r\n",
               ui_term_color_notice(), ui_term_color_reset());
        return;
    }

    if (opt_show) {
        psu_cal_t cal;
        psu_cal_get(&cal);
        printf("\r\n%s--- Active PSU Calibration Profile ---%s\r\n", ui_term_color_info(), ui_term_color_reset());
        printf("  Status:            %s%s%s\r\n",
               ui_term_color_prompt(), (cal.flags & 1) ? "Calibrated (User Profile)" : "Nominal Defaults", ui_term_color_reset());
        printf("  VOUT Gain:         %s%u ppt%s (%.2f%%)\r\n",
               ui_term_color_num_float(), cal.vout_gain_ppt, ui_term_color_reset(), (float)cal.vout_gain_ppt / 10.0f);
        printf("  VOUT Offset:       %s%d mV%s\r\n",
               ui_term_color_num_float(), cal.vout_offset_mv, ui_term_color_reset());
        printf("  ISENSE Gain:       %s%u ppt%s (%.2f%%)\r\n",
               ui_term_color_num_float(), cal.isense_gain_ppt, ui_term_color_reset(), (float)cal.isense_gain_ppt / 10.0f);
        printf("  ISENSE Offset:     %s%d uA%s\r\n",
               ui_term_color_num_float(), cal.isense_offset_ua, ui_term_color_reset());
        printf("  VREG DAC Gain:     %s%u ppt%s (%.2f%%)\r\n",
               ui_term_color_num_float(), cal.vdac_gain_ppt, ui_term_color_reset(), (float)cal.vdac_gain_ppt / 10.0f);
        printf("  VREG DAC Offset:   %s%d mV%s\r\n",
               ui_term_color_num_float(), cal.vdac_offset_mv, ui_term_color_reset());
        printf("  Current DAC Gain:  %s%u ppt%s (%.2f%%)\r\n",
               ui_term_color_num_float(), cal.idac_gain_ppt, ui_term_color_reset(), (float)cal.idac_gain_ppt / 10.0f);
        printf("  Current DAC Offset:%s%d mA%s\r\n",
               ui_term_color_num_float(), cal.idac_offset_ma, ui_term_color_reset());
        return;
    }

    // Interactive Two-Point Wizard
    printf("\r\n%s=== Bus Pirate PSU Two-Point Calibration Wizard ===%s\r\n", ui_term_color_info(), ui_term_color_reset());
    printf("This wizard computes gain and offset corrections for voltage & current sensing.\r\n\r\n");

    // Ensure PSU is disabled for zero point
    psucmd_disable();

    printf("%sStep 1: Zero-point offset measurement%s\r\n", ui_term_color_notice(), ui_term_color_reset());
    printf("Disconnect any circuit, external voltage, or load from VOUT/VREF pins.\r\n");
    if (bp_cmd_yes_no_exit("Ready to measure zero point?") != BP_YN_YES) {
        printf("Calibration cancelled.\r\n");
        return;
    }

    // Sample 16 iterations
    uint32_t zero_vout_sum = 0;
    uint32_t zero_isense_sum = 0;
    for (int i = 0; i < 16; i++) {
        uint32_t vout, isense, vreg;
        bool fuse;
        psu_measure(&vout, &isense, &vreg, &fuse);
        zero_vout_sum += vout;
        zero_isense_sum += isense;
        busy_wait_ms(10);
    }
    int16_t vout_offset = (int16_t)-(zero_vout_sum / 16);
    int32_t isense_offset = (int32_t)-(zero_isense_sum / 16);

    printf("Zero offsets measured: VOUT offset = %d mV, ISENSE offset = %ld uA\r\n\r\n",
           (int)vout_offset, (long)isense_offset);

    printf("%sStep 2: Reference voltage measurement%s\r\n", ui_term_color_notice(), ui_term_color_reset());
    printf("Connect a digital multimeter (DMM) between VOUT and GND.\r\n");
    printf("The PSU will now energize at 3.30V (300mA current limit).\r\n");
    if (bp_cmd_yes_no_exit("Ready to energize output?") != BP_YN_YES) {
        printf("Calibration cancelled.\r\n");
        return;
    }

    uint32_t psu_err = psucmd_enable(3.3f, 300.0f, false, 10);
    if (psu_err != PSU_OK) {
        printf("%sError: Failed to enable PSU (%u)%s\r\n", ui_term_color_error(), (unsigned)psu_err, ui_term_color_reset());
        res->error = true;
        return;
    }

    // Allow settling
    busy_wait_ms(200);

    // Sample raw measurement under load
    uint32_t ref_vout_sum = 0;
    for (int i = 0; i < 16; i++) {
        uint32_t vout, isense, vreg;
        bool fuse;
        psu_measure(&vout, &isense, &vreg, &fuse);
        ref_vout_sum += vout;
        busy_wait_ms(10);
    }
    uint32_t measured_raw_vout = ref_vout_sum / 16;

    // Prompt user for actual voltage reading from multimeter
    static const bp_val_constraint_t dmm_volts_con = {
        .type = BP_VAL_FLOAT,
        .f = { .min = 2.5f, .max = 4.0f, .def = 3.30f },
        .prompt = 0
    };
    float dmm_volts = 3.30f;
    printf("\r\nEnter actual voltage reading from your multimeter (V) [3.30]: ");
    if (bp_cmd_prompt(&dmm_volts_con, &dmm_volts) != BP_CMD_OK) {
        psucmd_disable();
        printf("Calibration cancelled.\r\n");
        return;
    }

    // Safe to disable PSU now
    psucmd_disable();

    // Compute coefficients
    uint32_t target_nominal_mv = 3300;
    uint32_t actual_dmm_mv = (uint32_t)(dmm_volts * 1000.0f);
    uint16_t vdac_gain = (uint16_t)((actual_dmm_mv * 1000UL) / target_nominal_mv);

    int32_t adj_raw_vout = (int32_t)measured_raw_vout + (int32_t)vout_offset;
    if (adj_raw_vout <= 0) adj_raw_vout = 1;
    uint16_t vout_gain = (uint16_t)(((uint64_t)actual_dmm_mv * 1000ULL) / (uint32_t)adj_raw_vout);

    psu_cal_t new_cal;
    psu_cal_reset_defaults(&new_cal);
    new_cal.vout_gain_ppt = vout_gain;
    new_cal.vout_offset_mv = vout_offset;
    new_cal.isense_gain_ppt = 1000;
    new_cal.isense_offset_ua = (int16_t)isense_offset;
    new_cal.vdac_gain_ppt = vdac_gain;
    new_cal.vdac_offset_mv = 0;
    new_cal.idac_gain_ppt = 1000;
    new_cal.idac_offset_ma = 0;
    new_cal.flags = 1;
    new_cal.checksum = psu_cal_calc_checksum(&new_cal);

    printf("\r\n%sComputed Calibration Coefficients:%s\r\n", ui_term_color_info(), ui_term_color_reset());
    printf("  VOUT Gain:         %u ppt (%.2f%%)\r\n", new_cal.vout_gain_ppt, (float)new_cal.vout_gain_ppt / 10.0f);
    printf("  VOUT Offset:       %d mV\r\n", new_cal.vout_offset_mv);
    printf("  ISENSE Offset:     %d uA\r\n", new_cal.isense_offset_ua);
    printf("  VREG DAC Gain:     %u ppt (%.2f%%)\r\n", new_cal.vdac_gain_ppt, (float)new_cal.vdac_gain_ppt / 10.0f);

    if (bp_cmd_yes_no_exit("Save calibration to /config/psu_cal.json?") == BP_YN_YES) {
        if (psu_cal_save(&new_cal)) {
            printf("%sCalibration profile successfully saved and applied!%s\r\n",
                   ui_term_color_prompt(), ui_term_color_reset());
        } else {
            psu_cal_set(&new_cal);
            printf("%sWarning: Applied calibration in RAM, but could not write to TF storage.%s\r\n",
                   ui_term_color_warning(), ui_term_color_reset());
        }
    } else {
        printf("Calibration discarded.\r\n");
    }
}

void psucmd_profile_handler(struct command_result* res) {
    if (bp_cmd_help_check(&psucmd_profile_def, res->help_flag)) {
        return;
    }

    char file_path[64] = {0};
    bool has_file = bp_cmd_get_string(&psucmd_profile_def, 'f', file_path, sizeof(file_path));

    uint32_t duration_s = 0;
    bp_cmd_get_uint32(&psucmd_profile_def, 't', &duration_s);

    float volts = 0.0f, current = 0.0f;
    bool has_volts = (bp_cmd_positional(&psucmd_profile_def, 1, &volts) == BP_CMD_OK);
    if (has_volts) {
        bp_cmd_positional(&psucmd_profile_def, 2, &current);
        if (current == 0.0f) current = 300.0f;
        uint32_t st = psucmd_enable(volts, current, false, 10);
        if (st != PSU_OK) {
            printf("%sError: Failed to enable PSU at %1.2fV%s\r\n",
                   ui_term_color_error(), volts, ui_term_color_reset());
            res->error = true;
            return;
        }
    } else if (!psu_status.enabled) {
        printf("PSU is currently disabled.\r\n");
        if (bp_cmd_yes_no_exit("Enable PSU at 3.3V 300mA?") == BP_YN_YES) {
            uint32_t st = psucmd_enable(3.3f, 300.0f, false, 10);
            if (st != PSU_OK) {
                printf("%sError: Failed to enable PSU%s\r\n", ui_term_color_error(), ui_term_color_reset());
                res->error = true;
                return;
            }
        } else {
            printf("Cannot profile while PSU is disabled.\r\n");
            return;
        }
    }

    FIL fil;
    bool file_open = false;
    char csv_buf[512];
    uint32_t csv_buf_pos = 0;

    if (has_file) {
        FRESULT fr = f_open(&fil, file_path, FA_CREATE_ALWAYS | FA_WRITE);
        if (fr == FR_OK) {
            file_open = true;
            const char* header = "timestamp_ms,voltage_v,current_ma,power_mw,charge_mah,energy_mwh\r\n";
            UINT bw = 0;
            f_write(&fil, header, strlen(header), &bw);
            f_sync(&fil);
            printf("%sLogging CSV telemetry to: %s%s\r\n", ui_term_color_notice(), file_path, ui_term_color_reset());
        } else {
            printf("%sWarning: Could not open file '%s' for CSV export (code %d)%s\r\n",
                   ui_term_color_warning(), file_path, fr, ui_term_color_reset());
        }
    }

    printf("%sStarting continuous power profiler. Press 'q', 'x', or ESC to stop.%s\r\n\r\n",
           ui_term_color_info(), ui_term_color_reset());

    system_config.terminal_toolbar_pause = true;

    uint32_t start_us = time_us_32();
    uint32_t last_us = start_us;
    uint32_t last_ui_us = start_us;
    uint32_t last_csv_us = start_us;

    uint64_t charge_uas_frac = 0;
    uint64_t energy_uws_frac = 0;
    uint32_t peak_current_ua = 0;
    uint32_t min_voltage_mv = 0xFFFFFFFF;
    uint32_t peak_power_uw = 0;
    uint32_t sample_count = 0;
    uint64_t sum_current_ua = 0;
    uint64_t sum_power_uw = 0;

    while (true) {
        bp_service(0);

        uint32_t now_us = time_us_32();
        uint32_t dt_us = now_us - last_us;
        last_us = now_us;

        uint32_t vout_mv, isense_ua, vreg_mv;
        bool fuse_ok;
        psu_measure(&vout_mv, &isense_ua, &vreg_mv, &fuse_ok);

        if (!fuse_ok || psu_poll_fuse_vout_error()) {
            printf("\r\n%sError: PSU fault detected! Profiling aborted.%s\r\n",
                   ui_term_color_error(), ui_term_color_reset());
            break;
        }

        uint32_t power_uw = (uint32_t)(((uint64_t)vout_mv * (uint64_t)isense_ua) / 1000ULL);

        charge_uas_frac += (uint64_t)isense_ua * (uint64_t)dt_us;
        energy_uws_frac += (uint64_t)power_uw * (uint64_t)dt_us;
        sample_count++;
        sum_current_ua += isense_ua;
        sum_power_uw += power_uw;

        if (isense_ua > peak_current_ua) peak_current_ua = isense_ua;
        if (vout_mv < min_voltage_mv) min_voltage_mv = vout_mv;
        if (power_uw > peak_power_uw) peak_power_uw = power_uw;

        // UI update every 100ms
        if ((uint32_t)(now_us - last_ui_us) >= 100000) {
            last_ui_us = now_us;
            uint32_t elapsed_s = (now_us - start_us) / 1000000UL;
            uint32_t hours = elapsed_s / 3600;
            uint32_t mins = (elapsed_s % 3600) / 60;
            uint32_t secs = elapsed_s % 60;

            uint32_t mah_int = (uint32_t)(charge_uas_frac / 3600000000000ULL);
            uint32_t mah_frac = (uint32_t)((charge_uas_frac / 3600000000ULL) % 1000ULL);
            uint32_t mwh_int = (uint32_t)(energy_uws_frac / 3600000000000ULL);
            uint32_t mwh_frac = (uint32_t)((energy_uws_frac / 3600000000ULL) % 1000ULL);

            printf("\rTime: %02u:%02u:%02u | V: %1.3fV | I: %3.1fmA | P: %4.1fmW | Q: %u.%03umAh | E: %u.%03umWh   ",
                   (unsigned)hours, (unsigned)mins, (unsigned)secs,
                   (float)vout_mv / 1000.0f,
                   (float)isense_ua / 1000.0f,
                   (float)power_uw / 1000.0f,
                   (unsigned)mah_int, (unsigned)mah_frac,
                   (unsigned)mwh_int, (unsigned)mwh_frac);
            fflush(stdout);
        }

        // CSV logging stream every 100ms
        if (file_open && (uint32_t)(now_us - last_csv_us) >= 100000) {
            last_csv_us = now_us;
            uint32_t elapsed_ms = (now_us - start_us) / 1000UL;
            uint32_t mah_int = (uint32_t)(charge_uas_frac / 3600000000000ULL);
            uint32_t mah_frac = (uint32_t)((charge_uas_frac / 3600000000ULL) % 1000ULL);
            uint32_t mwh_int = (uint32_t)(energy_uws_frac / 3600000000000ULL);
            uint32_t mwh_frac = (uint32_t)((energy_uws_frac / 3600000000ULL) % 1000ULL);

            char line[128];
            int n = snprintf(line, sizeof(line), "%lu,%1.3f,%1.2f,%1.2f,%u.%03u,%u.%03u\r\n",
                             (unsigned long)elapsed_ms,
                             (float)vout_mv / 1000.0f,
                             (float)isense_ua / 1000.0f,
                             (float)power_uw / 1000.0f,
                             (unsigned)mah_int, (unsigned)mah_frac,
                             (unsigned)mwh_int, (unsigned)mwh_frac);
            if (n > 0) {
                if (csv_buf_pos + (unsigned)n >= sizeof(csv_buf)) {
                    UINT bw = 0;
                    f_write(&fil, csv_buf, csv_buf_pos, &bw);
                    f_sync(&fil);
                    csv_buf_pos = 0;
                }
                memcpy(csv_buf + csv_buf_pos, line, n);
                csv_buf_pos += n;
            }
        }

        if (duration_s > 0 && (now_us - start_us) >= (duration_s * 1000000UL)) {
            break;
        }

        char c = 0;
        if (rx_fifo_try_get(&c) || bp_job_check_cancelled()) {
            if (c == 'q' || c == 'x' || c == 0x1B || c == 0x03 || c == ' ') {
                break;
            }
        }

        busy_wait_ms(50);
    }

    if (file_open) {
        if (csv_buf_pos > 0) {
            UINT bw = 0;
            f_write(&fil, csv_buf, csv_buf_pos, &bw);
        }
        f_sync(&fil);
        f_close(&fil);
    }

    system_config.terminal_toolbar_pause = false;

    uint32_t total_us = time_us_32() - start_us;
    uint32_t total_s = total_us / 1000000UL;
    uint32_t final_mah_int = (uint32_t)(charge_uas_frac / 3600000000000ULL);
    uint32_t final_mah_frac = (uint32_t)((charge_uas_frac / 3600000000ULL) % 1000ULL);
    uint32_t final_mwh_int = (uint32_t)(energy_uws_frac / 3600000000000ULL);
    uint32_t final_mwh_frac = (uint32_t)((energy_uws_frac / 3600000000ULL) % 1000ULL);

    float avg_current_ma = sample_count ? ((float)(sum_current_ua / sample_count) / 1000.0f) : 0.0f;
    float avg_power_mw = sample_count ? ((float)(sum_power_uw / sample_count) / 1000.0f) : 0.0f;

    printf("\r\n\r\n%s=== PSU Power Profile Summary ===%s\r\n", ui_term_color_info(), ui_term_color_reset());
    printf("  Duration:        %02u:%02u:%02u (%lu s)\r\n",
           (unsigned)(total_s / 3600), (unsigned)((total_s % 3600) / 60), (unsigned)(total_s % 60), (unsigned long)total_s);
    printf("  Total Charge:    %s%u.%03u mAh%s\r\n",
           ui_term_color_num_float(), (unsigned)final_mah_int, (unsigned)final_mah_frac, ui_term_color_reset());
    printf("  Total Energy:    %s%u.%03u mWh%s\r\n",
           ui_term_color_num_float(), (unsigned)final_mwh_int, (unsigned)final_mwh_frac, ui_term_color_reset());
    printf("  Average Current: %s%1.2f mA%s\r\n",
           ui_term_color_num_float(), avg_current_ma, ui_term_color_reset());
    printf("  Peak Current:    %s%1.2f mA%s\r\n",
           ui_term_color_num_float(), (float)peak_current_ua / 1000.0f, ui_term_color_reset());
    printf("  Average Power:   %s%1.2f mW%s\r\n",
           ui_term_color_num_float(), avg_power_mw, ui_term_color_reset());
    printf("  Peak Power:      %s%1.2f mW%s\r\n",
           ui_term_color_num_float(), (float)peak_power_uw / 1000.0f, ui_term_color_reset());
    if (min_voltage_mv != 0xFFFFFFFF) {
        printf("  Min Output V:    %s%1.3f V%s\r\n",
               ui_term_color_num_float(), (float)min_voltage_mv / 1000.0f, ui_term_color_reset());
    }
    if (file_open) {
        printf("  Exported File:   %s%s%s\r\n", ui_term_color_notice(), file_path, ui_term_color_reset());
    }
}

bool psucmd_init(void) {
    psu_init();
    return true;
}

void psucmd_show_clear_error(void) {
    if (psu_status.error_pending && (psu_status.error_overcurrent || psu_status.error_undervoltage)){
        psu_clear_error_flag();
        ui_term_screen_flash(true);
        printf("\r\n");
        if(psu_status.error_overcurrent) {
            ui_help_error(T_PSU_CURRENT_LIMIT_ERROR);
        }
        if(psu_status.error_undervoltage) {
            ui_help_error(T_PSU_SHORT_ERROR);
        }
        busy_wait_ms(500);
        ui_term_screen_flash(false);
    }
}
