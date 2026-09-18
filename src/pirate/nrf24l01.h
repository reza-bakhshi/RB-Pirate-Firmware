/**
 * @file nrf24l01.h
 * @brief Minimal nRF24L01/nRF24L01+ driver for the Bus Pirate RF24 mode.
 *
 * @author Bus Pirate Project
 * @date 2026
 */

#ifndef BP_NRF24L01_H
#define BP_NRF24L01_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    NRF24_RATE_250KBPS = 0,
    NRF24_RATE_1MBPS = 1,
    NRF24_RATE_2MBPS = 2,
} nrf24_data_rate_t;

typedef enum {
    NRF24_CRC_OFF = 0,
    NRF24_CRC_8 = 8,
    NRF24_CRC_16 = 16,
} nrf24_crc_t;

typedef struct {
    uint8_t channel;
    uint8_t pipe;
    uint8_t address[5];
    uint8_t address_length;
    nrf24_data_rate_t data_rate;
    nrf24_crc_t crc;
    bool dynamic_payload;
    uint8_t payload_length;
    bool auto_ack;
} nrf24_config_t;

bool nrf24_init(uint8_t ce_pin);
void nrf24_deinit(void);
bool nrf24_present(void);
bool nrf24_set_channel(uint8_t channel);
uint8_t nrf24_get_channel(void);
bool nrf24_configure_rx(const nrf24_config_t *config);
bool nrf24_configure_tx(const nrf24_config_t *config);
bool nrf24_start_listening(void);
void nrf24_stop_listening(void);
bool nrf24_rpd_sample(uint8_t channel, uint32_t dwell_us);
bool nrf24_available(uint8_t *pipe);
bool nrf24_receive(uint8_t *data, size_t capacity, uint8_t *length);
bool nrf24_send(const uint8_t *data, uint8_t length, bool *acknowledged);
void nrf24_flush_rx(void);
void nrf24_flush_tx(void);
void nrf24_power_down(void);
bool nrf24_tx_start(uint8_t channel, const uint8_t *data, uint8_t length);
void nrf24_tx_stop(void);
bool nrf24_set_power_max(void);
bool nrf24_jam_start(uint8_t data_rate);
bool nrf24_jam_channel(uint8_t channel);
bool nrf24_sniff_start(uint8_t channel, uint8_t data_rate);
bool nrf24_sniff_read(uint8_t *data, size_t capacity, uint8_t *length, uint8_t *pipe);
bool nrf24_addr_scan(uint8_t channel, const uint8_t *address, uint8_t address_length, uint8_t data_rate);

#endif
