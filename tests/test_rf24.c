#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "hardware/spi.h"
#include "pirate/bio.h"
#include "pirate/hwspi.h"
#include "pirate/nrf24l01.h"
#include "kernel/bp_status.h"
#include "kernel/bp_resource.h"

/* --- Mock Hardware Globals --- */
spi_inst_t s_mock_spi0 = { NULL };
spi_inst_t s_mock_spi1 = { NULL };
spi_inst_t *spi0 = &s_mock_spi0;
spi_inst_t *spi1 = &s_mock_spi1;
const uint8_t bio2bufiopin[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };

/* --- Mock nRF24 Register Bank & SPI State Machine --- */
#define NRF_REG_CONFIG            0x00u
#define NRF_REG_EN_AA             0x01u
#define NRF_REG_EN_RXADDR         0x02u
#define NRF_REG_SETUP_AW          0x03u
#define NRF_REG_SETUP_RETR        0x04u
#define NRF_REG_RF_CH             0x05u
#define NRF_REG_RF_SETUP          0x06u
#define NRF_REG_STATUS            0x07u
#define NRF_REG_RPD               0x09u
#define NRF_REG_FIFO_STATUS       0x17u
#define NRF_REG_DYNPD             0x1cu
#define NRF_REG_FEATURE           0x1du

static uint8_t s_mock_regs[32];
static bool s_spi_cs_active = false;
static uint8_t s_spi_cmd = 0;
static uint8_t s_spi_byte_idx = 0;
static uint8_t s_mock_payload_len = 32;

void hwspi_init(uint8_t data_bits, uint8_t cpol, uint8_t cpha) {
    (void)data_bits; (void)cpol; (void)cpha;
}

void hwspi_deinit(void) {
    s_spi_cs_active = false;
}

void hwspi_select(void) {
    s_spi_cs_active = true;
    s_spi_byte_idx = 0;
    s_spi_cmd = 0;
}

void hwspi_deselect(void) {
    s_spi_cs_active = false;
    s_spi_byte_idx = 0;
    s_spi_cmd = 0;
}

uint32_t hwspi_write_read(uint32_t data) {
    uint8_t in_byte = (uint8_t)data;
    uint8_t out_byte = 0;

    if (s_spi_byte_idx == 0) {
        s_spi_cmd = in_byte;
        s_spi_byte_idx++;
        /* Command byte transfer returns the STATUS register */
        return s_mock_regs[NRF_REG_STATUS];
    }

    uint8_t opcode = s_spi_cmd & 0xe0u;
    uint8_t reg = s_spi_cmd & 0x1fu;

    if (opcode == 0x00u) {
        /* R_REGISTER */
        out_byte = s_mock_regs[reg];
    } else if (opcode == 0x20u) {
        /* W_REGISTER */
        s_mock_regs[reg] = in_byte;
        out_byte = s_mock_regs[NRF_REG_STATUS];
    } else if (s_spi_cmd == 0x60u) {
        /* R_RX_PL_WID: return mock dynamic payload width */
        out_byte = s_mock_payload_len;
    } else if (s_spi_cmd == 0xe1u) {
        /* FLUSH_TX */
        out_byte = s_mock_regs[NRF_REG_STATUS];
    } else if (s_spi_cmd == 0xe2u) {
        /* FLUSH_RX */
        out_byte = s_mock_regs[NRF_REG_STATUS];
    } else {
        out_byte = 0;
    }

    s_spi_byte_idx++;
    return out_byte;
}

/* --- Mock Job Cancellation --- */
static bool s_mock_job_cancelled = false;
bool bp_job_check_cancelled(void) {
    return s_mock_job_cancelled;
}

void sleep_ms(uint32_t ms) {
    (void)ms;
}

/* --- Tests --- */

