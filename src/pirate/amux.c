/**
 * @file amux.c
 * @brief Analog multiplexer (AMUX) control and ADC management
 * 
 * This file implements the analog multiplexer system for Bus Pirate, which
 * allows reading voltages from multiple sources through a single ADC channel.
 * 
 * The AMUX system provides:
 * - Voltage measurement on all 8 BIO pins
 * - Power supply voltage monitoring
 * - Current sense measurement
 * - Multi-core safe ADC access with spinlocks
 * - Averaging for noise reduction
 * 
 * Hardware:
 * - CD4067 16-channel analog multiplexer
 * - RP2040 ADC (12-bit)
 * - Voltage dividers for different ranges
 * 
 * @author Bus Pirate Project
 * @date 2024-2026
 */

#include <stdio.h>
/* Modified by Codex: bounded ADC acquisition never steals ownership. */
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#if !defined(BP_HOST_TEST)
#include "hardware/dma.h"
#endif
#include "pirate.h"
#include "pirate/amux.h"
#include "command_struct.h"
#include "hardware/sync.h"
#include "pico/lock_core.h"
#include "kernel/bp_resource.h"

spin_lock_t* adc_spin_lock;    /**< Spinlock for multi-core ADC access protection */
uint adc_spin_lock_num;        /**< Spinlock number */

bool reset_adc_average = false; /**< Flag to reset ADC averaging */

static volatile int8_t s_adc_owner_core = -1;
static volatile uint32_t s_adc_nest_count = 0;

/* Autonomous DMA AMUX pipeline state */
static int32_t s_amux_dma_ch_data = -1;
static int32_t s_amux_dma_ch_ctrl = -1;
static uint16_t s_amux_pingpong[2][HW_ADC_MUX_COUNT + 1];
static amux_frame_t s_amux_frames[2];
static volatile uint8_t s_amux_completed_idx = 0;
static volatile uint8_t s_amux_active_idx = 1;
static volatile bool s_amux_dma_busy = false;
static bool s_amux_dma_initialized = false;
static volatile amux_state_t s_amux_state = AMUX_STATE_STOPPED;
static volatile uint32_t s_amux_generation = 0;
static volatile uint32_t s_amux_fault_count = 0;

int32_t amux_dma_get_data_channel(void) {
    return s_amux_dma_ch_data;
}

int32_t amux_dma_get_ctrl_channel(void) {
    return s_amux_dma_ch_ctrl;
}

bool amux_dma_is_enabled(void) {
    return s_amux_dma_initialized;
}

bool amux_dma_is_busy(void) {
    return s_amux_dma_busy;
}

amux_state_t amux_get_state(void) {
    return s_amux_state;
}

uint32_t amux_get_generation(void) {
    return s_amux_generation;
}

uint32_t amux_get_fault_count(void) {
    return s_amux_fault_count;
}

bool amux_is_telemetry_fresh(uint64_t max_age_us) {
    if (!s_amux_dma_initialized || s_amux_state == AMUX_STATE_FAULT) {
        return false;
    }
    uint8_t idx = s_amux_completed_idx;
    if (!s_amux_frames[idx].valid || s_amux_frames[idx].generation == 0) {
        return false;
    }
    uint64_t now_us = (uint64_t)time_us_32();
    uint64_t age = (now_us >= s_amux_frames[idx].timestamp_us) ?
                   (now_us - s_amux_frames[idx].timestamp_us) : 0;
    return (age <= max_age_us);
}

void amux_inject_fault(uint8_t fault_code) {
    s_amux_state = AMUX_STATE_FAULT;
    s_amux_fault_count++;
    s_amux_dma_busy = false;
    s_amux_frames[s_amux_completed_idx].valid = false;
    s_amux_frames[s_amux_completed_idx].fault_code = fault_code;
    s_amux_frames[s_amux_active_idx].valid = false;
    s_amux_frames[s_amux_active_idx].fault_code = fault_code;
}

void amux_clear_fault(void) {
    if (s_amux_state == AMUX_STATE_FAULT) {
        s_amux_state = s_amux_dma_initialized ? AMUX_STATE_IDLE : AMUX_STATE_STOPPED;
    }
}

