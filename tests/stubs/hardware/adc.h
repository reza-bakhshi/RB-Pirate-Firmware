#ifndef _HARDWARE_ADC_H
#define _HARDWARE_ADC_H
#include <stdint.h>
#include <stdbool.h>
#include "hardware/sync.h"

extern uint16_t s_mock_adc_val;
extern uint16_t s_mock_channel_adc[16];
extern bool s_mock_use_channel_adc;
extern uint16_t s_mock_current_amux_channel;

static inline void adc_init(void) {}
static inline void adc_gpio_init(uint gpio) { (void)gpio; }
static inline void adc_select_input(uint input) { (void)input; }
static inline uint16_t adc_read(void) {
    if (s_mock_use_channel_adc && s_mock_current_amux_channel < 16) {
        return s_mock_channel_adc[s_mock_current_amux_channel];
    }
    return s_mock_adc_val;
}

#endif
