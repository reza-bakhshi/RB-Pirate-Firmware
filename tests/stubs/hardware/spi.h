#ifndef MOCK_HARDWARE_SPI_H
#define MOCK_HARDWARE_SPI_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct spi_inst {
    void *hw;
} spi_inst_t;

extern spi_inst_t *spi0;
extern spi_inst_t *spi1;

static inline uint32_t spi_init(spi_inst_t *spi, uint32_t baudrate) {
    (void)spi;
    (void)baudrate;
    return baudrate;
}

static inline void spi_deinit(spi_inst_t *spi) {
    (void)spi;
}

#endif
