/**
 * @file psu.c
 * @brief Power supply unit (PSU) control implementation.
 * @details Manages programmable voltage regulator (VREG) and current limiting circuitry.
 *          Supports both PWM-based (BP5 Rev8/Rev9) and I2C DAC-based (BP5 Rev10+) control.
 *          Features include:
 *          - Voltage range: 0.8V to 5.0V with 12-bit resolution
 *          - Current limiting: 0 to 500mA with overcurrent protection
 *          - Undervoltage monitoring with configurable threshold
 *          - Backflow detection
 *          - Automatic fault detection and shutdown
 */

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "pirate.h"
#include "pirate/psu.h"
#include "pirate/amux.h"
#include "kernel/bp_job.h"
#include "kernel/bp_config.h"
#include "mjson/mjson.h"
#if !defined(BP_HOST_TEST)
#include "fatfs/ff.h"
#include "system_config.h"
#else
#include "../../tests/stubs/storage_fs.h"
#endif
#if BP_HW_PSU_DAC
    #include "hardware/i2c.h"
#endif

#if BP_HW_PSU_DAC
    #define PWM_TOP 0x0fff //12 bit DAC
#else
    #define PWM_TOP 14000 // 0x30D3
#endif

// voltage settings
#define PSU_V_LOW 800   // millivolts
#define PSU_V_HIGH 5000 // millivolts
#define PSU_V_RANGE ((PSU_V_HIGH * 10) - (PSU_V_LOW * 10))

// current settings
#define PSU_I_LOW 0    // mA
#define PSU_I_HIGH 500 // mA
#define PSU_I_RANGE ((PSU_I_HIGH * 10000) - (PSU_I_LOW * 10000))

struct psu_status_t psu_status;

bool psu_cal_is_valid(const psu_cal_t *cal) {
    if (!cal) return false;
    if (cal->magic != PSU_CAL_MAGIC) return false;
    if (cal->vout_gain_ppt < 500 || cal->vout_gain_ppt > 2000) return false;
    if (cal->isense_gain_ppt < 500 || cal->isense_gain_ppt > 2000) return false;
    if (cal->vdac_gain_ppt < 500 || cal->vdac_gain_ppt > 2000) return false;
    if (cal->idac_gain_ppt < 500 || cal->idac_gain_ppt > 2000) return false;
    if (cal->vout_offset_mv < -2000 || cal->vout_offset_mv > 2000) return false;
    if (cal->isense_offset_ua < -30000 || cal->isense_offset_ua > 30000) return false;
    if (cal->checksum != psu_cal_calc_checksum(cal)) return false;
    return true;
}

void psu_cal_get(psu_cal_t *out_cal) {
    if (!out_cal) return;
    const bp_runtime_t *rt = bp_runtime_get();
    if (rt && psu_cal_is_valid(&rt->psu_cal)) {
        *out_cal = rt->psu_cal;
    } else {
        psu_cal_reset_defaults(out_cal);
    }
}

bool psu_cal_set(const psu_cal_t *new_cal) {
    if (!new_cal || !psu_cal_is_valid(new_cal)) return false;
    bp_runtime_t *rt = bp_runtime_get_mutable();
    if (!rt) return false;
    rt->psu_cal = *new_cal;
    return true;
}

