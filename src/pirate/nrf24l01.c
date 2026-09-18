/**
 * @file nrf24l01.c
 * @brief Minimal nRF24L01/nRF24L01+ driver for the Bus Pirate RF24 mode.
 *
 * Implements standard packet transmit/receive and RPD energy detection. It
 * deliberately does not implement constant-carrier or interference features.
 *
 * @author Bus Pirate Project
 * @date 2026
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "hardware/spi.h"
#include "pico/stdlib.h"
#include "pirate.h"
#include "pirate/bio.h"
#include "pirate/hwspi.h"
#include "pirate/nrf24l01.h"

#define NRF_CMD_R_REGISTER        0x00u
#define NRF_CMD_W_REGISTER        0x20u
#define NRF_CMD_R_RX_PAYLOAD      0x61u
#define NRF_CMD_W_TX_PAYLOAD      0xa0u
#define NRF_CMD_FLUSH_TX          0xe1u
#define NRF_CMD_FLUSH_RX          0xe2u
#define NRF_CMD_R_RX_PL_WID       0x60u
#define NRF_CMD_ACTIVATE          0x50u
#define NRF_CMD_NOP               0xffu

#define NRF_REG_CONFIG            0x00u
#define NRF_REG_EN_AA             0x01u
#define NRF_REG_EN_RXADDR         0x02u
#define NRF_REG_SETUP_AW          0x03u
#define NRF_REG_SETUP_RETR        0x04u
#define NRF_REG_RF_CH             0x05u
#define NRF_REG_RF_SETUP          0x06u
#define NRF_REG_STATUS            0x07u
#define NRF_REG_RPD               0x09u
#define NRF_REG_RX_ADDR_P0        0x0au
#define NRF_REG_RX_ADDR_P1        0x0bu
#define NRF_REG_TX_ADDR           0x10u
#define NRF_REG_RX_PW_P0          0x11u
#define NRF_REG_FIFO_STATUS       0x17u
#define NRF_REG_DYNPD             0x1cu
#define NRF_REG_FEATURE           0x1du

#define NRF_CONFIG_EN_CRC         (1u << 3)
#define NRF_CONFIG_CRCO           (1u << 2)
#define NRF_CONFIG_PWR_UP         (1u << 1)
#define NRF_CONFIG_PRIM_RX        (1u << 0)
#define NRF_STATUS_RX_DR          (1u << 6)
#define NRF_STATUS_TX_DS          (1u << 5)
#define NRF_STATUS_MAX_RT         (1u << 4)
#define NRF_STATUS_RX_P_NO_MASK   (7u << 1)
#define NRF_FIFO_RX_EMPTY         (1u << 0)
#define NRF_FEATURE_EN_DPL        (1u << 2)

/* The nRF24L01+ needs up to 1.5ms to transition from power-down to standby-I.
 * Use a little margin so the first packet/sample after configuration is not
 * lost while the oscillator is still starting. */
#define NRF_POWER_UP_DELAY_US      2000u

static uint8_t nrf_ce_pin;
static bool nrf_initialized;
static bool nrf_dynamic_payload;
static uint8_t nrf_fixed_payload_length = 32u;

static uint8_t transfer(uint8_t value) {
    return (uint8_t)hwspi_write_read(value);
}

static uint8_t command(uint8_t cmd) {
    hwspi_select();
    uint8_t status = transfer(cmd);
    hwspi_deselect();
    return status;
}

static uint8_t read_register(uint8_t reg) {
    hwspi_select();
    transfer(NRF_CMD_R_REGISTER | (reg & 0x1fu));
    uint8_t value = transfer(NRF_CMD_NOP);
    hwspi_deselect();
    return value;
}

static void read_register_bytes(uint8_t reg, uint8_t *data, uint8_t length) {
    hwspi_select();
    transfer(NRF_CMD_R_REGISTER | (reg & 0x1fu));
    for (uint8_t i = 0; i < length; i++) data[i] = transfer(NRF_CMD_NOP);
    hwspi_deselect();
}

static void write_register(uint8_t reg, uint8_t value) {
    hwspi_select();
    transfer(NRF_CMD_W_REGISTER | (reg & 0x1fu));
    transfer(value);
    hwspi_deselect();
}