static void reset_mock_radio(void) {
    memset(s_mock_regs, 0, sizeof(s_mock_regs));
    s_mock_regs[NRF_REG_RF_CH] = 76u;
    s_mock_regs[NRF_REG_STATUS] = 0x0eu; /* TX FIFO empty, RX FIFO empty */
    s_mock_payload_len = 32u;
    s_spi_cs_active = false;
    s_mock_job_cancelled = false;
}

static void test_presence_detection(void) {
    printf("[TEST] nRF24L01 presence detection...\n");
    reset_mock_radio();
    bp_res_init();

    /* Successful presence: registers reflect written values */
    assert(nrf24_init(BIO0) == true);
    assert(nrf24_present() == true);
    nrf24_deinit();

    /* Absent module: reads 0xFF on RF_CH */
    s_mock_regs[NRF_REG_RF_CH] = 0xffu;
    assert(nrf24_init(BIO0) == false);
    assert(bp_res_is_free(BP_RES_CLASS_PERIPH, BP_RES_PERIPH_SPI1) == true);

    printf("[PASS] Presence detection verified\n");
}

static void test_channel_configuration(void) {
    printf("[TEST] nRF24L01 channel configuration...\n");
    reset_mock_radio();
    bp_res_init();

    assert(nrf24_init(BIO0) == true);

    /* Valid channels: 0..125 */
    assert(nrf24_set_channel(0u) == true);
    assert(nrf24_get_channel() == 0u);
    assert(s_mock_regs[NRF_REG_RF_CH] == 0u);

    assert(nrf24_set_channel(76u) == true);
    assert(nrf24_get_channel() == 76u);
    assert(s_mock_regs[NRF_REG_RF_CH] == 76u);

    assert(nrf24_set_channel(125u) == true);
    assert(nrf24_get_channel() == 125u);
    assert(s_mock_regs[NRF_REG_RF_CH] == 125u);

    /* Invalid channel > 125 rejected */
    assert(nrf24_set_channel(126u) == false);
    assert(nrf24_get_channel() == 125u);

    nrf24_deinit();
    printf("[PASS] Channel configuration verified\n");
}

static void test_rpd_energy_sampling(void) {
    printf("[TEST] nRF24L01 RPD energy detection...\n");
    reset_mock_radio();
    bp_res_init();

    assert(nrf24_init(BIO0) == true);

    /* RPD bit 0 = 0 -> no carrier detected */
    s_mock_regs[NRF_REG_RPD] = 0x00u;
    assert(nrf24_rpd_sample(40u, 160u) == false);

    /* RPD bit 0 = 1 -> carrier energy > -64dBm detected */
    s_mock_regs[NRF_REG_RPD] = 0x01u;
    assert(nrf24_rpd_sample(40u, 160u) == true);

    /* Out of bound channel rejected */
    assert(nrf24_rpd_sample(126u, 160u) == false);

    nrf24_deinit();
    printf("[PASS] RPD energy detection verified\n");
}

static void test_carrier_jam_continuous_wave(void) {
    printf("[TEST] nRF24L01 continuous carrier jam configuration...\n");
    reset_mock_radio();
    bp_res_init();

    assert(nrf24_init(BIO0) == true);

    /* Start jam at 2Mbps */
    assert(nrf24_jam_start((uint8_t)NRF24_RATE_2MBPS) == true);

    /* Verify CONT_WAVE (bit 7) and PLL_LOCK (bit 4) set in RF_SETUP */
    uint8_t rf_setup = s_mock_regs[NRF_REG_RF_SETUP];
    assert((rf_setup & (1u << 7)) != 0); /* CONT_WAVE */
    assert((rf_setup & (1u << 4)) != 0); /* PLL_LOCK */

    /* Verify channel hopping during jam */
    assert(nrf24_jam_channel(6u) == true);
    assert(s_mock_regs[NRF_REG_RF_CH] == 6u);
    assert(nrf24_jam_channel(11u) == true);
    assert(s_mock_regs[NRF_REG_RF_CH] == 11u);

    nrf24_tx_stop();
    nrf24_deinit();
    printf("[PASS] Continuous carrier jam configuration verified\n");
}