bool psu_cal_save(const psu_cal_t *cal) {
    if (!cal || !psu_cal_is_valid(cal)) {
        return false;
    }

    f_mkdir("config");
    f_mkdir("/config");

    FIL fil;
    FRESULT fr = f_open(&fil, PSU_CAL_FILE, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK) {
        fr = f_open(&fil, PSU_CAL_ALT_FILE, FA_CREATE_ALWAYS | FA_WRITE);
    }
    if (fr != FR_OK) {
        return false;
    }

    char buf[256];
    int len = snprintf(buf, sizeof(buf),
        "{\n"
        "  \"vout_gain_ppt\": %u,\n"
        "  \"vout_offset_mv\": %d,\n"
        "  \"isense_gain_ppt\": %u,\n"
        "  \"isense_offset_ua\": %d,\n"
        "  \"vdac_gain_ppt\": %u,\n"
        "  \"vdac_offset_mv\": %d,\n"
        "  \"idac_gain_ppt\": %u,\n"
        "  \"idac_offset_ma\": %d\n"
        "}\n",
        (unsigned)cal->vout_gain_ppt,
        (int)cal->vout_offset_mv,
        (unsigned)cal->isense_gain_ppt,
        (int)cal->isense_offset_ua,
        (unsigned)cal->vdac_gain_ppt,
        (int)cal->vdac_offset_mv,
        (unsigned)cal->idac_gain_ppt,
        (int)cal->idac_offset_ma
    );

    UINT bw = 0;
    fr = f_write(&fil, buf, (UINT)len, &bw);
    f_sync(&fil);
    f_close(&fil);

    if (fr == FR_OK && bw == (UINT)len) {
        psu_cal_set(cal);
        return true;
    }
    return false;
}

bool psu_cal_load(void) {
    FIL fil;
    FRESULT fr = f_open(&fil, PSU_CAL_FILE, FA_READ | FA_OPEN_EXISTING);
    if (fr != FR_OK) {
        fr = f_open(&fil, PSU_CAL_ALT_FILE, FA_READ | FA_OPEN_EXISTING);
    }
    if (fr != FR_OK) {
        fr = f_open(&fil, "psu_cal.json", FA_READ | FA_OPEN_EXISTING);
    }
    if (fr != FR_OK) {
        fr = f_open(&fil, "/psu_cal.json", FA_READ | FA_OPEN_EXISTING);
    }
    if (fr != FR_OK) {
        psu_cal_t def;
        psu_cal_reset_defaults(&def);
        psu_cal_set(&def);
        return false;
    }

    char json[512];
    UINT br = 0;
    fr = f_read(&fil, json, sizeof(json) - 1, &br);
    f_close(&fil);

    if (fr != FR_OK || br == 0) {
        psu_cal_t def;
        psu_cal_reset_defaults(&def);
        psu_cal_set(&def);
        return false;
    }
    json[br] = '\0';

    psu_cal_t loaded;
    psu_cal_reset_defaults(&loaded);

    double val = 0;
    if (mjson_get_number(json, br, "$.vout_gain_ppt", &val)) {
        loaded.vout_gain_ppt = (uint16_t)val;
    }
    if (mjson_get_number(json, br, "$.vout_offset_mv", &val)) {
        loaded.vout_offset_mv = (int16_t)val;
    }
    if (mjson_get_number(json, br, "$.isense_gain_ppt", &val)) {
        loaded.isense_gain_ppt = (uint16_t)val;
    }
    if (mjson_get_number(json, br, "$.isense_offset_ua", &val)) {
        loaded.isense_offset_ua = (int16_t)val;
    }
    if (mjson_get_number(json, br, "$.vdac_gain_ppt", &val)) {
        loaded.vdac_gain_ppt = (uint16_t)val;
    }
    if (mjson_get_number(json, br, "$.vdac_offset_mv", &val)) {
        loaded.vdac_offset_mv = (int16_t)val;
    }
    if (mjson_get_number(json, br, "$.idac_gain_ppt", &val)) {
        loaded.idac_gain_ppt = (uint16_t)val;
    }
    if (mjson_get_number(json, br, "$.idac_offset_ma", &val)) {
        loaded.idac_offset_ma = (int16_t)val;
    }

    loaded.flags = 1;
    loaded.checksum = psu_cal_calc_checksum(&loaded);

    if (psu_cal_is_valid(&loaded)) {
        psu_cal_set(&loaded);
        return true;
    } else {
        psu_cal_t def;
        psu_cal_reset_defaults(&def);
        psu_cal_set(&def);
        return false;
    }
}

