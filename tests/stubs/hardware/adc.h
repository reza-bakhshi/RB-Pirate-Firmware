#ifndef _HARDWARE_ADC_H
#define _HARDWARE_ADC_H
#include <stdint.h>
#include <stdbool.h>
#include "hardware/sync.h"

extern uint16_t s_mock_adc_val;

static inline void adc_init(void) {}
static inline void adc_gpio_init(uint gpio) { (void)gpio; }
static inline void adc_select_input(uint input) { (void)input; }
static inline uint16_t adc_read(void) { return s_mock_adc_val; }

#endif
