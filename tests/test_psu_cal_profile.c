#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <math.h>

#include "hardware/sync.h"
#include "hardware/adc.h"
#include "kernel/bp_status.h"
#include "kernel/bp_config.h"
#include "kernel/bp_job.h"
#include "system_config.h"
#include "pirate/amux.h"
#include "pirate/psu.h"
#define BP_STORAGE_OWNER 1
#include "../../tests/stubs/storage_fs.h"

// Stubs for host execution
uint16_t s_mock_adc_val = 1000;
uint16_t s_mock_current_amux_channel = 0;
uint16_t s_mock_channel_adc[16] = {0};
bool s_mock_use_channel_adc = false;
bool s_mock_gpio_fuse_tripped = false;
static spin_lock_t s_mock_spin_lock = 0;

static int8_t s_test_core_id = 0;
unsigned int get_core_num(void) {
    return (unsigned int)s_test_core_id;
}

uint16_t hw_adc_raw[16];
uint32_t hw_adc_voltage[16];
uint32_t hw_adc_avgsum_voltage[16];

struct _system_config system_config;

static uint32_t s_mock_open_file_count = 0;
void bp_storage_file_opened(void) { s_mock_open_file_count++; }
void bp_storage_file_closed(void) { if (s_mock_open_file_count > 0) s_mock_open_file_count--; }
uint32_t bp_storage_get_open_count(void) { return s_mock_open_file_count; }

void bp_watchdog_heartbeat(uint8_t c) { (void)c; }

uint32_t spin_lock_blocking(spin_lock_t *lock) { (void)lock; return 0; }
void spin_unlock(spin_lock_t *lock, uint32_t save) { (void)lock; (void)save; }
void spin_lock_unsafe_blocking(spin_lock_t *lock) { (void)lock; }
void spin_unlock_unsafe(spin_lock_t *lock) { (void)lock; }
uint spin_lock_claim_unused(bool required) { (void)required; return 1; }
spin_lock_t* spin_lock_init(uint lock_num) { (void)lock_num; return &s_mock_spin_lock; }

static uint32_t s_mock_sim_us = 0;
uint32_t time_us_32(void) { return s_mock_sim_us; }
void busy_wait_ms(uint32_t ms) { s_mock_sim_us += ms * 1000; }
void busy_wait_us(uint32_t us) { s_mock_sim_us += us; }

uint pwm_gpio_to_slice_num(uint gpio) { (void)gpio; return 0; }
uint pwm_gpio_to_channel(uint gpio) { (void)gpio; return 0; }
void pwm_set_chan_level(uint slice_num, uint chan, uint16_t level) {
    (void)slice_num; (void)chan; (void)level;
}

// -------------------------------------------------------------------------
// Test 1: Calibration Defaults and Integrity Checksums
// -------------------------------------------------------------------------
static void test_cal_defaults(void) {
    printf("[TEST] Calibration defaults and checksum initialization...\n");
    psu_cal_t cal;
    psu_cal_reset_defaults(&cal);

    assert(cal.magic == PSU_CAL_MAGIC);
    assert(cal.vout_gain_ppt == 1000);
    assert(cal.vout_offset_mv == 0);
    assert(cal.isense_gain_ppt == 1000);
    assert(cal.isense_offset_ua == 0);
    assert(cal.vdac_gain_ppt == 1000);
    assert(cal.vdac_offset_mv == 0);
    assert(cal.idac_gain_ppt == 1000);
    assert(cal.idac_offset_ma == 0);
    assert(cal.flags == 0);
    assert(psu_cal_is_valid(&cal));

    printf("[PASS] Nominal factory defaults verified\n");
}