/// @brief Reset the PSU current fuse/trigger
/// @param  None
/// @return None
static void psu_fuse_reset(void) {
    // reset current trigger
    #if BP_HW_IOEXP_595
        shift_clear_set_wait(CURRENT_RESET, 0); // low to activate the pnp
        busy_wait_ms(5);
        shift_clear_set_wait(0, CURRENT_RESET); // high to disable    
    #elif BP_HW_IOEXP_NONE
        gpio_put(CURRENT_RESET, 0);
        busy_wait_ms(5);
        gpio_put(CURRENT_RESET, 1);
    #else
        #error "Platform not speficied in psu.c"
    #endif
}

/// @brief Enable or disable the PSU VREG/current limit connection
/// @param  enable  true to enable VREG/current connection, false to disable
/// @return None
void psu_vreg_enable(bool enable) {
    #if BP_HW_IOEXP_595
        if (enable) {
            shift_clear_set_wait(CURRENT_EN, 0); // low is on (PNP)
        } else {
            shift_clear_set_wait(0, CURRENT_EN); // high is off
        }    
    #elif BP_HW_IOEXP_NONE
        gpio_put(CURRENT_EN, !enable);
    #else
        #error "Platform not speficied in psu.c"
    #endif
}

/// @brief Override or restore the current limiting circuitry
/// @param  enable  true to disable current limiting (override), false to enable limiting
/// @return None
void psu_current_limit_override(bool enable) {
    #if BP_HW_IOEXP_595
        if (enable) {
            shift_clear_set_wait(0, CURRENT_EN_OVERRIDE);
        } else {
            shift_clear_set_wait(CURRENT_EN_OVERRIDE, 0);
        }
    #elif BP_HW_IOEXP_NONE
        gpio_put(CURRENT_EN_OVERRIDE, enable);
    #else
        #error "Platform not speficied in psu.c"
    #endif
    psu_status.current_limit_override = enable;
}

/// @brief Calculate and set voltage-related fields in the PSU status
/// @param  volts  Requested voltage in volts (float)
/// @param  psu    Pointer to a `psu_status_t` struct to update
/// @return None
void psu_set_v(float volts, struct psu_status_t* psu) {
    psu_cal_t cal;
    psu_cal_get(&cal);

    float target_mv = volts * 1000.0f;
    float cal_v_mv = ((target_mv - (float)cal.vdac_offset_mv) * 1000.0f) / (float)cal.vdac_gain_ppt;
    if (cal_v_mv < (float)PSU_V_LOW) cal_v_mv = (float)PSU_V_LOW;
    if (cal_v_mv > (float)PSU_V_HIGH) cal_v_mv = (float)PSU_V_HIGH;

    uint32_t psu_v_per_bit = ((PSU_V_RANGE) / PWM_TOP);
    uint32_t vset = (uint32_t)(cal_v_mv * 10.0f);
    if (vset >= (PSU_V_LOW * 10)) {
        vset = vset - (PSU_V_LOW * 10);
    } else {
        vset = 0;
    }
    vset = vset / psu_v_per_bit;
    if (vset > PWM_TOP) {
        vset = PWM_TOP + 1;
    }

    psu->voltage_requested_float = volts;
    psu->voltage_actual_int = ((vset * psu_v_per_bit) + (PSU_V_LOW * 10));
    psu->voltage_actual_float = (float)((float)((vset * psu_v_per_bit) + (PSU_V_LOW * 10)) / (float)10000);
    // inverse for VREG margining
    psu->voltage_dac_value = (PWM_TOP - vset);
}

/// @brief Calculate and set current-related fields in the PSU status
/// @param  current  Requested current in mA (float)
/// @param  psu      Pointer to a `psu_status_t` struct to update
/// @return None
void psu_set_i(float current, struct psu_status_t* psu) {
    psu_cal_t cal;
    psu_cal_get(&cal);

    float cal_i_ma = ((current - (float)cal.idac_offset_ma) * 1000.0f) / (float)cal.idac_gain_ppt;
    if (cal_i_ma < (float)PSU_I_LOW) cal_i_ma = (float)PSU_I_LOW;
    if (cal_i_ma > (float)PSU_I_HIGH) cal_i_ma = (float)PSU_I_HIGH;

    float psu_i_per_bit = ((float)(PSU_I_RANGE) / (float)PWM_TOP);
    float iset = (float)(cal_i_ma * 10000.0f);
    iset /= psu_i_per_bit;
    float iact = (float)((float)iset * psu_i_per_bit) / 10000.0f;

    psu->current_dac_value = (uint32_t)iset;
    psu->current_requested_float = current;
    psu->current_actual_int = (uint32_t)((float)iset * (float)psu_i_per_bit);
    psu->current_actual_float = iact;
}

