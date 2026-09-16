#ifndef BP_CC1101_H
#define BP_CC1101_H

#include <stdbool.h>
#include <stdint.h>

bool cc1101_init(uint32_t frequency_khz);
void cc1101_deinit(void);
bool cc1101_present(uint8_t *part, uint8_t *version);
bool cc1101_set_frequency(uint32_t frequency_khz);
bool cc1101_set_ook_bandwidth(uint16_t bandwidth_khz);
bool cc1101_rx(void);
bool cc1101_tx(void);
bool cc1101_idle(void);
int16_t cc1101_rssi(void);

#endif