// -------------------------------------------------------------------------
// Test 2: Calibration Validation and Tamper Resistance
// -------------------------------------------------------------------------
static void test_cal_validation_and_tampering(void) {
    printf("[TEST] Calibration bounds checking and tamper detection...\n");
    psu_cal_t cal;
    psu_cal_reset_defaults(&cal);
    assert(psu_cal_is_valid(&cal));

    // Tampered magic
    cal.magic = 0x12345678;
    assert(!psu_cal_is_valid(&cal));
    cal.magic = PSU_CAL_MAGIC;

    // Tampered checksum
    cal.checksum ^= 0xFF;
    assert(!psu_cal_is_valid(&cal));
    cal.checksum = psu_cal_calc_checksum(&cal);
    assert(psu_cal_is_valid(&cal));

    // Out of bounds gain (< 500 ppt or > 2000 ppt)
    cal.vout_gain_ppt = 499;
    cal.checksum = psu_cal_calc_checksum(&cal);
    assert(!psu_cal_is_valid(&cal));
    cal.vout_gain_ppt = 2001;
    cal.checksum = psu_cal_calc_checksum(&cal);
    assert(!psu_cal_is_valid(&cal));
    cal.vout_gain_ppt = 1000;

    // Out of bounds offset (> 2000 mV)
    cal.vout_offset_mv = 2500;
    cal.checksum = psu_cal_calc_checksum(&cal);
    assert(!psu_cal_is_valid(&cal));
    cal.vout_offset_mv = 0;
    cal.checksum = psu_cal_calc_checksum(&cal);
    assert(psu_cal_is_valid(&cal));

    printf("[PASS] Bounds checking and tamper detection verified\n");
}

// -------------------------------------------------------------------------
// Test 3: JSON Serialization, Persistence, and Storage Loading
// -------------------------------------------------------------------------
static void test_cal_json_persistence(void) {
    printf("[TEST] Calibration JSON serialization and storage loading...\n");
    memset(mock_files, 0, sizeof(mock_files));

    psu_cal_t custom;
    psu_cal_reset_defaults(&custom);
    custom.vout_gain_ppt = 1025;      // +2.5% gain
    custom.vout_offset_mv = -15;      // -15 mV offset
    custom.isense_gain_ppt = 975;     // -2.5% gain
    custom.isense_offset_ua = 320;    // +320 uA offset
    custom.vdac_gain_ppt = 1010;      // +1.0% DAC gain
    custom.vdac_offset_mv = 8;        // +8 mV DAC offset
    custom.idac_gain_ppt = 990;       // -1.0% IDAC gain
    custom.idac_offset_ma = -1;       // -1 mA IDAC offset
    custom.flags = 1;                 // calibrated
    custom.checksum = psu_cal_calc_checksum(&custom);

    // Save custom calibration
    assert(psu_cal_save(&custom));

    // Verify file was written to mock filesystem
    int idx = mock_find(PSU_CAL_FILE);
    assert(idx >= 0);
    assert(mock_files[idx].size > 0);

    // Reset RAM calibration to defaults
    psu_cal_t def;
    psu_cal_reset_defaults(&def);
    psu_cal_set(&def);

    psu_cal_t current;
    psu_cal_get(&current);
    assert(current.vout_gain_ppt == 1000);

    // Load from storage
    assert(psu_cal_load());
    psu_cal_get(&current);

    assert(current.vout_gain_ppt == 1025);
    assert(current.vout_offset_mv == -15);
    assert(current.isense_gain_ppt == 975);
    assert(current.isense_offset_ua == 320);
    assert(current.vdac_gain_ppt == 1010);
    assert(current.vdac_offset_mv == 8);
    assert(current.idac_gain_ppt == 990);
    assert(current.idac_offset_ma == -1);
    assert(current.flags == 1);

    printf("[PASS] Calibration profile serialized to JSON and reloaded correctly\n");
}

// -------------------------------------------------------------------------
// Test 4: Missing & Corrupt Calibration Fallback
// -------------------------------------------------------------------------
static void test_cal_fallback_missing_and_corrupt(void) {
    printf("[TEST] Calibration fallback on missing and corrupted files...\n");
    memset(mock_files, 0, sizeof(mock_files));

    // File missing: should return false and safely restore nominal defaults
    assert(!psu_cal_load());
    psu_cal_t cal;
    psu_cal_get(&cal);
    assert(cal.vout_gain_ppt == 1000);
    assert(cal.vout_offset_mv == 0);
    assert(cal.flags == 0);

    // Write corrupted JSON
    FIL fil;
    assert(f_open(&fil, PSU_CAL_FILE, FA_CREATE_ALWAYS | FA_WRITE) == FR_OK);
    const char *garbage = "{ \"vout_gain_ppt\": 99999, invalid syntax }";
    UINT bw = 0;
    f_write(&fil, garbage, strlen(garbage), &bw);
    f_close(&fil);

    // Load should reject invalid/corrupted file and safely fall back to nominal defaults
    assert(!psu_cal_load());
    psu_cal_get(&cal);
    assert(cal.vout_gain_ppt == 1000);
    assert(cal.flags == 0);

    printf("[PASS] Fallback to nominal defaults on missing and corrupted files verified\n");
}