/// @brief Apply DAC/PWM values for voltage and current outputs
/// @param  v_dac  Voltage DAC/PWM value
/// @param  i_dac  Current DAC/PWM value
/// @return None
void psu_dac_set(uint16_t v_dac, uint16_t i_dac) {
    #if BP_HW_PSU_PWM
        uint slice_num = pwm_gpio_to_slice_num(PSU_PWM_VREG_ADJ);
        uint v_chan_num = pwm_gpio_to_channel(PSU_PWM_VREG_ADJ);
        uint i_chan_num = pwm_gpio_to_channel(PSU_PWM_CURRENT_ADJ);
        pwm_set_chan_level(slice_num, v_chan_num, v_dac);
        pwm_set_chan_level(slice_num, i_chan_num, i_dac);
    #elif BP_HW_PSU_DAC
        //I2C dac
        //voltage dac
        const uint8_t v_dac_address = 0xc2;
        uint8_t dac[2];
        dac[0] = (v_dac >> 8) & 0xF;
        dac[1] = v_dac & 0xFF;
        if(i2c_write_blocking(BP_I2C_PORT, v_dac_address, dac, 2, false) == PICO_ERROR_GENERIC){
            printf("I2C write error\n");
        } 
        //current dac
        const uint8_t i_dac_address = 0xc0;
        dac[0] = (i_dac >> 8) & 0xF;
        dac[1] = i_dac & 0xFF;
        if(i2c_write_blocking(BP_I2C_PORT, i_dac_address, dac, 2, false) == PICO_ERROR_GENERIC){
            printf("I2C write error\n");
        }
    #else
        #error "Platform not speficied in psu.c"
    #endif
    // printf("GPIO: %d, slice: %d, v_chan: %d, i_chan: %d",PSU_PWM_VREG_ADJ,slice_num,v_chan_num,i_chan_num);
}

// deal with floating limit better (eg 0.7)
/// @brief Configure undervoltage alarm threshold in the PSU status
/// @param  psu                   Pointer to a `psu_status_t` struct to update
/// @param  undervoltage_percent  Undervoltage threshold as percent (1-100), 100 to disable
/// @return None
void psu_set_undervoltage_limit(struct psu_status_t* psu, uint8_t undervoltage_percent) {
    if (undervoltage_percent == 100) {
        psu->undervoltage_limit_override = true;
    }else{
        psu->undervoltage_limit_override = false;
    }
    psu->undervoltage_percent = undervoltage_percent;

    // calculate undervoltage drop in 0.1mV units
    uint32_t drop = (uint32_t)((float)psu->voltage_actual_int * ((float)undervoltage_percent / 100.0f));

    // Ensure minimum noise margin of 200mV (2000 in 0.1mV units) at lower voltages
    // so PWM ripple (~50mV) and ADC noise do not cause spurious undervoltage trips,
    // while capping drop to at most half the output voltage on very low rails.
    if (drop < 2000) {
        drop = 2000;
        if (drop > (psu->voltage_actual_int / 2)) {
            drop = psu->voltage_actual_int / 2;
        }
    }

    if (drop >= psu->voltage_actual_int) {
        psu->undervoltage_limit_int = 0;
    } else {
        psu->undervoltage_limit_int = psu->voltage_actual_int - drop;
    }

    // calculate raw ADC counts for fast comparison: adc = (mV * 4096) / 66000
    psu->undervoltage_limit_adc = (uint16_t)((psu->undervoltage_limit_int * 4096UL) / 66000UL);
}