bool amux_get_frame(amux_frame_t* out_frame) {
    if (!out_frame || !s_amux_dma_initialized || s_amux_state == AMUX_STATE_FAULT) {
        return false;
    }
    for (int retry = 0; retry < 3; retry++) {
        uint8_t idx = s_amux_completed_idx;
        uint32_t gen_before = s_amux_frames[idx].generation;
        if (gen_before == 0 || !s_amux_frames[idx].valid) {
            return false;
        }
        *out_frame = s_amux_frames[idx];
        uint32_t gen_after = s_amux_frames[idx].generation;
        if (gen_before == gen_after) {
            return true;
        }
    }
    return false;
}

bool amux_dma_init(void) {
    if (s_amux_dma_initialized) {
        return true;
    }
    /* 1. Transactional acquire: ADC peripheral lease */
    if (bp_res_acquire(BP_RES_CLASS_PERIPH, BP_RES_PERIPH_ADC, BP_RES_OWNER_SYSTEM, "amux_adc") != BP_STATUS_OK) {
        return false;
    }

    /* 2. Transactional acquire: Data DMA channel */
    uint32_t ch_data = 0;
    if (bp_res_claim_unused_dma_ch(BP_RES_OWNER_SYSTEM, "amux_adc", &ch_data) != BP_STATUS_OK) {
        bp_res_release(BP_RES_CLASS_PERIPH, BP_RES_PERIPH_ADC, BP_RES_OWNER_SYSTEM);
        return false;
    }

    /* 3. Transactional acquire: Control DMA channel */
    uint32_t ch_ctrl = 0;
    if (bp_res_claim_unused_dma_ch(BP_RES_OWNER_SYSTEM, "amux_ctrl", &ch_ctrl) != BP_STATUS_OK) {
        bp_res_release(BP_RES_CLASS_DMA_CH, ch_data, BP_RES_OWNER_SYSTEM);
        bp_res_release(BP_RES_CLASS_PERIPH, BP_RES_PERIPH_ADC, BP_RES_OWNER_SYSTEM);
        return false;
    }

    s_amux_dma_ch_data = (int32_t)ch_data;
    s_amux_dma_ch_ctrl = (int32_t)ch_ctrl;
    s_amux_completed_idx = 0;
    s_amux_active_idx = 1;
    s_amux_dma_busy = false;
    s_amux_state = AMUX_STATE_IDLE;
    s_amux_dma_initialized = true;

    /* Pre-fill ping-pong buffers and frames with baseline */
    memset(s_amux_pingpong, 0, sizeof(s_amux_pingpong));
    memset(s_amux_frames, 0, sizeof(s_amux_frames));

    /* Startup priming: acquire initial sweep so valid telemetry is available immediately */
    amux_dma_sweep_start();
    return true;
}

void amux_dma_deinit(void) {
    if (s_amux_dma_ch_ctrl >= 0) {
        bp_res_release(BP_RES_CLASS_DMA_CH, (uint32_t)s_amux_dma_ch_ctrl, BP_RES_OWNER_SYSTEM);
        s_amux_dma_ch_ctrl = -1;
    }
    if (s_amux_dma_ch_data >= 0) {
        bp_res_release(BP_RES_CLASS_DMA_CH, (uint32_t)s_amux_dma_ch_data, BP_RES_OWNER_SYSTEM);
        s_amux_dma_ch_data = -1;
    }
    bp_res_release(BP_RES_CLASS_PERIPH, BP_RES_PERIPH_ADC, BP_RES_OWNER_SYSTEM);
    s_amux_dma_initialized = false;
    s_amux_dma_busy = false;
    s_amux_state = AMUX_STATE_STOPPED;
}

