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

static inline uint32_t spi_set_baudrate(spi_inst_t *spi, uint32_t baudrate) {
    (void)spi;
    return baudrate;
}

static inline int spi_write_read_blocking(spi_inst_t *spi, const uint8_t *src, uint8_t *dst, size_t len) {
    (void)spi;
    (void)src;
    (void)dst;
    return (int)len;
}

static inline int spi_read_blocking(spi_inst_t *spi, uint8_t repeated_tx_data, uint8_t *dst, size_t len) {
    (void)spi;
    (void)repeated_tx_data;
    (void)dst;
    return (int)len;
}

static inline int spi_write_blocking(spi_inst_t *spi, const uint8_t *src, size_t len) {
    (void)spi;
    (void)src;
    return (int)len;
}

#endif