/// @brief Check whether the PSU current fuse/limit indicates OK
/// @param  None
/// @return true if fuse/current detection indicates OK, false if blown/overcurrent
bool psu_fuse_ok(void) {
    if(psu_status.error_overcurrent) {
        return false;
    }
    uint32_t fuse = amux_read(HW_ADC_MUX_CURRENT_DETECT);
    psu_status.last_fuse_adc = (uint16_t)fuse;
    psu_status.last_fuse_sample_valid = true;
#ifdef CURRENT_FUSE_DETECT
    psu_status.last_fuse_gpio = gpio_get(CURRENT_FUSE_DETECT);
    // Fuse is OK when CURRENT_DETECT is HIGH (> 300 ADC counts and/or direct GPIO HIGH)
    return (fuse > 300) || psu_status.last_fuse_gpio;
#else
    return (fuse > 300);
#endif
}

/// @brief Check whether Vout is above the configured undervoltage limit
/// @param  None
/// @return true if Vout is OK, false if undervoltage detected
bool psu_vout_ok(void) {
    if(psu_status.undervoltage_limit_override) {
        return true;
    }
    if(psu_status.error_undervoltage) {
        return false;
    }
    // Average 8 samples to filter single-shot ADC noise and PWM ripple
    uint32_t vout = 0;
    for (int i = 0; i < 8; i++) {
        vout += amux_read(HW_ADC_MUX_VREF_VOUT);
    }
    vout /= 8;
    // compare raw ADC counts directly - no runtime math needed
    return (vout >= psu_status.undervoltage_limit_adc);
}

#define PSU_FAULT_DEBOUNCE_COUNT 4
static uint8_t s_undervoltage_fail_count = 0;

/// @brief Poll PSU sensors for fuse or Vout errors and handle them
/// @param  None
/// @return true if an error was detected and handled, false otherwise
bool psu_poll_fuse_vout_error(void) {
    if(!psu_status.enabled) {
        s_undervoltage_fail_count = 0;
        return false;
    }

#ifndef BP_HOST_TEST
    // Rate-limit polling to 10 ms (100 Hz) to avoid bus flooding and high-frequency mux switching
    static uint32_t s_last_poll_us = 0;
    uint32_t now_us = time_us_32();
    if ((uint32_t)(now_us - s_last_poll_us) < 10000) {
        return false;
    }
    s_last_poll_us = now_us;
#endif

    bool error = false;
    if (!psu_fuse_ok()) {
        psu_status.error_overcurrent = true;
        error = true;
    }
    if (!psu_vout_ok()) {
        if (++s_undervoltage_fail_count >= PSU_FAULT_DEBOUNCE_COUNT) {
            psu_status.error_undervoltage = true;
            error = true;
        }
    } else {
        s_undervoltage_fail_count = 0;
    }
    // Re-check enabled flag to avoid race condition with psu_disable() on other core
    // If PSU was intentionally disabled between our initial check and now, don't flag as error
    if(error && psu_status.enabled) {
        psu_disable();
        psu_status.error_pending = true;
        uint32_t active_job = bp_job_get_active_id();
        if (active_job != 0) {
            bp_job_cancel(active_job);
        }
    } else if (!error) {
        // PSU was disabled intentionally or no error, clear any false error flags we may have set
        psu_status.error_overcurrent = false;
        psu_status.error_undervoltage = false;
    }
    return error;
}

/// @brief Clear the pending PSU error flag
/// @param  None
/// @return None
void psu_clear_error_flag(void) {
    psu_status.error_pending = false;
    s_undervoltage_fail_count = 0;
}

/// @brief Check for backflow condition between VREG and VOUT
/// @param  None
/// @return true if backflow is not detected, false if backflow present
bool psu_backflow_ok(void) {
    // Average 8 samples to filter single-shot ADC noise
    uint32_t vout = 0;
    uint32_t vreg = 0;
    for (int i = 0; i < 8; i++) {
        vout += amux_read(HW_ADC_MUX_VREF_VOUT);
        vreg += amux_read(HW_ADC_MUX_VREG_OUT);
    }
    vout /= 8;
    vreg /= 8;
    // Allow up to ~320mV (200 ADC counts) for resistor divider tolerances and diode drop.
    // Real external backflow (e.g. 5V or external 3.3V into a lower rail) exceeds 500+ counts.
    return (vout < (vreg + 200));
}

