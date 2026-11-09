#ifndef MOCK_PIRATE_HWSPI_H
#define MOCK_PIRATE_HWSPI_H

#include <stdint.h>
#include <stdbool.h>

void hwspi_init(uint8_t data_bits, uint8_t cpol, uint8_t cpha);
void hwspi_deinit(void);
void hwspi_select(void);
void hwspi_deselect(void);
uint32_t hwspi_write_read(uint32_t data);

#endif