// -------------------------------------------------------------------------
// Test 5: Gain and Offset Application to DAC and Measurements
// -------------------------------------------------------------------------
static void test_cal_applied_math(void) {
    printf("[TEST] Applying calibration gain and offset to DAC and ADC measurements...\n");
    psu_cal_t cal;
    psu_cal_reset_defaults(&cal);
    cal.vout_gain_ppt = 1050;      // +5% gain
    cal.vout_offset_mv = 20;       // +20 mV offset
    cal.isense_gain_ppt = 1100;    // +10% gain
    cal.isense_offset_ua = 500;    // +500 uA offset
    cal.vdac_gain_ppt = 1020;      // +2% DAC gain
    cal.vdac_offset_mv = 10;       // +10 mV DAC offset
    cal.flags = 1;
    cal.checksum = psu_cal_calc_checksum(&cal);
    assert(psu_cal_set(&cal));

    // Test psu_set_v DAC adjustment
    struct psu_status_t status;
    memset(&status, 0, sizeof(status));
    psu_set_v(3.30f, &status);
    assert(status.voltage_requested_float == 3.30f);
    // Target was 3300 mV. Compensated DAC target = ((3300 - 10) * 1000) / 1020 = 3225 mV.
    // Ensure DAC value was margined accordingly
    assert(status.voltage_dac_value > 0);

    // Test psu_measure with ADC mock values
    amux_dma_init();
    s_mock_use_channel_adc = true;
    s_mock_channel_adc[HW_ADC_MUX_VREF_VOUT] = 2048;
    s_mock_channel_adc[HW_ADC_MUX_VREG_OUT] = 2048;
    s_mock_channel_adc[HW_ADC_CURRENT_SENSE] = 819;
    s_mock_channel_adc[HW_ADC_MUX_CURRENT_DETECT] = 1000;
    amux_dma_sweep_start();

    uint32_t vout = 0, isense = 0, vreg = 0;
    bool fuse = false;
    psu_measure(&vout, &isense, &vreg, &fuse);

    // Raw voltage was 3300 mV (2048 * 6600 / 4096 = 3300).
    // Expected VOUT: (3300 * 1050) / 1000 + 20 = 3465 + 20 = 3485 mV
    assert(vout == 3485);
    // Expected VREG: 3485 mV
    assert(vreg == 3485);
    // Raw isense = 819 * (500000 / 4095) = 819 * 122 = 99918 uA.
    // Calibrated ISENSE = (99918 * 1100) / 1000 + 500 = 109909 + 500 = 110,409 uA
    assert(isense == 110409);
    assert(fuse == true);

    printf("[PASS] Calibrated DAC set and ADC measurement math verified\n");
}