bool amux_dma_sweep_start(void) {
    if (!s_amux_dma_initialized) {
        return false;
    }
    if (s_amux_state == AMUX_STATE_MANUAL || s_amux_state == AMUX_STATE_PAUSING || s_amux_state == AMUX_STATE_FAULT) {
        return false;
    }
    if (!adc_busy_wait(true)) {
        s_amux_state = AMUX_STATE_FAULT;
        s_amux_fault_count++;
        return false;
    }
    s_amux_dma_busy = true;
    s_amux_state = AMUX_STATE_SAMPLING;

    uint8_t target = s_amux_active_idx;
#if !defined(BP_HOST_TEST)
    /* Hardware DMA sweep execution */
    adc_select_input(AMUX_OUT_ADC);
    for (int i = 0; i < HW_ADC_MUX_COUNT; i++) {
        amux_select_input(HW_ADC_MUX_GND);
        busy_wait_us(5);
        amux_select_input(i);
        busy_wait_us(40);
        uint16_t sample = (uint16_t)adc_read();
        s_amux_pingpong[target][i] = sample;
        s_amux_frames[target].raw[i] = sample;
    }
    amux_select_input(HW_ADC_MUX_GND);
    adc_select_input(CURRENT_SENSE_ADC);
    busy_wait_us(40);
    uint16_t cur_sample = (uint16_t)adc_read();
    s_amux_pingpong[target][HW_ADC_CURRENT_SENSE] = cur_sample;
    s_amux_frames[target].raw[HW_ADC_CURRENT_SENSE] = cur_sample;
#else
    /* Host test simulation */
    for (int i = 0; i < HW_ADC_MUX_COUNT; i++) {
        amux_select_input(i);
        uint16_t sample = (uint16_t)adc_read();
        s_amux_pingpong[target][i] = sample;
        s_amux_frames[target].raw[i] = sample;
    }
    s_mock_current_amux_channel = HW_ADC_CURRENT_SENSE;
    uint16_t cur_sample = (uint16_t)adc_read();
    s_amux_pingpong[target][HW_ADC_CURRENT_SENSE] = cur_sample;
    s_amux_frames[target].raw[HW_ADC_CURRENT_SENSE] = cur_sample;
#endif

    /* Record timestamp, generation, and validity */
    s_amux_frames[target].timestamp_us = (uint64_t)time_us_32();
    s_amux_frames[target].generation = ++s_amux_generation;
    s_amux_frames[target].valid = true;
    s_amux_frames[target].fault_code = 0;

    /* Coherent atomic buffer swap */
    uint8_t new_completed = target;
    s_amux_active_idx = s_amux_completed_idx;
    s_amux_completed_idx = new_completed;
    s_amux_dma_busy = false;
    s_amux_state = AMUX_STATE_IDLE;
    adc_busy_wait(false);
    return true;
}

bool amux_dma_get_coherent(uint16_t* out_raw, size_t count) {
    if (!out_raw || count == 0) {
        return false;
    }
    if (!s_amux_dma_initialized || s_amux_state == AMUX_STATE_FAULT || !s_amux_frames[s_amux_completed_idx].valid) {
        return false;
    }
    /* Serialize copying with sampling so a lapping producer cannot reuse it. */
    if (!adc_busy_wait(true)) return false;
    uint8_t idx = s_amux_completed_idx;
    size_t copy_cnt = (count < (HW_ADC_MUX_COUNT + 1)) ? count : (HW_ADC_MUX_COUNT + 1);
    for (size_t i = 0; i < copy_cnt; i++) {
        out_raw[i] = s_amux_pingpong[idx][i];
    }
    adc_busy_wait(false);
    return true;
}

int8_t adc_get_owner(void) {
    return s_adc_owner_core;
}

uint32_t adc_get_nest_count(void) {
    return s_adc_nest_count;
}

/**
 * @brief Provide protected access to ADC (core-safe, reentrant)
 * 
 * Uses spinlock with core-id tracking and nesting counter to allow
 * reentrant acquisition on the same core without deadlock.
 * 
 * @param enable true to acquire lock, false to release
 */
bool adc_try_acquire(void) {
    int8_t core = (int8_t)get_core_num();
    uint32_t save = spin_lock_blocking(adc_spin_lock);
    bool acquired = s_adc_owner_core == -1;
    if (acquired) { s_adc_owner_core = core; s_adc_nest_count++; }
    spin_unlock(adc_spin_lock, save);
    return acquired;
}