static void write_register_bytes(uint8_t reg, const uint8_t *data, uint8_t length) {
    hwspi_select();
    transfer(NRF_CMD_W_REGISTER | (reg & 0x1fu));
    for (uint8_t i = 0; i < length; i++) transfer(data[i]);
    hwspi_deselect();
}

static uint8_t status(void) {
    return command(NRF_CMD_NOP);
}

static void clear_irqs(void) {
    write_register(NRF_REG_STATUS, NRF_STATUS_RX_DR | NRF_STATUS_TX_DS | NRF_STATUS_MAX_RT);
}

static uint8_t config_register_value(nrf24_crc_t crc, bool receiver) {
    uint8_t value = NRF_CONFIG_PWR_UP;
    if (receiver) value |= NRF_CONFIG_PRIM_RX;
    if (crc == NRF24_CRC_8) value |= NRF_CONFIG_EN_CRC;
    if (crc == NRF24_CRC_16) value |= NRF_CONFIG_EN_CRC | NRF_CONFIG_CRCO;
    return value;
}

static bool set_data_rate(nrf24_data_rate_t rate) {
    uint8_t value = read_register(NRF_REG_RF_SETUP) & (uint8_t)~((1u << 5) | (1u << 3));
    if (rate == NRF24_RATE_250KBPS) value |= 1u << 5;
    else if (rate == NRF24_RATE_2MBPS) value |= 1u << 3;
    else if (rate != NRF24_RATE_1MBPS) return false;
    /* 0dBm output, LNA enabled. */
    value = (value & (uint8_t)~(3u << 1)) | (3u << 1) | 1u;
    write_register(NRF_REG_RF_SETUP, value);
    return (read_register(NRF_REG_RF_SETUP) & ((1u << 5) | (1u << 3))) ==
           (value & ((1u << 5) | (1u << 3)));
}

static bool enable_dynamic_payload(uint8_t pipe_mask) {
    write_register(NRF_REG_FEATURE, NRF_FEATURE_EN_DPL);
    if (!(read_register(NRF_REG_FEATURE) & NRF_FEATURE_EN_DPL)) {
        hwspi_select();
        transfer(NRF_CMD_ACTIVATE);
        transfer(0x73u);
        hwspi_deselect();
        write_register(NRF_REG_FEATURE, NRF_FEATURE_EN_DPL);
    }
    write_register(NRF_REG_DYNPD, pipe_mask & 0x3fu);
    return (read_register(NRF_REG_FEATURE) & NRF_FEATURE_EN_DPL) != 0;
}

static bool apply_common_config(const nrf24_config_t *config) {
    if (!config || config->channel > 125u || config->pipe > 5u ||
        config->address_length < 3u || config->address_length > 5u ||
        config->payload_length < 1u || config->payload_length > 32u) return false;

    nrf24_stop_listening();
    write_register(NRF_REG_SETUP_AW, config->address_length - 2u);
    if ((read_register(NRF_REG_SETUP_AW) & 3u) != config->address_length - 2u) return false;
    if (!nrf24_set_channel(config->channel) || !set_data_rate(config->data_rate)) return false;

    nrf_dynamic_payload = config->dynamic_payload;
    nrf_fixed_payload_length = config->payload_length;
    if (config->dynamic_payload) {
        if (!enable_dynamic_payload(1u << config->pipe)) return false;
    } else {
        write_register(NRF_REG_DYNPD, 0u);
        write_register(NRF_REG_FEATURE, 0u);
    }
    return true;
}

bool nrf24_init(uint8_t ce_pin) {
    nrf_ce_pin = ce_pin;
    bio_output(nrf_ce_pin);
    bio_put(nrf_ce_pin, false);
    spi_init(M_SPI_PORT, 8000000u);
    hwspi_init(8u, 0u, 0u);
    hwspi_deselect();
    sleep_ms(5u);

    nrf_initialized = true;
    if (!nrf24_present()) {
        nrf24_deinit();
        return false;
    }
    nrf24_flush_rx();
    nrf24_flush_tx();
    clear_irqs();
    write_register(NRF_REG_CONFIG, NRF_CONFIG_EN_CRC | NRF_CONFIG_CRCO);
    write_register(NRF_REG_EN_AA, 0x3fu);
    write_register(NRF_REG_EN_RXADDR, 0x03u);
    write_register(NRF_REG_SETUP_RETR, 0x3fu); /* 1000us delay, 15 retries. */
    return true;
}