/// @brief Measure and return the current in mA
/// @param  None
/// @return Current measurement in milliamps (mA)
uint32_t psu_measure_current(void) {
    psu_cal_t cal;
    psu_cal_get(&cal);
    int32_t raw = (int32_t)(amux_read_current() * ((500 * 1000) / 4095));
    int32_t cal_i = (raw * (int32_t)cal.isense_gain_ppt) / 1000 + (int32_t)cal.isense_offset_ua;
    if (cal_i < 0) cal_i = 0;
    return (uint32_t)cal_i;
}

/// @brief Measure and return the VREG voltage
/// @param  None
/// @return VREG voltage scaled in mV
uint32_t psu_measure_vreg(void) {
    psu_cal_t cal;
    psu_cal_get(&cal);
    int32_t raw = (int32_t)((6600 * amux_read(HW_ADC_MUX_VREG_OUT)) / 4096);
    int32_t cal_v = (raw * (int32_t)cal.vout_gain_ppt) / 1000 + (int32_t)cal.vout_offset_mv;
    if (cal_v < 0) cal_v = 0;
    return (uint32_t)cal_v;
}

/// @brief Measure and return the VOUT voltage
/// @param  None
/// @return VOUT voltage scaled in mV
uint32_t psu_measure_vout(void) {
    psu_cal_t cal;
    psu_cal_get(&cal);
    int32_t raw = (int32_t)((6600 * amux_read(HW_ADC_MUX_VREF_VOUT)) / 4096);
    int32_t cal_v = (raw * (int32_t)cal.vout_gain_ppt) / 1000 + (int32_t)cal.vout_offset_mv;
    if (cal_v < 0) cal_v = 0;
    return (uint32_t)cal_v;
}

/// @brief Sweep ADC multiplexer and read Vout, Isense, Vreg and fuse status
/// @param  vout   Pointer to store measured VOUT value
/// @param  isense Pointer to store measured current sense value
/// @param  vreg   Pointer to store measured VREG value
/// @param  fuse   Pointer to store fuse boolean (true if fuse OK)
/// @return None
void psu_measure(uint32_t* vout, uint32_t* isense, uint32_t* vreg, bool* fuse) {
    amux_sweep();
    psu_cal_t cal;
    psu_cal_get(&cal);

    int32_t raw_isense_ua = (int32_t)((hw_adc_raw[HW_ADC_CURRENT_SENSE]) * ((500 * 1000) / 4095));
    int32_t cal_isense_ua = (raw_isense_ua * (int32_t)cal.isense_gain_ppt) / 1000 + (int32_t)cal.isense_offset_ua;
    if (cal_isense_ua < 0) cal_isense_ua = 0;
    *isense = (uint32_t)cal_isense_ua;

    int32_t raw_vreg = (int32_t)hw_adc_voltage[HW_ADC_MUX_VREG_OUT];
    int32_t cal_vreg = (raw_vreg * (int32_t)cal.vout_gain_ppt) / 1000 + (int32_t)cal.vout_offset_mv;
    if (cal_vreg < 0) cal_vreg = 0;
    *vreg = (uint32_t)cal_vreg;

    int32_t raw_vout = (int32_t)hw_adc_voltage[HW_ADC_MUX_VREF_VOUT];
    int32_t cal_vout = (raw_vout * (int32_t)cal.vout_gain_ppt) / 1000 + (int32_t)cal.vout_offset_mv;
    if (cal_vout < 0) cal_vout = 0;
    *vout = (uint32_t)cal_vout;

    *fuse = (hw_adc_raw[HW_ADC_MUX_CURRENT_DETECT] > 300);
}

