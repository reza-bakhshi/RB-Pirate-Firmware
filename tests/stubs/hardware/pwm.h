#ifndef _HARDWARE_PWM_H
#define _HARDWARE_PWM_H
#include <stdint.h>
#include <stdbool.h>
#include "hardware/sync.h"

static inline void pwm_set_chan_level(uint slice_num, uint chan, uint16_t level) { (void)slice_num; (void)chan; (void)level; }
static inline void pwm_set_enabled(uint slice_num, bool enabled) { (void)slice_num; (void)enabled; }

#endif