void nrf24_deinit(void) {
    if (nrf_initialized) {
        bio_put(nrf_ce_pin, false);
        write_register(NRF_REG_CONFIG, 0u);
        hwspi_deinit();
    }
    nrf_initialized = false;
}

bool nrf24_present(void) {
    if (!nrf_initialized) return false;
    uint8_t original = read_register(NRF_REG_RF_CH);
    if (original == 0xffu) return false;
    uint8_t probe = original == 42u ? 43u : 42u;
    write_register(NRF_REG_RF_CH, probe);
    bool present = read_register(NRF_REG_RF_CH) == probe;
    write_register(NRF_REG_RF_CH, original <= 125u ? original : 76u);
    return present;
}

bool nrf24_set_channel(uint8_t channel) {
    if (!nrf_initialized || channel > 125u) return false;
    write_register(NRF_REG_RF_CH, channel);
    return read_register(NRF_REG_RF_CH) == channel;
}

uint8_t nrf24_get_channel(void) {
    return nrf_initialized ? read_register(NRF_REG_RF_CH) : 0u;
}

bool nrf24_configure_rx(const nrf24_config_t *config) {
    if (!nrf_initialized || !apply_common_config(config)) return false;
    uint8_t pipe_bit = 1u << config->pipe;
    write_register(NRF_REG_EN_AA, config->auto_ack ? pipe_bit : 0u);
    write_register(NRF_REG_EN_RXADDR, pipe_bit);

    if (config->pipe <= 1u) {
        write_register_bytes(NRF_REG_RX_ADDR_P0 + config->pipe, config->address, config->address_length);
    } else {
        /* Pipes 2-5 share the upper address bytes with pipe 1. */
        write_register_bytes(NRF_REG_RX_ADDR_P1, config->address, config->address_length);
        write_register(NRF_REG_RX_ADDR_P0 + config->pipe, config->address[0]);
    }
    write_register(NRF_REG_RX_PW_P0 + config->pipe,
                   config->dynamic_payload ? 0u : config->payload_length);
    write_register(NRF_REG_CONFIG, config_register_value(config->crc, true));
    clear_irqs();
    nrf24_flush_rx();
    sleep_us(NRF_POWER_UP_DELAY_US);
    return true;
}

bool nrf24_configure_tx(const nrf24_config_t *config) {
    if (!nrf_initialized || !apply_common_config(config)) return false;
    write_register(NRF_REG_EN_AA, config->auto_ack ? 1u : 0u);
    write_register(NRF_REG_EN_RXADDR, 1u);
    write_register_bytes(NRF_REG_TX_ADDR, config->address, config->address_length);
    /* Pipe 0 must mirror TX_ADDR to receive enhanced-shockburst ACKs. */
    write_register_bytes(NRF_REG_RX_ADDR_P0, config->address, config->address_length);
    write_register(NRF_REG_RX_PW_P0, config->dynamic_payload ? 0u : config->payload_length);
    write_register(NRF_REG_CONFIG, config_register_value(config->crc, false));
    clear_irqs();
    nrf24_flush_tx();
    sleep_us(NRF_POWER_UP_DELAY_US);
    return true;
}

bool nrf24_start_listening(void) {
    if (!nrf_initialized) return false;
    bio_put(nrf_ce_pin, false);
    uint8_t config = read_register(NRF_REG_CONFIG) | NRF_CONFIG_PWR_UP | NRF_CONFIG_PRIM_RX;
    write_register(NRF_REG_CONFIG, config);
    clear_irqs();
    bio_put(nrf_ce_pin, true);
    sleep_us(150u);
    return true;
}

void nrf24_stop_listening(void) {
    if (nrf_initialized) bio_put(nrf_ce_pin, false);
}