/// @brief Disable the PSU hardware and reset related state
/// @param  None
/// @return None
void psu_disable(void) {
    psu_status.enabled = false;
    s_undervoltage_fail_count = 0;
    psu_vreg_enable(false);
    psu_current_limit_override(false);
    psu_dac_set(PWM_TOP, 0);
    busy_wait_ms(5);
}

/// @brief Enable and configure the PSU with requested settings
/// @param  volts                   Requested voltage in volts (float)
/// @param  current                 Requested current in mA (float)
/// @param  current_limit_override  true to disable current limiting, false to enforce limit
/// @param  undervoltage_percent    Undervoltage threshold percent (1-100), 100 to disable
/// @return PSU_OK on success or an error code (PSU_ERROR_*) on failure
static uint8_t psu_control_levels(void) {
#if BP_HW_IOEXP_NONE
    return gpio_get(CURRENT_RESET) | (gpio_get(CURRENT_EN) << 1) | (gpio_get(CURRENT_EN_OVERRIDE) << 2);
#else
    return 0;
#endif
}

uint32_t psu_enable(float volts, float current, bool current_limit_override, uint8_t undervoltage_percent) {

    // setup the config struct
    psu_disable();
    s_undervoltage_fail_count = 0;
    psu_status.error_overcurrent = false;
    psu_status.error_undervoltage = false;
    psu_status.error_pending = false;

    psu_status.last_fuse_sample_valid = false;
    psu_set_v(volts, &psu_status);
    if (!current_limit_override) {
        psu_set_i(current, &psu_status);
    } else {
        psu_status.current_dac_value = PWM_TOP; // override, set to 100% current
    }

    psu_set_undervoltage_limit(&psu_status, undervoltage_percent);

    // 1. Start with override engaged to power output and absorb inrush
    psu_current_limit_override(true);
    psu_dac_set(psu_status.voltage_dac_value, psu_status.current_dac_value);
    psu_vreg_enable(true);
    // Allow capacitors to charge and inrush current to settle (50ms)
    busy_wait_ms(50);

    // 2. Reset the fuse latch now that inrush has settled to 0mA
    psu_fuse_reset();
    busy_wait_ms(5);

    psu_status.startup_controls = psu_control_levels();
    psu_status.startup_vreg_adc = amux_read(HW_ADC_MUX_VREG_OUT);
    psu_status.startup_vout_adc = amux_read(HW_ADC_MUX_VREF_VOUT);
    psu_status.startup_current_adc = amux_read_current();
    psu_status.startup_fuse_adc = amux_read(HW_ADC_MUX_CURRENT_DETECT);

    // 3. After settling time, engage normal current limit system if override not requested
    if (!current_limit_override) {
        psu_current_limit_override(false);
        busy_wait_ms(50);
        // Transient retry loop for high-capacitance loads
        uint8_t retries = 0;
        while (!psu_fuse_ok() && retries < 2) {
            retries++;
            // Re-engage override briefly to allow high capacitive load to finish charging
            psu_current_limit_override(true);
            busy_wait_ms(25);
            psu_fuse_reset();
            busy_wait_ms(5);
            psu_current_limit_override(false);
            busy_wait_ms(25);
        }
        if (!psu_fuse_ok()) { // sustained overcurrent or real short circuit
            psu_status.fault_controls = psu_control_levels();
            psu_status.fault_vreg_adc = amux_read(HW_ADC_MUX_VREG_OUT);
            psu_status.fault_vout_adc = amux_read(HW_ADC_MUX_VREF_VOUT);
            psu_status.fault_current_adc = amux_read_current();
            psu_disable();
            return PSU_ERROR_FUSE_TRIPPED;
        }
    }

    if (!psu_vout_ok()) { // did the vout voltage drop?
        psu_disable();
        return PSU_ERROR_VOUT_LOW;
    }

    if (!psu_backflow_ok()) { // is vreg_out < vref_vout?
        psu_disable();
        return PSU_ERROR_BACKFLOW;
    }

    psu_status.enabled = true;

    return PSU_OK;
}