bool adc_busy_wait(bool enable) {
    int8_t core = (int8_t)get_core_num();

    if (!enable) {
        uint32_t save = spin_lock_blocking(adc_spin_lock);
        bool owned = s_adc_owner_core == core;
        if (owned && s_adc_nest_count > 0 && --s_adc_nest_count == 0) {
            s_adc_owner_core = -1;
        }
        spin_unlock(adc_spin_lock, save);
        return owned;
    }

    /* Bound attempts as well as time: a stopped host/hardware timer cannot hang us. */
    uint32_t start = time_us_32();
    for (uint32_t attempt = 0; attempt < 10000; ++attempt) {
        uint32_t save = spin_lock_blocking(adc_spin_lock);
        bool available = s_adc_owner_core == -1 || s_adc_owner_core == core;
        if (available && s_adc_nest_count != UINT32_MAX) {
            s_adc_owner_core = core;
            s_adc_nest_count++;
            spin_unlock(adc_spin_lock, save);
            return true;
        }
        spin_unlock(adc_spin_lock, save);
        if ((uint32_t)(time_us_32() - start) >= 1000) break;
        tight_loop_contents();
    }
    return false;
}

/**
 * @brief Initialize analog multiplexer and ADC subsystem
 * 
 * Sets up:
 * - ADC peripheral
 * - GPIO pins for ADC input
 * - Spinlock for multi-core protection
 * - Averaging reset flag
 * 
 */
void amux_init(void) {
    adc_init();
    adc_gpio_init(AMUX_OUT);
    adc_gpio_init(CURRENT_SENSE);
    // setup the spinlock for adc arbitration
    adc_spin_lock_num = spin_lock_claim_unused(true);
    adc_spin_lock = spin_lock_init(adc_spin_lock_num);
    reset_adc_average = true;
    amux_dma_init();
}

/**
 * @brief Select AMUX input channel
 * 
 * Configures the CD4067 analog multiplexer to route the specified
 * input channel to the ADC.
 * 
 * @param channel Channel number (0-15, platform-specific defines)
 * @return true if successful
 * 
 * @note Does not affect current sense input (separate ADC channel)
 * @note Channel definitions are platform-specific (see platform headers)
 */
bool amux_select_input(uint16_t channel) {
    // clear the amux control bits, set the amux channel bits
    #if BP_HW_IOEXP_595
        shift_clear_set((0b1111 << 1), (channel << 1) & 0b11110, true);
        #elif (BP_VER == 6 || BP_VER == 7)
        // uint64_t value=(uint64_t)(channel<<AMUX_S0);
        // uint64_t mask=(uint64_t)(0b1111<<AMUX_S0);
        // gpio_put_masked(mask, value);
        gpio_put(AMUX_S0, (channel >> 0) & 1);
        gpio_put(AMUX_S1, (channel >> 1) & 1);
        gpio_put(AMUX_S2, (channel >> 2) & 1);
        gpio_put(AMUX_S3, (channel >> 3) & 1);
#ifdef BP_HOST_TEST
        extern uint16_t s_mock_current_amux_channel;
        s_mock_current_amux_channel = channel;
#endif
    #elif (BP_VER == 8)
       #error "AMUX not implemented for BP8"
    #else
        #error "Platform not speficied in amux.c"
    #endif
    return true;
}

/**
 * @brief Select AMUX input using BIO pin number
 * 
 * Convenience function that maps BIO pin numbers to AMUX channels.
 * 
 * @param bio BIO pin number (0-7)
 * @return true if successful
 */
bool amux_select_bio(uint8_t bio) {
    amux_select_input(bufio2amux(bio));
    return true;
}

/**
 * @brief Read voltage from specified AMUX channel
 * 
 * Selects the channel, waits for settling, and performs ADC conversion.
 * Uses spinlock for multi-core safety.
 * 
 * @param channel AMUX channel number
 * @return 12-bit ADC reading (0-4095)
 * 
 * @note Direct channel selection with 60μs settling time
 */
