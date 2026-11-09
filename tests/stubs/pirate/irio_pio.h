// stub
#ifndef HOST_IRIO_PIO
#define HOST_IRIO_PIO
#include <stdint.h>
#include <stdbool.h>
void irio_pio_tx_frame_write(float mod_freq, uint16_t pairs, uint32_t *buffer);
bool irio_pio_validate_pairs(uint16_t pairs, const uint32_t *buffer);
#endif