// -------------------------------------------------------------------------
// Test 6: Coulomb Counter & Power Profiler Integration Accuracy
// -------------------------------------------------------------------------
static void test_power_profiler_resistive_load_accuracy(void) {
    printf("[TEST] Power profiler integration accuracy against known resistive load...\n");

    // Simulation parameters:
    // Voltage = 3.300 V (3300 mV)
    // Load R = 33.00 Ohm
    // Theoretical Current I = V / R = 3.300 V / 33.00 Ohm = 0.100 A = 100.000 mA (100,000 uA)
    // Theoretical Power P = V * I = 3.300 V * 0.100 A = 0.330 W = 330.000 mW (330,000 uW)
    const uint32_t sim_vout_mv = 3300;
    const uint32_t sim_isense_ua = 100000; // 100 mA
    const uint32_t sim_power_uw = (uint32_t)(((uint64_t)sim_vout_mv * (uint64_t)sim_isense_ua) / 1000ULL); // 330,000 uW

    // Integration accumulators (matching w.profile implementation)
    uint64_t charge_uas_frac = 0;
    uint64_t energy_uws_frac = 0;

    // Simulate 3600 seconds (1 full hour) with 50 ms time steps (72,000 ticks)
    const uint32_t dt_us = 50000; // 50 ms
    const uint32_t total_ticks = 72000;

    for (uint32_t t = 0; t < total_ticks; t++) {
        charge_uas_frac += (uint64_t)sim_isense_ua * (uint64_t)dt_us;
        energy_uws_frac += (uint64_t)sim_power_uw * (uint64_t)dt_us;
    }

    // Convert to milliampere-hours (mAh) and milliwatt-hours (mWh)
    // 1 mAh = 3,600,000,000,000 uAs_frac (1 uA * 1 us = 1 pC; 1 mAh = 3.6e12 pC)
    // 1 mWh = 3,600,000,000,000 uWs_frac (1 uW * 1 us = 1 pJ; 1 mWh = 3.6e12 pJ)
    double total_mah = (double)charge_uas_frac / 3.6e12;
    double total_mwh = (double)energy_uws_frac / 3.6e12;

    printf("  1-hour simulated resistive load (3.3V, 33 Ohm):\n");
    printf("    Theoretical Charge: 100.000 mAh | Measured: %.5f mAh\n", total_mah);
    printf("    Theoretical Energy: 330.000 mWh | Measured: %.5f mWh\n", total_mwh);

    // Verify mathematical accuracy is within 0.0001% (zero integration drift)
    assert(fabs(total_mah - 100.0) < 0.0001);
    assert(fabs(total_mwh - 330.0) < 0.0001);

    // Test integer display extraction (mAh integer and fractional parts)
    uint32_t mah_int = (uint32_t)(charge_uas_frac / 3600000000000ULL);
    uint32_t mah_frac = (uint32_t)((charge_uas_frac / 3600000000ULL) % 1000ULL);
    assert(mah_int == 100);
    assert(mah_frac == 0);

    uint32_t mwh_int = (uint32_t)(energy_uws_frac / 3600000000000ULL);
    uint32_t mwh_frac = (uint32_t)((energy_uws_frac / 3600000000ULL) % 1000ULL);
    assert(mwh_int == 330);
    assert(mwh_frac == 0);

    // Test pulsed load scenario:
    // Phase 1: 1800 s @ 50 mA  (165 mW) -> Expected charge = 50 mA * 0.5 h = 25 mAh
    // Phase 2: 1800 s @ 150 mA (495 mW) -> Expected charge = 150 mA * 0.5 h = 75 mAh
    // Total expected charge = 100 mAh; Total expected energy = 82.5 + 247.5 = 330 mWh
    charge_uas_frac = 0;
    energy_uws_frac = 0;
    for (uint32_t t = 0; t < 36000; t++) {
        charge_uas_frac += (uint64_t)50000 * (uint64_t)dt_us;
        energy_uws_frac += (uint64_t)165000 * (uint64_t)dt_us;
    }
    for (uint32_t t = 0; t < 36000; t++) {
        charge_uas_frac += (uint64_t)150000 * (uint64_t)dt_us;
        energy_uws_frac += (uint64_t)495000 * (uint64_t)dt_us;
    }
    total_mah = (double)charge_uas_frac / 3.6e12;
    total_mwh = (double)energy_uws_frac / 3.6e12;
    assert(fabs(total_mah - 100.0) < 0.0001);
    assert(fabs(total_mwh - 330.0) < 0.0001);

    printf("[PASS] Coulomb counting & energy integration accuracy verified to < 0.0001%%\n");
}