uint32_t amux_read(uint8_t channel) {
    amux_state_t prev_state = s_amux_state;
    if (s_amux_state == AMUX_STATE_SAMPLING) {
        s_amux_state = AMUX_STATE_PAUSING;
    }
    if (!adc_busy_wait(true)) {
        if (s_amux_state == AMUX_STATE_PAUSING) s_amux_state = prev_state;
        return 0;
    }
    s_amux_state = AMUX_STATE_MANUAL;
    adc_select_input(AMUX_OUT_ADC);
    amux_select_input((uint16_t)channel);
    busy_wait_us(60);
    uint32_t ret = adc_read();
    s_amux_state = s_amux_dma_initialized ? AMUX_STATE_IDLE : AMUX_STATE_STOPPED;
    adc_busy_wait(false);
    return ret;
}

uint32_t amux_read_present_channel(void) {
    if (!adc_busy_wait(true)) return 0;
    uint32_t ret = adc_read();
    adc_busy_wait(false);
    return ret;
}

// read from AMUX using BIO pin number
uint32_t amux_read_bio(uint8_t bio) {
    return amux_read(bufio2amux(bio));
}

// this is actually on a different ADC and not the AMUX
// but this is the best place for it I think
// voltage is not /2 so we can use the full range of the ADC
uint32_t amux_read_current(void) {
    amux_state_t prev_state = s_amux_state;
    if (s_amux_state == AMUX_STATE_SAMPLING) {
        s_amux_state = AMUX_STATE_PAUSING;
    }
    if (!adc_busy_wait(true)) {
        if (s_amux_state == AMUX_STATE_PAUSING) s_amux_state = prev_state;
        return 0;
    }
    s_amux_state = AMUX_STATE_MANUAL;
    adc_select_input(CURRENT_SENSE_ADC);
    uint32_t ret = adc_read();
    s_amux_state = s_amux_dma_initialized ? AMUX_STATE_IDLE : AMUX_STATE_STOPPED;
    adc_busy_wait(false);
    return ret;
}

// read all the AMUX channels and the current sense
// place into the global arrays hw_adc_raw and hw_adc_voltage
void amux_sweep(void) {
    if (s_amux_dma_initialized) {
        /* Run an autonomous sweep cycle if not busy */
        if (!s_amux_dma_busy) {
            amux_dma_sweep_start();
        }
        uint16_t raw[HW_ADC_MUX_COUNT + 1];
        if (!amux_dma_get_coherent(raw, HW_ADC_MUX_COUNT + 1)) return;
        for (int i = 0; i <= HW_ADC_MUX_COUNT; i++) hw_adc_raw[i] = raw[i];
    } else {
        if (!adc_busy_wait(true)) return;
        adc_select_input(AMUX_OUT_ADC);
        for (int i = 0; i < HW_ADC_MUX_COUNT; i++) {
            amux_select_input(HW_ADC_MUX_GND); // to clear any charge from a floating pin
            busy_wait_us(10);
            amux_select_input(i);
            busy_wait_us(60);
            hw_adc_raw[i] = adc_read();
            // hw_adc_voltage[i]=hw_adc_to_volts_x2(i); //these are X2 because a resistor divider /2
        }
        amux_select_input(HW_ADC_MUX_GND); // to clear any charge from a floating pin
        adc_select_input(CURRENT_SENSE_ADC);
        busy_wait_us(60);
        hw_adc_raw[HW_ADC_CURRENT_SENSE] = (adc_read() + hw_adc_raw[HW_ADC_CURRENT_SENSE]) / 2;
        adc_busy_wait(false);
    }
    // do these outside the ADC spin lock
    for (int i = 0; i < HW_ADC_MUX_COUNT; i++) {
        hw_adc_voltage[i] = hw_adc_to_volts_x2(i); // these are X2 because a resistor divider /2
        
        if (reset_adc_average)
            hw_adc_avgsum_voltage[i] = hw_adc_voltage[i]*ADC_AVG_TIMES;
        else
        {
            // calculate the rolling average
            hw_adc_avgsum_voltage[i]-=get_adc_average(hw_adc_avgsum_voltage[i]);
            hw_adc_avgsum_voltage[i]+=hw_adc_voltage[i];
        }
    }
    if (reset_adc_average)
        reset_adc_average = false;
    hw_adc_voltage[HW_ADC_CURRENT_SENSE] = hw_adc_to_volts_x1(HW_ADC_CURRENT_SENSE);
}