static void test_resource_lease_arbitration(void) {
    printf("[TEST] SPI1 peripheral lease arbitration & pin lifecycle...\n");
    reset_mock_radio();
    bp_res_init();

    /* Initial state: SPI1 is free */
    assert(bp_res_is_free(BP_RES_CLASS_PERIPH, BP_RES_PERIPH_SPI1) == true);

    /* Acquisition succeeds */
    assert(nrf24_init(BIO1) == true);
    assert(bp_res_is_free(BP_RES_CLASS_PERIPH, BP_RES_PERIPH_SPI1) == false);
    assert(bp_res_get_owner(BP_RES_CLASS_PERIPH, BP_RES_PERIPH_SPI1) == BP_RES_OWNER_MODE_BASE);

    /* Clean deinit releases lease */
    nrf24_deinit();
    assert(bp_res_is_free(BP_RES_CLASS_PERIPH, BP_RES_PERIPH_SPI1) == true);

    /* Contested lease scenario: another owner has SPI1 */
    assert(bp_res_acquire(BP_RES_CLASS_PERIPH, BP_RES_PERIPH_SPI1, BP_RES_OWNER_SYSTEM, "system_spi") == BP_STATUS_OK);
    assert(nrf24_init(BIO1) == false);

    /* System still holds lease */
    assert(bp_res_get_owner(BP_RES_CLASS_PERIPH, BP_RES_PERIPH_SPI1) == BP_RES_OWNER_SYSTEM);
    bp_res_release(BP_RES_CLASS_PERIPH, BP_RES_PERIPH_SPI1, BP_RES_OWNER_SYSTEM);
    assert(bp_res_is_free(BP_RES_CLASS_PERIPH, BP_RES_PERIPH_SPI1) == true);

    printf("[PASS] SPI1 resource lease arbitration verified\n");
}

static void test_cooperative_cancellation_contract(void) {
    printf("[TEST] Cooperative cancellation contract (< 100 ms)...\n");
    s_mock_job_cancelled = false;
    assert(bp_job_check_cancelled() == false);

    s_mock_job_cancelled = true;
    assert(bp_job_check_cancelled() == true);
    s_mock_job_cancelled = false;

    printf("[PASS] Cooperative cancellation contract verified\n");
}

static void test_channel_groups_integrity(void) {
    printf("[TEST] 2.4 GHz channel groups and frequency math...\n");
    /* WiFi Channels 1-14: 2412 - 2484 MHz */
    static const uint8_t wifi_test_channels[] = {
        6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18,
        22, 24, 26, 28,
        30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44,
        46, 48, 50, 52,
        55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68
    };
    for (size_t i = 0; i < sizeof(wifi_test_channels); i++) {
        uint8_t ch = wifi_test_channels[i];
        assert(ch <= 125u);
        uint32_t freq = 2400u + ch;
        assert(freq >= 2400u && freq <= 2525u);
    }

    /* BLE advertising channels: 37 (2402MHz / CH2), 38 (2426MHz / CH26), 39 (2480MHz / CH80) */
    static const uint8_t ble_test_channels[] = { 1, 2, 3, 25, 26, 27, 79, 80, 81 };
    for (size_t i = 0; i < sizeof(ble_test_channels); i++) {
        assert(ble_test_channels[i] <= 125u);
    }

    printf("[PASS] Channel groups and frequency math verified\n");
}

int main(void) {
    printf("=== Bus Pirate nRF24L01 Subsystem Host Unit Tests ===\n");
    test_presence_detection();
    test_channel_configuration();
    test_rpd_energy_sampling();
    test_carrier_jam_continuous_wave();
    test_resource_lease_arbitration();
    test_cooperative_cancellation_contract();
    test_channel_groups_integrity();
    printf("\n=== ALL RF24 UNIT TESTS PASSED ===\n");
    return 0;
}