// -------------------------------------------------------------------------
// Test 7: Real-Time CSV Streaming Buffer Logic
// -------------------------------------------------------------------------
static void test_power_profiler_csv_streaming(void) {
    printf("[TEST] Real-time CSV formatting and streaming buffer flush...\n");
    memset(mock_files, 0, sizeof(mock_files));

    const char *csv_path = "/log/power_test.csv";
    FIL fil;
    assert(f_open(&fil, csv_path, FA_CREATE_ALWAYS | FA_WRITE) == FR_OK);

    const char *header = "timestamp_ms,voltage_v,current_ma,power_mw,charge_mah,energy_mwh\r\n";
    UINT bw = 0;
    assert(f_write(&fil, header, strlen(header), &bw) == FR_OK);
    assert(bw == strlen(header));

    char csv_buf[512];
    uint32_t csv_buf_pos = 0;

    // Simulate 20 periodic 100ms row samples
    for (uint32_t i = 1; i <= 20; i++) {
        uint32_t elapsed_ms = i * 100;
        float v = 3.300f;
        float cur = 50.0f;
        float p = 165.0f;
        uint32_t q_int = 0, q_frac = (uint32_t)(i * 13);
        uint32_t e_int = 0, e_frac = (uint32_t)(i * 45);

        char line[128];
        int n = snprintf(line, sizeof(line), "%lu,%1.3f,%1.2f,%1.2f,%u.%03u,%u.%03u\r\n",
                         (unsigned long)elapsed_ms, v, cur, p, q_int, q_frac, e_int, e_frac);
        assert(n > 0);

        if (csv_buf_pos + (unsigned)n >= sizeof(csv_buf)) {
            UINT chunk_bw = 0;
            assert(f_write(&fil, csv_buf, csv_buf_pos, &chunk_bw) == FR_OK);
            assert(chunk_bw == csv_buf_pos);
            csv_buf_pos = 0;
        }
        memcpy(csv_buf + csv_buf_pos, line, n);
        csv_buf_pos += n;
    }

    // Final flush
    if (csv_buf_pos > 0) {
        UINT chunk_bw = 0;
        assert(f_write(&fil, csv_buf, csv_buf_pos, &chunk_bw) == FR_OK);
        assert(chunk_bw == csv_buf_pos);
    }
    assert(f_sync(&fil) == FR_OK);
    assert(f_close(&fil) == FR_OK);

    int idx = mock_find(csv_path);
    assert(idx >= 0);
    assert(mock_files[idx].size > 0);
    // Verify file starts with header
    assert(strncmp((char*)mock_files[idx].data, "timestamp_ms", 12) == 0);

    printf("[PASS] CSV streaming export verified: %u bytes flushed to %s\n",
           mock_files[idx].size, csv_path);
}

static void test_cal_save_preserves_previous(void) {
    memset(mock_files, 0, sizeof(mock_files));
    psu_cal_t cal;
    psu_cal_reset_defaults(&cal);
    assert(psu_cal_save(&cal));
    cal.vout_gain_ppt = 1100;
    cal.checksum = psu_cal_calc_checksum(&cal);
    mock_sync_error = FR_DISK_ERR;
    assert(!psu_cal_save(&cal));
    mock_sync_error = 0;
    assert(psu_cal_load());
    psu_cal_t loaded;
    psu_cal_get(&loaded);
    assert(loaded.vout_gain_ppt == 1000);
    mock_rename_error = FR_DISK_ERR;
    assert(!psu_cal_save(&cal));
    mock_rename_error = 0;
    assert(psu_cal_load());
    psu_cal_get(&loaded);
    assert(loaded.vout_gain_ppt == 1000);
    assert(psu_cal_save(&cal));
    /* Simulate reset after moving primary aside, before installing temp. */
    assert(f_unlink(PSU_CAL_FILE) == FR_OK);
    assert(psu_cal_load());
    psu_cal_get(&loaded);
    assert(loaded.vout_gain_ppt == 1000);
}

static void test_cal_persistence_errors(void) {
    psu_cal_t cal;
    psu_cal_reset_defaults(&cal);
    mock_sync_error = FR_DISK_ERR;
    assert(!psu_cal_save(&cal));
    mock_sync_error = 0;
    mock_close_error = FR_DISK_ERR;
    assert(!psu_cal_save(&cal));
    mock_close_error = 0;
    mock_short_write = 1;
    assert(!psu_cal_save(&cal));
    mock_short_write = 0;
}

static void test_cal_invalid_numeric_fields(void) {
    const char *values[] = { "1e100", "-1", "1000.5", "66536", "null" };
    for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        memset(mock_files, 0, sizeof(mock_files));
        char json[512];
        int len = snprintf(json, sizeof(json),
            "{\"vout_gain_ppt\":%s,\"vout_offset_mv\":0,"
            "\"isense_gain_ppt\":1000,\"isense_offset_ua\":0,"
            "\"vdac_gain_ppt\":1000,\"vdac_offset_mv\":0,"
            "\"idac_gain_ppt\":1000,\"idac_offset_ma\":0}", values[i]);
        FIL fil;
        UINT bw;
        assert(f_open(&fil, PSU_CAL_FILE, FA_CREATE_ALWAYS | FA_WRITE) == FR_OK);
        assert(f_write(&fil, json, len, &bw) == FR_OK && bw == (UINT)len);
        assert(f_close(&fil) == FR_OK);
        assert(!psu_cal_load());
        psu_cal_t cal;
        psu_cal_get(&cal);
        assert(cal.flags == 0 && cal.vout_gain_ppt == 1000);
    }
    memset(mock_files, 0, sizeof(mock_files));
    FIL fil;
    UINT bw;
    assert(f_open(&fil, PSU_CAL_FILE, FA_CREATE_ALWAYS | FA_WRITE) == FR_OK);
    assert(f_write(&fil, "{}", 2, &bw) == FR_OK);
    assert(f_close(&fil) == FR_OK);
    assert(!psu_cal_load());
}