/// @brief Initialize PSU hardware and status
/// @param  None
/// @return None
void psu_init(void) {
    psu_vreg_enable(false);
    psu_status.enabled = false;
    psu_status.error_overcurrent = false;
    psu_status.error_undervoltage = false;
    psu_status.error_pending = false;

    psu_cal_t cal;
    psu_cal_get(&cal);
    if (!psu_cal_is_valid(&cal)) {
        psu_cal_reset_defaults(&cal);
        psu_cal_set(&cal);
    }

    #if BP_HW_PSU_PWM
        #if BP_HW_IOEXP_NONE
        // Discrete control pin setup (RP2350 SIO)
        gpio_set_function(CURRENT_RESET, GPIO_FUNC_SIO);
        gpio_set_dir(CURRENT_RESET, GPIO_OUT);
        gpio_put(CURRENT_RESET, 1);

        gpio_set_function(CURRENT_EN, GPIO_FUNC_SIO);
        gpio_set_dir(CURRENT_EN, GPIO_OUT);
        gpio_put(CURRENT_EN, 1);

        gpio_set_function(CURRENT_EN_OVERRIDE, GPIO_FUNC_SIO);
        gpio_set_dir(CURRENT_EN_OVERRIDE, GPIO_OUT);
        gpio_put(CURRENT_EN_OVERRIDE, 0);

#ifdef CURRENT_FUSE_DETECT
        gpio_set_function(CURRENT_FUSE_DETECT, GPIO_FUNC_SIO);
        gpio_set_dir(CURRENT_FUSE_DETECT, GPIO_IN);
        gpio_set_input_enabled(CURRENT_FUSE_DETECT, true);
        gpio_pull_up(CURRENT_FUSE_DETECT);
#endif
        #endif

        // pin and PWM setup
        gpio_set_function(PSU_PWM_CURRENT_ADJ, GPIO_FUNC_SIO);
        gpio_set_dir(PSU_PWM_CURRENT_ADJ, GPIO_OUT);
        gpio_put(PSU_PWM_CURRENT_ADJ, 0);
        gpio_set_function(PSU_PWM_VREG_ADJ, GPIO_FUNC_SIO);
        gpio_set_dir(PSU_PWM_VREG_ADJ, GPIO_OUT);
        gpio_put(PSU_PWM_VREG_ADJ, 1);

        uint slice_num = pwm_gpio_to_slice_num(PSU_PWM_VREG_ADJ);
        uint v_chan_num = pwm_gpio_to_channel(PSU_PWM_VREG_ADJ);
        uint i_chan_num = pwm_gpio_to_channel(PSU_PWM_CURRENT_ADJ);

        // 10kHz clock, into our 1K + 0.1uF filter
        pwm_set_clkdiv_int_frac(slice_num, 16 >> 4, 16 & 0b1111);
        pwm_set_wrap(slice_num, PWM_TOP);

        // start with v adjust high (lowest voltage output)
        pwm_set_chan_level(slice_num, v_chan_num, PWM_TOP);

        // start with i adjust low (lowest current output)
        pwm_set_chan_level(slice_num, i_chan_num, 0);

        // enable output
        gpio_set_function(PSU_PWM_VREG_ADJ, GPIO_FUNC_PWM);
        gpio_set_function(PSU_PWM_CURRENT_ADJ, GPIO_FUNC_PWM);
        pwm_set_enabled(slice_num, true);

        // too early, spi not setup for shift register method of IO control
        // psu_current_limit_override(false);
        // psu_fuse_reset();
    #elif BP_HW_PSU_DAC
        //I2C dac
        i2c_init(BP_I2C_PORT, 400 * 1000);
        gpio_set_function(BP_I2C_SDA, GPIO_FUNC_I2C);
        gpio_set_function(BP_I2C_SCL, GPIO_FUNC_I2C);    
        //init dac
        psu_dac_set(0xffff, 0x0000);  
    #else
        #error "Platform not speficied in psu.c"
    #endif
}