bool nrf24_rpd_sample(uint8_t channel, uint32_t dwell_us) {
    if (!nrf_initialized || channel > 125u) return false;
    nrf24_stop_listening();
    if (!nrf24_set_channel(channel)) return false;
    uint8_t config = read_register(NRF_REG_CONFIG) | NRF_CONFIG_PWR_UP | NRF_CONFIG_PRIM_RX;
    write_register(NRF_REG_CONFIG, config);
    bio_put(nrf_ce_pin, true);
    sleep_us(dwell_us < 140u ? 140u : dwell_us);
    bio_put(nrf_ce_pin, false);
    return (read_register(NRF_REG_RPD) & 1u) != 0;
}

bool nrf24_available(uint8_t *pipe) {
    if (!nrf_initialized) return false;
    uint8_t state = status();
    uint8_t pipe_number = (state & NRF_STATUS_RX_P_NO_MASK) >> 1;
    if (pipe_number <= 5u) {
        if (pipe) *pipe = pipe_number;
        return true;
    }
    bool available = !(read_register(NRF_REG_FIFO_STATUS) & NRF_FIFO_RX_EMPTY);
    if (available && pipe) *pipe = 7u;
    return available;
}

bool nrf24_receive(uint8_t *data, size_t capacity, uint8_t *length) {
    if (!nrf_initialized || !data || !length || !capacity || !nrf24_available(NULL)) return false;
    uint8_t payload_length = nrf_fixed_payload_length;
    if (nrf_dynamic_payload) {
        hwspi_select();
        transfer(NRF_CMD_R_RX_PL_WID);
        payload_length = transfer(NRF_CMD_NOP);
        hwspi_deselect();
        if (!payload_length || payload_length > 32u) {
            nrf24_flush_rx();
            clear_irqs();
            return false;
        }
    }
    uint8_t copy_length = payload_length > capacity ? (uint8_t)capacity : payload_length;
    hwspi_select();
    transfer(NRF_CMD_R_RX_PAYLOAD);
    for (uint8_t i = 0; i < payload_length; i++) {
        uint8_t value = transfer(NRF_CMD_NOP);
        if (i < copy_length) data[i] = value;
    }
    hwspi_deselect();
    write_register(NRF_REG_STATUS, NRF_STATUS_RX_DR);
    *length = copy_length;
    return true;
}

bool nrf24_send(const uint8_t *data, uint8_t length, bool *acknowledged) {
    if (!nrf_initialized || !data || !length || length > 32u) return false;
    nrf24_stop_listening();
    uint8_t config = (read_register(NRF_REG_CONFIG) | NRF_CONFIG_PWR_UP) & (uint8_t)~NRF_CONFIG_PRIM_RX;
    write_register(NRF_REG_CONFIG, config);
    clear_irqs();
    nrf24_flush_tx();
    hwspi_select();
    transfer(NRF_CMD_W_TX_PAYLOAD);
    for (uint8_t i = 0; i < length; i++) transfer(data[i]);
    if (!nrf_dynamic_payload) {
        for (uint8_t i = length; i < nrf_fixed_payload_length; i++) transfer(0u);
    }
    hwspi_deselect();
    bio_put(nrf_ce_pin, true);
    busy_wait_us_32(15u);
    bio_put(nrf_ce_pin, false);

    absolute_time_t timeout = make_timeout_time_ms(25u);
    uint8_t state;
    do {
        state = status();
        if (state & (NRF_STATUS_TX_DS | NRF_STATUS_MAX_RT)) break;
        tight_loop_contents();
    } while (!time_reached(timeout));

    bool completed = (state & (NRF_STATUS_TX_DS | NRF_STATUS_MAX_RT)) != 0;
    if (acknowledged) *acknowledged = (state & NRF_STATUS_TX_DS) != 0;
    write_register(NRF_REG_STATUS, NRF_STATUS_TX_DS | NRF_STATUS_MAX_RT);
    if (state & NRF_STATUS_MAX_RT) nrf24_flush_tx();
    return completed;
}

void nrf24_flush_rx(void) {
    if (nrf_initialized) (void)command(NRF_CMD_FLUSH_RX);
}

void nrf24_flush_tx(void) {
    if (nrf_initialized) (void)command(NRF_CMD_FLUSH_TX);
}

void nrf24_power_down(void) {
    if (!nrf_initialized) return;
    nrf24_stop_listening();
    write_register(NRF_REG_CONFIG, read_register(NRF_REG_CONFIG) & (uint8_t)~NRF_CONFIG_PWR_UP);
}