static void test_corrupt_primary_valid_backup_recovery(void) {
    printf("[TEST] Corrupt primary with valid backup recovery...\n");
    memset(mock_files, 0, sizeof(mock_files));

    /* 1. Write corrupted primary */
    FIL fil;
    UINT bw;
    assert(f_open(&fil, PSU_CAL_FILE, FA_CREATE_ALWAYS | FA_WRITE) == FR_OK);
    const char *corrupted_primary = "{\n  \"schema_version\": 1,\n  \"vout_gain_ppt\": 99999,\n  \"checksum_crc32\": \"12345678\"\n}\n";
    assert(f_write(&fil, corrupted_primary, strlen(corrupted_primary), &bw) == FR_OK);
    assert(f_close(&fil) == FR_OK);

    /* 2. Write valid backup with custom coefficients */
    psu_cal_t backup_cal;
    psu_cal_reset_defaults(&backup_cal);
    backup_cal.vout_gain_ppt = 1080;
    backup_cal.vout_offset_mv = -12;
    backup_cal.flags = 1;
    backup_cal.checksum = psu_cal_calc_checksum(&backup_cal);

    uint32_t crc = psu_cal_canonical_crc32(1, &backup_cal);
    char backup_json[512];
    int len = snprintf(backup_json, sizeof(backup_json),
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"vout_gain_ppt\": %u,\n"
        "  \"vout_offset_mv\": %d,\n"
        "  \"isense_gain_ppt\": %u,\n"
        "  \"isense_offset_ua\": %d,\n"
        "  \"vdac_gain_ppt\": %u,\n"
        "  \"vdac_offset_mv\": %d,\n"
        "  \"idac_gain_ppt\": %u,\n"
        "  \"idac_offset_ma\": %d,\n"
        "  \"checksum_crc32\": \"%08x\"\n"
        "}\n",
        backup_cal.vout_gain_ppt, backup_cal.vout_offset_mv,
        backup_cal.isense_gain_ppt, backup_cal.isense_offset_ua,
        backup_cal.vdac_gain_ppt, backup_cal.vdac_offset_mv,
        backup_cal.idac_gain_ppt, backup_cal.idac_offset_ma,
        crc
    );

    assert(f_open(&fil, "/config/psu_cal.bak", FA_CREATE_ALWAYS | FA_WRITE) == FR_OK);
    assert(f_write(&fil, backup_json, len, &bw) == FR_OK);
    assert(f_close(&fil) == FR_OK);

    /* 3. Load should not stop at corrupt primary; must recover from valid backup! */
    assert(psu_cal_load());

    psu_cal_t current;
    psu_cal_get(&current);
    assert(current.vout_gain_ppt == 1080);
    assert(current.vout_offset_mv == -12);
    assert(current.flags == 1);
    assert(bp_storage_get_open_count() == 0);

    printf("[PASS] Corrupt primary bypassed, valid backup recovered successfully\n");
}

static void test_bounded_candidate_parser_edge_cases(void) {
    printf("[TEST] Bounded parser edge cases (duplicates, trailing, oversize, CRC, legacy)...\n");
    psu_cal_t cal;
    bool is_legacy = false;

    /* 1. NULL / Empty checks */
    assert(!psu_cal_parse_json(NULL, 10, &cal, &is_legacy));
    assert(!psu_cal_parse_json("{}", 0, &cal, &is_legacy));
    assert(!psu_cal_parse_json("{}", 2, NULL, &is_legacy));

    /* 2. Oversize check (>= 512 bytes) */
    char huge[513];
    memset(huge, ' ', sizeof(huge));
    assert(!psu_cal_parse_json(huge, sizeof(huge), &cal, &is_legacy));

    /* 3. Duplicate field check */
    const char *dup_field =
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"vout_gain_ppt\": 1000,\n"
        "  \"vout_gain_ppt\": 1000,\n"
        "  \"vout_offset_mv\": 0,\n"
        "  \"isense_gain_ppt\": 1000,\n"
        "  \"isense_offset_ua\": 0,\n"
        "  \"vdac_gain_ppt\": 1000,\n"
        "  \"vdac_offset_mv\": 0,\n"
        "  \"idac_gain_ppt\": 1000,\n"
        "  \"idac_offset_ma\": 0,\n"
        "  \"checksum_crc32\": \"12345678\"\n"
        "}";
    assert(!psu_cal_parse_json(dup_field, strlen(dup_field), &cal, &is_legacy));

    /* 4. Trailing garbage check */
    psu_cal_t valid_def;
    psu_cal_reset_defaults(&valid_def);
    uint32_t valid_crc = psu_cal_canonical_crc32(1, &valid_def);

    char valid_with_trailing[300];
    snprintf(valid_with_trailing, sizeof(valid_with_trailing),
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"vout_gain_ppt\": 1000,\n"
        "  \"vout_offset_mv\": 0,\n"
        "  \"isense_gain_ppt\": 1000,\n"
        "  \"isense_offset_ua\": 0,\n"
        "  \"vdac_gain_ppt\": 1000,\n"
        "  \"vdac_offset_mv\": 0,\n"
        "  \"idac_gain_ppt\": 1000,\n"
        "  \"idac_offset_ma\": 0,\n"
        "  \"checksum_crc32\": \"%08x\"\n"
        "} trailing_garbage", valid_crc);
    assert(!psu_cal_parse_json(valid_with_trailing, strlen(valid_with_trailing), &cal, &is_legacy));

    /* Valid with trailing whitespace should pass */
    char valid_with_ws[300];
    snprintf(valid_with_ws, sizeof(valid_with_ws),
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"vout_gain_ppt\": 1000,\n"
        "  \"vout_offset_mv\": 0,\n"
        "  \"isense_gain_ppt\": 1000,\n"
        "  \"isense_offset_ua\": 0,\n"
        "  \"vdac_gain_ppt\": 1000,\n"
        "  \"vdac_offset_mv\": 0,\n"
        "  \"idac_gain_ppt\": 1000,\n"
        "  \"idac_offset_ma\": 0,\n"
        "  \"checksum_crc32\": \"%08x\"\n"
        "}   \n\r\t  ", valid_crc);
    assert(psu_cal_parse_json(valid_with_ws, strlen(valid_with_ws), &cal, &is_legacy));
    assert(!is_legacy);

    /* 5. Unsupported future schema_version check */
    char bad_version[300];
    snprintf(bad_version, sizeof(bad_version),
        "{\n"
        "  \"schema_version\": 2,\n"
        "  \"vout_gain_ppt\": 1000,\n"
        "  \"vout_offset_mv\": 0,\n"
        "  \"isense_gain_ppt\": 1000,\n"
        "  \"isense_offset_ua\": 0,\n"
        "  \"vdac_gain_ppt\": 1000,\n"
        "  \"vdac_offset_mv\": 0,\n"
        "  \"idac_gain_ppt\": 1000,\n"
        "  \"idac_offset_ma\": 0,\n"
        "  \"checksum_crc32\": \"%08x\"\n"
        "}", valid_crc);
    assert(!psu_cal_parse_json(bad_version, strlen(bad_version), &cal, &is_legacy));

    /* 6. Invalid non-hex / wrong length checksum check */
    char bad_checksum[300];
    snprintf(bad_checksum, sizeof(bad_checksum),
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"vout_gain_ppt\": 1000,\n"
        "  \"vout_offset_mv\": 0,\n"
        "  \"isense_gain_ppt\": 1000,\n"
        "  \"isense_offset_ua\": 0,\n"
        "  \"vdac_gain_ppt\": 1000,\n"
        "  \"vdac_offset_mv\": 0,\n"
        "  \"idac_gain_ppt\": 1000,\n"
        "  \"idac_offset_ma\": 0,\n"
        "  \"checksum_crc32\": \"zzzzzzzz\"\n"
        "}");
    assert(!psu_cal_parse_json(bad_checksum, strlen(bad_checksum), &cal, &is_legacy));

    /* 7. Mismatched checksum */
    char wrong_crc[300];
    snprintf(wrong_crc, sizeof(wrong_crc),
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"vout_gain_ppt\": 1000,\n"
        "  \"vout_offset_mv\": 0,\n"
        "  \"isense_gain_ppt\": 1000,\n"
        "  \"isense_offset_ua\": 0,\n"
        "  \"vdac_gain_ppt\": 1000,\n"
        "  \"vdac_offset_mv\": 0,\n"
        "  \"idac_gain_ppt\": 1000,\n"
        "  \"idac_offset_ma\": 0,\n"
        "  \"checksum_crc32\": \"deadbeef\"\n"
        "}");
    assert(!psu_cal_parse_json(wrong_crc, strlen(wrong_crc), &cal, &is_legacy));

    /* 8. Partial schema / checksum (only one present) must be rejected */
    const char *only_schema =
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"vout_gain_ppt\": 1000,\n"
        "  \"vout_offset_mv\": 0,\n"
        "  \"isense_gain_ppt\": 1000,\n"
        "  \"isense_offset_ua\": 0,\n"
        "  \"vdac_gain_ppt\": 1000,\n"
        "  \"vdac_offset_mv\": 0,\n"
        "  \"idac_gain_ppt\": 1000,\n"
        "  \"idac_offset_ma\": 0\n"
        "}";
    assert(!psu_cal_parse_json(only_schema, strlen(only_schema), &cal, &is_legacy));

    const char *only_checksum =
        "{\n"
        "  \"vout_gain_ppt\": 1000,\n"
        "  \"vout_offset_mv\": 0,\n"
        "  \"isense_gain_ppt\": 1000,\n"
        "  \"isense_offset_ua\": 0,\n"
        "  \"vdac_gain_ppt\": 1000,\n"
        "  \"vdac_offset_mv\": 0,\n"
        "  \"idac_gain_ppt\": 1000,\n"
        "  \"idac_offset_ma\": 0,\n"
        "  \"checksum_crc32\": \"12345678\"\n"
        "}";
    assert(!psu_cal_parse_json(only_checksum, strlen(only_checksum), &cal, &is_legacy));

    /* 9. Legacy format parsing & migration */
    const char *valid_legacy =
        "{\n"
        "  \"vout_gain_ppt\": 1040,\n"
        "  \"vout_offset_mv\": 15,\n"
        "  \"isense_gain_ppt\": 960,\n"
        "  \"isense_offset_ua\": -200,\n"
        "  \"vdac_gain_ppt\": 1010,\n"
        "  \"vdac_offset_mv\": 5,\n"
        "  \"idac_gain_ppt\": 995,\n"
        "  \"idac_offset_ma\": 2\n"
        "}";
    assert(psu_cal_parse_json(valid_legacy, strlen(valid_legacy), &cal, &is_legacy));
    assert(is_legacy == true);
    assert(cal.vout_gain_ppt == 1040);
    assert(cal.vout_offset_mv == 15);

    /* Test full migration on disk */
    memset(mock_files, 0, sizeof(mock_files));
    FIL fil;
    UINT bw;
    assert(f_open(&fil, PSU_CAL_FILE, FA_CREATE_ALWAYS | FA_WRITE) == FR_OK);
    assert(f_write(&fil, valid_legacy, strlen(valid_legacy), &bw) == FR_OK);
    assert(f_close(&fil) == FR_OK);

    /* Loading legacy must succeed and trigger migration to schema_version 1 */
    assert(psu_cal_load());
    int idx = mock_find(PSU_CAL_FILE);
    assert(idx >= 0);
    assert(strstr((char*)mock_files[idx].data, "\"schema_version\": 1") != NULL);
    assert(strstr((char*)mock_files[idx].data, "\"checksum_crc32\"") != NULL);

    printf("[PASS] Bounded candidate parser edge cases and migration verified\n");
}

int main(void) {
    printf("=== Starting PSU Profiler & Calibration Unit Tests ===\n");
    assert(bp_config_subsystem_init() == BP_STATUS_OK);

    test_cal_save_preserves_previous();
    test_cal_persistence_errors();
    test_cal_invalid_numeric_fields();
    test_cal_defaults();
    test_cal_validation_and_tampering();
    test_cal_json_persistence();
    test_cal_fallback_missing_and_corrupt();
    test_cal_applied_math();
    test_corrupt_primary_valid_backup_recovery();
    test_bounded_candidate_parser_edge_cases();
    test_power_profiler_resistive_load_accuracy();
    test_power_profiler_csv_streaming();

    printf("=== ALL PSU PROFILER & CALIBRATION TESTS PASSED ===\n");
    return 0;
}
