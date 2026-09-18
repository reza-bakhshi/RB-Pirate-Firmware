/**
 * @file pn532.c
 * @brief PN532 RFID/NFC Controller Driver for Bus Pirate (High-Speed UART)
 * @details Implements communication with NXP PN532 over High-Speed UART (HSU).
 * @author Bus Pirate Project
 * @date 2026
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "pirate.h"
#include "pirate/bio.h"
#include "pirate/pn532.h"

static uart_inst_t *pn532_uart = NULL;
static bool pn532_is_init_flag = false;
static bool pn532_is_ready_flag = false;
static uint8_t pn532_tx_bio_pin = BIO4;
static uint8_t pn532_rx_bio_pin = BIO5;
static uint32_t pn532_baudrate = 115200;

// Diagnostic counters
static uint32_t pn532_diag_bytes_rx = 0;
static uint8_t pn532_diag_last_bytes[16] = { 0 };
static uint8_t pn532_diag_last_bytes_count = 0;
static uint32_t pn532_diag_uart_errors = 0;
static bool pn532_diag_rx_line_high = true;

uint32_t pn532_get_diag_rx_count(void) {
    return pn532_diag_bytes_rx;
}

uint32_t pn532_get_diag_uart_errors(void) {
    return pn532_diag_uart_errors;
}

bool pn532_get_diag_rx_line_high(void) {
    return pn532_diag_rx_line_high;
}

void pn532_get_diag_last_bytes(uint8_t *buf, uint8_t *len) {
    if (buf && len) {
        uint8_t copy_len = pn532_diag_last_bytes_count;
        if (copy_len > *len) copy_len = *len;
        memcpy(buf, pn532_diag_last_bytes, copy_len);
        *len = copy_len;
    }
}

static const uint8_t common_mifare_keys[][6] = {
    { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }, // Factory default
    { 0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5 }, // NFC forum MAD
    { 0xD3, 0xF7, 0xD3, 0xF7, 0xD3, 0xF7 }, // NDEF default key
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, // Blank / Zero key
    { 0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5 },
    { 0x4D, 0x3A, 0x99, 0xC3, 0x51, 0xDD },
    { 0x1A, 0x98, 0x2C, 0x7E, 0x45, 0x9A },
    { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF },
    { 0x71, 0x4C, 0x5C, 0x88, 0x6E, 0x97 },
    { 0x58, 0x7E, 0xE5, 0xF9, 0x35, 0x0F },
    { 0xA0, 0x47, 0x8C, 0xC3, 0x90, 0x91 },
    { 0x53, 0x3C, 0xB6, 0xC7, 0x23, 0xF6 },
    { 0x8F, 0xD0, 0xA4, 0xF2, 0x56, 0xE9 },
    { 0xA6, 0x45, 0x98, 0xA7, 0x74, 0x78 },
    { 0x26, 0x94, 0x0B, 0x21, 0xFF, 0x5D },
    { 0xFC, 0x00, 0x01, 0x87, 0x78, 0xF7 }
};
static const uint32_t common_keys_count = count_of(common_mifare_keys);

static void pn532_uart_capture_status(void) {
    if (!pn532_uart) return;

    pn532_diag_uart_errors |= uart_get_hw(pn532_uart)->rsr & 0x0Fu;
    pn532_diag_rx_line_high = gpio_get(bio2bufiopin[pn532_rx_bio_pin]);
}

static void pn532_uart_flush(void) {
    if (!pn532_uart) return;

    pn532_uart_capture_status();
    while (uart_is_readable(pn532_uart)) {
        (void)uart_getc(pn532_uart);
    }
    // UARTRSR is also the error-clear register. Any write clears framing,
    // parity, break and overrun state before the next transaction.
    uart_get_hw(pn532_uart)->rsr = 0;
}

static void pn532_hardware_reinit(void) {
    if (!pn532_uart) return;

    // Reset PL011 UART hardware, baud rate, and clear FIFOs
    uart_init(pn532_uart, pn532_baudrate);
    uart_set_format(pn532_uart, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(pn532_uart, true);
    uart_set_hw_flow(pn532_uart, false, false);

    // Ensure buffered IO pins and directions are configured
    bio_buf_output(pn532_tx_bio_pin);
    bio_buf_input(pn532_rx_bio_pin);

    bio_set_function(pn532_tx_bio_pin, GPIO_FUNC_UART);
    bio_set_function(pn532_rx_bio_pin, GPIO_FUNC_UART);

    gpio_set_outover(bio2bufiopin[pn532_tx_bio_pin], GPIO_OVERRIDE_NORMAL);
    gpio_set_inover(bio2bufiopin[pn532_rx_bio_pin], GPIO_OVERRIDE_NORMAL);

    // Drain any garbage bytes from startup
    pn532_uart_flush();
}

static bool pn532_uart_read_byte(uint8_t *b, uint32_t timeout_ms) {
    if (!pn532_uart) return false;
    uint32_t start = to_ms_since_boot(get_absolute_time());
    while (!uart_is_readable(pn532_uart)) {
        pn532_uart_capture_status();
        if ((to_ms_since_boot(get_absolute_time()) - start) >= timeout_ms) {
            return false;
        }
        tight_loop_contents();
    }
    *b = (uint8_t)uart_getc(pn532_uart);

    pn532_diag_bytes_rx++;
    if (pn532_diag_last_bytes_count < sizeof(pn532_diag_last_bytes)) {
        pn532_diag_last_bytes[pn532_diag_last_bytes_count++] = *b;
    } else {
        memmove(&pn532_diag_last_bytes[0], &pn532_diag_last_bytes[1], sizeof(pn532_diag_last_bytes) - 1);
        pn532_diag_last_bytes[sizeof(pn532_diag_last_bytes) - 1] = *b;
    }
    return true;
}

// Send a PN532 frame over High-Speed UART. On the first transaction after
// power-up, prepend the HSU wake sequence in the same UART transfer as the
// command frame, as recommended by the Elechouse reference implementation.
static bool pn532_send_command_frame(const uint8_t *cmd, uint8_t len, bool wakeup) {
    if (!pn532_uart || len > 64) return false;
    static const uint8_t wakeup_preamble[] = {
        0x55, 0x55,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    uint8_t pkt[sizeof(wakeup_preamble) + 8 + 64];
    uint8_t offset = 0;
    uint8_t packet_len = len + 1; // includes TFI (0xD4)

    if (wakeup) {
        memcpy(pkt, wakeup_preamble, sizeof(wakeup_preamble));
        offset = sizeof(wakeup_preamble);
    }

    pkt[offset + 0] = PN532_PREAMBLE;   // 0x00
    pkt[offset + 1] = PN532_STARTCODE1; // 0x00
    pkt[offset + 2] = PN532_STARTCODE2; // 0xFF
    pkt[offset + 3] = packet_len;
    pkt[offset + 4] = (uint8_t)(~packet_len + 1);
    pkt[offset + 5] = PN532_HOSTTOPN532; // 0xD4

    uint8_t sum = PN532_HOSTTOPN532;
    for (uint8_t i = 0; i < len; i++) {
        pkt[offset + 6 + i] = cmd[i];
        sum += cmd[i];
    }
    pkt[offset + 6 + len] = (uint8_t)(~sum + 1);
    pkt[offset + 7 + len] = PN532_POSTAMBLE; // 0x00

    pn532_uart_flush();
    uart_write_blocking(pn532_uart, pkt, offset + 8 + len);
    // uart_write_blocking() only guarantees that the FIFO accepted the bytes.
    // Do not start looking for the PN532 ACK until the final stop bit left TX.
    uart_tx_wait_blocking(pn532_uart);
    return true;
}

static bool pn532_send_command(const uint8_t *cmd, uint8_t len) {
    return pn532_send_command_frame(cmd, len, false);
}

// Read and verify ACK frame from PN532 over UART
// Expected frame: 0x00 0x00 0xFF 0x00 0xFF 0x00
static bool pn532_check_ack(uint32_t timeout_ms) {
    static const uint8_t ack_pattern[6] = { 0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00 };
    uint8_t window[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    uint32_t start = to_ms_since_boot(get_absolute_time());

    while ((to_ms_since_boot(get_absolute_time()) - start) < timeout_ms) {
        uint8_t b;
        if (!pn532_uart_read_byte(&b, 10)) continue;

        memmove(&window[0], &window[1], 5);
        window[5] = b;

        if (memcmp(window, ack_pattern, 6) == 0) {
            return true;
        }
    }
    return false;
}

// Read and verify response frame from PN532 over UART
// Frame: 0x00 0x00 0xFF LEN LCS 0xD5 (CMD+1) [DATA...] DCS 0x00
static bool pn532_read_response(uint8_t cmd_code, uint8_t *resp, uint8_t *resp_len, uint32_t timeout_ms) {
    uint32_t start = to_ms_since_boot(get_absolute_time());
    uint8_t prev = 0xFF;

    while ((to_ms_since_boot(get_absolute_time()) - start) < timeout_ms) {
        uint8_t b;
        if (!pn532_uart_read_byte(&b, 10)) continue;

        // Check for 0x00 followed by 0xFF (Start of packet)
        if (prev == 0x00 && b == 0xFF) {
            uint8_t len, lcs;
            if (!pn532_uart_read_byte(&len, 100)) return false;
            if (!pn532_uart_read_byte(&lcs, 100)) return false;

            // Check length checksum: (len + lcs) & 0xFF must be 0
            if ((uint8_t)(len + lcs) == 0x00) {
                // If len == 0, this was an ACK frame; skip postamble and continue searching for response
                if (len == 0x00) {
                    uint8_t post;
                    (void)pn532_uart_read_byte(&post, 50);
                    prev = 0xFF;
                    continue;
                }

                if (len < 2 || len > 64) return false;

                // Read TFI (0xD5)
                uint8_t tfi;
                if (!pn532_uart_read_byte(&tfi, 100)) return false;
                if (tfi != PN532_PN532TOHOST) return false;

                // Read CMD+1
                uint8_t resp_cmd;
                if (!pn532_uart_read_byte(&resp_cmd, 100)) return false;
                if (cmd_code != 0xFF && resp_cmd != (cmd_code + 1)) return false;

                uint8_t data_len = len - 2;
                uint8_t sum = tfi + resp_cmd;

                uint8_t rx_buf[64];
                for (uint8_t i = 0; i < data_len; i++) {
                    if (!pn532_uart_read_byte(&rx_buf[i], 100)) return false;
                    sum += rx_buf[i];
                }

                // Read DCS and Postamble
                uint8_t dcs, postamble;
                if (!pn532_uart_read_byte(&dcs, 100)) return false;
                if (!pn532_uart_read_byte(&postamble, 100)) return false;

                if ((uint8_t)(sum + dcs) != 0x00) return false;

                if (resp && resp_len) {
                    if (data_len > *resp_len) data_len = *resp_len;
                    memcpy(resp, rx_buf, data_len);
                    *resp_len = data_len;
                }
                return true;
            }
        }
        prev = b;
    }
    return false;
}

// Send command, verify ACK, and read response
static bool pn532_transceive(const uint8_t *cmd, uint8_t cmd_len, uint8_t *resp, uint8_t *resp_len, uint32_t timeout_ms) {
    if (!pn532_send_command(cmd, cmd_len)) return false;
    if (!pn532_check_ack(150)) return false;
    return pn532_read_response(cmd[0], resp, resp_len, timeout_ms);
}

static bool pn532_transceive_wakeup(const uint8_t *cmd, uint8_t cmd_len, uint8_t *resp, uint8_t *resp_len, uint32_t timeout_ms) {
    if (!pn532_send_command_frame(cmd, cmd_len, true)) return false;
    if (!pn532_check_ack(250)) return false;
    return pn532_read_response(cmd[0], resp, resp_len, timeout_ms);
}

// Low-level InDataExchange command
pn532_status_t pn532_data_exchange(const uint8_t *tx, uint8_t tx_len, uint8_t *rx, uint8_t *rx_len, uint32_t timeout_ms) {
    uint8_t cmd[2 + 64];
    if (tx_len > 60) return PN532_ERR_UNSUPPORTED;

    cmd[0] = PN532_CMD_INDATAEXCHANGE;
    cmd[1] = 0x01; // Target 1
    memcpy(&cmd[2], tx, tx_len);

    uint8_t resp[64];
    uint8_t resp_len = sizeof(resp);

    if (!pn532_transceive(cmd, 2 + tx_len, resp, &resp_len, timeout_ms)) {
        return PN532_ERR_COMM;
    }

    if (resp_len < 1) return PN532_ERR_COMM;
    uint8_t status = resp[0];
    if ((status & 0x3F) != 0x00) {
        if (status == 0x14 || status == 0x27) return PN532_ERR_AUTH;
        return PN532_ERR_COMM;
    }

    uint8_t data_len = resp_len - 1;
    if (rx && rx_len) {
        if (data_len > *rx_len) data_len = *rx_len;
        memcpy(rx, &resp[1], data_len);
        *rx_len = data_len;
    }
    return PN532_OK;
}

pn532_status_t pn532_command(const uint8_t *cmd, uint8_t cmd_len,
                            uint8_t *response, uint8_t *response_len,
                            uint32_t timeout_ms) {
    if (!cmd || cmd_len == 0 || cmd_len > 64) return PN532_ERR_UNSUPPORTED;
    if (!pn532_transceive(cmd, cmd_len, response, response_len, timeout_ms)) {
        return PN532_ERR_COMM;
    }
    return PN532_OK;
}

pn532_status_t pn532_list_passive_target(uint8_t brty,
                                        const uint8_t *initiator_data,
                                        uint8_t initiator_data_len,
                                        uint8_t *response,
                                        uint8_t *response_len,
                                        uint32_t timeout_ms) {
    if (!response || !response_len || initiator_data_len > 60) return PN532_ERR_COMM;

    uint8_t cmd[3 + 60];
    cmd[0] = PN532_CMD_INLISTPASSIVETARGET;
    cmd[1] = 0x01;
    cmd[2] = brty;
    if (initiator_data_len) {
        if (!initiator_data) return PN532_ERR_COMM;
        memcpy(&cmd[3], initiator_data, initiator_data_len);
    }

    if (!pn532_transceive(cmd, (uint8_t)(3 + initiator_data_len), response,
                          response_len, timeout_ms)) {
        return PN532_ERR_COMM;
    }
    if (*response_len == 0 || response[0] == 0) return PN532_ERR_NO_TAG;
    return PN532_OK;
}

pn532_status_t pn532_communicate_thru(const uint8_t *tx, uint8_t tx_len,
                                     uint8_t *rx, uint8_t *rx_len,
                                     uint32_t timeout_ms) {
    if ((!tx && tx_len) || tx_len > 62) return PN532_ERR_UNSUPPORTED;

    uint8_t cmd[1 + 62];
    cmd[0] = PN532_CMD_INCOMMUNICATETHRU;
    if (tx_len) memcpy(&cmd[1], tx, tx_len);

    uint8_t resp[64];
    uint8_t resp_len = sizeof(resp);
    if (!pn532_transceive(cmd, (uint8_t)(1 + tx_len), resp, &resp_len, timeout_ms)) {
        return PN532_ERR_COMM;
    }
    if (resp_len < 1) return PN532_ERR_COMM;
    if ((resp[0] & 0x3F) != 0) return PN532_ERR_COMM;

    uint8_t data_len = (uint8_t)(resp_len - 1);
    if (rx && rx_len) {
        if (data_len > *rx_len) data_len = *rx_len;
        memcpy(rx, &resp[1], data_len);
        *rx_len = data_len;
    }
    return PN532_OK;
}

pn532_status_t pn532_target_init(const uint8_t *params, uint8_t params_len,
                                uint8_t *activation, uint8_t *activation_len,
                                uint32_t timeout_ms) {
    if (!params || params_len > 63) return PN532_ERR_UNSUPPORTED;
    // Keep automatic ATR_RES and RATS handling enabled and add the PN532's
    // ISO14443-4 PICC mode required by TgGetData/TgSetData.
    uint8_t set_parameters[] = { PN532_CMD_SETPARAMETERS, 0x34 };
    uint8_t parameter_response[2];
    uint8_t parameter_response_len = sizeof(parameter_response);
    if (!pn532_transceive(set_parameters, sizeof(set_parameters), parameter_response,
                          &parameter_response_len, 250)) return PN532_ERR_COMM;
    uint8_t cmd[64];
    cmd[0] = PN532_CMD_TGINITASTARGET;
    memcpy(&cmd[1], params, params_len);
    return pn532_command(cmd, (uint8_t)(params_len + 1), activation, activation_len, timeout_ms);
}

pn532_status_t pn532_target_get_data(uint8_t *data, uint8_t *data_len,
                                    uint32_t timeout_ms) {
    uint8_t cmd[] = { PN532_CMD_TGGETDATA };
    uint8_t response[64];
    uint8_t response_len = sizeof(response);
    pn532_status_t st = pn532_command(cmd, sizeof(cmd), response, &response_len, timeout_ms);
    if (st != PN532_OK) return st;
    if (response_len < 1 || (response[0] & 0x3F) != 0) return PN532_ERR_COMM;
    uint8_t payload_len = (uint8_t)(response_len - 1);
    if (data && data_len) {
        if (payload_len > *data_len) payload_len = *data_len;
        memcpy(data, &response[1], payload_len);
        *data_len = payload_len;
    }
    return PN532_OK;
}

pn532_status_t pn532_target_set_data(const uint8_t *data, uint8_t data_len,
                                    uint32_t timeout_ms) {
    if ((!data && data_len) || data_len > 62) return PN532_ERR_UNSUPPORTED;
    uint8_t cmd[63];
    cmd[0] = PN532_CMD_TGSETDATA;
    if (data_len) memcpy(&cmd[1], data, data_len);
    uint8_t response[4];
    uint8_t response_len = sizeof(response);
    pn532_status_t st = pn532_command(cmd, (uint8_t)(data_len + 1), response, &response_len, timeout_ms);
    if (st != PN532_OK) return st;
    if (response_len < 1 || (response[0] & 0x3F) != 0) return PN532_ERR_COMM;
    return PN532_OK;
}

bool pn532_init(uint8_t tx_bio, uint8_t rx_bio, uint32_t baudrate) {
    pn532_tx_bio_pin = tx_bio;
    pn532_rx_bio_pin = rx_bio;
    pn532_baudrate = baudrate ? baudrate : 115200;

    if (pn532_tx_bio_pin == BIO0 && pn532_rx_bio_pin == BIO1) {
        pn532_uart = uart1;
    } else {
        pn532_uart = uart0;
    }

    pn532_hardware_reinit();

    pn532_is_init_flag = true;
    pn532_is_ready_flag = false;

    return true;
}

void pn532_deinit(void) {
    if (pn532_uart) {
        if (pn532_is_ready_flag) {
            uint8_t pwr_cmd[] = { PN532_CMD_POWERDOWN, 0xC0 };
            pn532_send_command(pwr_cmd, sizeof(pwr_cmd));
            sleep_ms(5);
        }
        uart_deinit(pn532_uart);
        pn532_uart = NULL;
    }
    pn532_is_init_flag = false;
    pn532_is_ready_flag = false;
}

bool pn532_is_initialized(void) {
    return pn532_is_init_flag;
}

bool pn532_is_connected(void) {
    return pn532_is_ready_flag;
}

bool pn532_wakeup(void) {
    if (!pn532_uart) return false;
    // NXP's HSU wake-up alternative is one 0x55 byte followed by Twakeup.
    static const uint8_t wakeup_byte = 0x55;
    pn532_uart_flush();
    uart_write_blocking(pn532_uart, &wakeup_byte, 1);
    uart_tx_wait_blocking(pn532_uart);
    sleep_ms(10);
    return true;
}

static bool pn532_sam_config_internal(bool wakeup) {
    // SAMConfiguration: Mode 0x01 (Normal mode)
    uint8_t cmd[] = { PN532_CMD_SAMCONFIGURATION, 0x01, 0x14, 0x01 };
    uint8_t resp[8];
    uint8_t resp_len = sizeof(resp);
    bool ok = wakeup
                  ? pn532_transceive_wakeup(cmd, sizeof(cmd), resp, &resp_len, 250)
                  : pn532_transceive(cmd, sizeof(cmd), resp, &resp_len, 250);
    if (!ok) {
        // Fallback to 3-byte SAMConfig format (without IRQ parameter)
        uint8_t cmd2[] = { PN532_CMD_SAMCONFIGURATION, 0x01, 0x00 };
        resp_len = sizeof(resp);
        return pn532_transceive(cmd2, sizeof(cmd2), resp, &resp_len, 200);
    }
    return true;
}

bool pn532_sam_config(void) {
    return pn532_sam_config_internal(false);
}

bool pn532_get_firmware_version(uint8_t *ic, uint8_t *ver, uint8_t *rev, uint8_t *support) {
    uint8_t cmd[] = { PN532_CMD_GETFIRMWAREVERSION };
    uint8_t resp[8];
    uint8_t resp_len = sizeof(resp);

    if (!pn532_transceive(cmd, sizeof(cmd), resp, &resp_len, 250)) return false;
    if (resp_len < 4) return false;

    if (ic) *ic = resp[0];
    if (ver) *ver = resp[1];
    if (rev) *rev = resp[2];
    if (support) *support = resp[3];
    return true;
}

bool pn532_set_passive_activation_retries(uint8_t max_retries) {
    uint8_t cmd[] = { PN532_CMD_RFCONFIGURATION, 0x05, 0x01, 0x00, max_retries };
    uint8_t resp[4];
    uint8_t resp_len = sizeof(resp);
    return pn532_transceive(cmd, sizeof(cmd), resp, &resp_len, 200);
}

static int8_t pn532_baud_code(uint32_t baudrate) {
    switch (baudrate) {
        case 9600:   return 0;
        case 19200:  return 1;
        case 38400:  return 2;
        case 57600:  return 3;
        case 115200: return 4;
        case 230400: return 5;
        case 460800: return 6;
        case 921600: return 7;
        default:     return -1;
    }
}

bool pn532_set_serial_baudrate(uint32_t baudrate) {
    int8_t code = pn532_baud_code(baudrate);
    if (code < 0 || !pn532_uart) return false;
    if (baudrate == pn532_baudrate) return true;
    uint8_t cmd[] = { PN532_CMD_SETSERIALBAUDRATE, (uint8_t)code };
    uint8_t resp[4];
    uint8_t resp_len = sizeof(resp);
    // Sent at the CURRENT rate; the PN532 switches after its ACK+response.
    if (!pn532_transceive(cmd, sizeof(cmd), resp, &resp_len, 300)) return false;
    sleep_ms(10);
    pn532_baudrate = baudrate;
    pn532_hardware_reinit();
    sleep_ms(10);
    uint8_t ic = 0, ver = 0, rev = 0, sup = 0;
    return pn532_get_firmware_version(&ic, &ver, &rev, &sup);
}

bool pn532_connect(void) {
    if (!pn532_is_initialized()) return false;

    pn532_diag_bytes_rx = 0;
    pn532_diag_last_bytes_count = 0;
    pn532_diag_uart_errors = 0;
    pn532_diag_rx_line_high = true;

    uint32_t requested = pn532_baudrate ? pn532_baudrate : 115200;

    // 1. Re-initialize UART and buffer directions now that power is on
    pn532_hardware_reinit();

    // Helper: full wakeup + SAM + firmware check at the CURRENT uart rate.
    // 2. A PN532 on HSU starts in LowVbat. First try the Elechouse sequence,
    // where wake-up preamble and SAMConfiguration are one continuous transfer.
    if (!pn532_sam_config_internal(true)) {
        // Fall back to NXP's alternative: one 0x55 byte, Twakeup, then command.
        pn532_wakeup();
        sleep_ms(50);
        pn532_uart_flush();
        if (!pn532_sam_config()) {
            // A power-cycled PN532 always returns to 115200. If we asked for a
            // faster rate, retry the whole handshake at 115200 first.
            if (requested != 115200) {
                pn532_baudrate = 115200;
                pn532_hardware_reinit();
                pn532_diag_bytes_rx = 0;
                pn532_diag_last_bytes_count = 0;
                if (!pn532_sam_config_internal(true)) {
                    pn532_wakeup();
                    sleep_ms(50);
                    pn532_uart_flush();
                    if (!pn532_sam_config()) {
                        pn532_uart_capture_status();
                        pn532_is_ready_flag = false;
                        pn532_baudrate = requested;
                        return false;
                    }
                }
            } else {
                pn532_uart_capture_status();
                pn532_is_ready_flag = false;
                return false;
            }
        }
    }

    // 3. Now in Normal mode, verify framing with GetFirmwareVersion.
    uint8_t ic = 0, ver = 0, rev = 0, sup = 0;
    if (!pn532_get_firmware_version(&ic, &ver, &rev, &sup)) {
        pn532_uart_capture_status();
        pn532_is_ready_flag = false;
        pn532_baudrate = requested;
        return false;
    }

    // 4. Switch to the requested high rate if we handshaked at 115200.
    if (pn532_baudrate != requested) {
        if (!pn532_set_serial_baudrate(requested)) {
            pn532_uart_capture_status();
            pn532_is_ready_flag = false;
            return false;
        }
    }

    // 5. Configure passive retries.
    pn532_set_passive_activation_retries(0x02);
    pn532_is_ready_flag = true;
    return true;
}

static void pn532_identify_tag_type(pn532_tag_info_t *tag) {
    tag->is_mifare_classic = false;
    tag->is_ultralight_or_ntag = false;

    if (tag->sak & 0x04) {
        snprintf(tag->type_name, sizeof(tag->type_name), "Cascade / Incomplete UID");
        return;
    }

    switch (tag->sak) {
        case 0x08:
            snprintf(tag->type_name, sizeof(tag->type_name), "MIFARE Classic 1K");
            tag->total_pages = 64;
            tag->data_pages = 48;
            tag->is_mifare_classic = true;
            break;
        case 0x18:
            snprintf(tag->type_name, sizeof(tag->type_name), "MIFARE Classic 4K");
            tag->total_pages = 256;
            tag->data_pages = 216;
            tag->is_mifare_classic = true;
            break;
        case 0x09:
            snprintf(tag->type_name, sizeof(tag->type_name), "MIFARE Mini (320B)");
            tag->total_pages = 20;
            tag->data_pages = 15;
            tag->is_mifare_classic = true;
            break;
        case 0x00:
            tag->is_ultralight_or_ntag = true;
            tag->total_pages = 16;
            tag->data_pages = 12;
            snprintf(tag->type_name, sizeof(tag->type_name), "MIFARE Ultralight");

            uint8_t cc[4];
            if (pn532_ultralight_read_page(3, cc) == PN532_OK) {
                if (cc[2] == 0x12) {
                    snprintf(tag->type_name, sizeof(tag->type_name), "NTAG213 (144B)");
                    tag->total_pages = 45;
                    tag->data_pages = 36;
                } else if (cc[2] == 0x3E) {
                    snprintf(tag->type_name, sizeof(tag->type_name), "NTAG215 (504B)");
                    tag->total_pages = 135;
                    tag->data_pages = 126;
                } else if (cc[2] == 0x6D) {
                    snprintf(tag->type_name, sizeof(tag->type_name), "NTAG216 (888B)");
                    tag->total_pages = 231;
                    tag->data_pages = 222;
                }
            }
            break;
        case 0x10:
        case 0x11:
            snprintf(tag->type_name, sizeof(tag->type_name), "MIFARE Plus");
            tag->total_pages = 64;
            tag->data_pages = 48;
            break;
        case 0x20:
            snprintf(tag->type_name, sizeof(tag->type_name), "ISO14443-4 DESFire / Smartcard");
            break;
        default:
            snprintf(tag->type_name, sizeof(tag->type_name), "ISO14443A (SAK=0x%02X)", tag->sak);
            break;
    }
}

pn532_status_t pn532_detect_tag(pn532_tag_info_t *tag, uint32_t timeout_ms) {
    if (!tag) return PN532_ERR_COMM;
    memset(tag, 0, sizeof(*tag));

    uint8_t cmd[] = { PN532_CMD_INLISTPASSIVETARGET, 0x01, PN532_BAUDRATE_106KBPS_ISO14443A };
    uint8_t resp[32];
    uint8_t resp_len = sizeof(resp);

    if (!pn532_transceive(cmd, sizeof(cmd), resp, &resp_len, timeout_ms)) {
        return PN532_ERR_NO_TAG;
    }

    if (resp_len < 6 || resp[0] == 0) {
        return PN532_ERR_NO_TAG;
    }

    tag->atqa = ((uint16_t)resp[2] << 8) | resp[3];
    tag->sak = resp[4];
    tag->uid_len = resp[5];

    if (tag->uid_len > sizeof(tag->uid)) {
        tag->uid_len = sizeof(tag->uid);
    }
    memcpy(tag->uid, &resp[6], tag->uid_len);

    pn532_identify_tag_type(tag);
    return PN532_OK;
}

pn532_status_t pn532_mifare_auth(uint8_t block, uint8_t key_type, const uint8_t *key, const uint8_t *uid, uint8_t uid_len) {
    if (!key || !uid || uid_len < 4) return PN532_ERR_COMM;

    uint8_t auth_payload[12];
    auth_payload[0] = key_type;
    auth_payload[1] = block;
    memcpy(&auth_payload[2], key, 6);
    memcpy(&auth_payload[8], uid, 4);

    return pn532_data_exchange(auth_payload, sizeof(auth_payload), NULL, NULL, 200);
}

pn532_status_t pn532_mifare_auth_auto_type(uint8_t block, const uint8_t *uid, uint8_t uid_len,
                                           uint8_t *found_key, uint8_t *found_type) {
    // Mirror nfc-mfclassic: try every dictionary key as Key A first, then as Key B.
    for (uint8_t pass = 0; pass < 2; pass++) {
        uint8_t key_type = (pass == 0) ? MIFARE_CMD_AUTH_A : MIFARE_CMD_AUTH_B;
        for (uint32_t i = 0; i < common_keys_count; i++) {
            if (pn532_mifare_auth(block, key_type, common_mifare_keys[i], uid, uid_len) == PN532_OK) {
                if (found_key) memcpy(found_key, common_mifare_keys[i], 6);
                if (found_type) *found_type = key_type;
                return PN532_OK;
            }
        }
    }
    return PN532_ERR_AUTH;
}

pn532_status_t pn532_mifare_auth_auto(uint8_t block, const uint8_t *uid, uint8_t uid_len, uint8_t *found_key) {
    return pn532_mifare_auth_auto_type(block, uid, uid_len, found_key, NULL);
}

pn532_status_t pn532_mifare_auth_key(uint8_t block, const uint8_t *uid, uint8_t uid_len,
                                     const uint8_t *key, uint8_t key_type) {
    if (!key || (key_type != MIFARE_CMD_AUTH_A && key_type != MIFARE_CMD_AUTH_B)) return PN532_ERR_COMM;
    return pn532_mifare_auth(block, key_type, key, uid, uid_len);
}

pn532_status_t pn532_mifare_read_block(uint8_t block, uint8_t *data16) {
    if (!data16) return PN532_ERR_COMM;
    uint8_t cmd[] = { MIFARE_CMD_READ, block };
    uint8_t rx_len = 16;
    return pn532_data_exchange(cmd, sizeof(cmd), data16, &rx_len, 200);
}

pn532_status_t pn532_mifare_write_block(uint8_t block, const uint8_t *data16) {
    if (!data16) return PN532_ERR_COMM;
    uint8_t cmd[2 + 16];
    cmd[0] = MIFARE_CMD_WRITE;
    cmd[1] = block;
    memcpy(&cmd[2], data16, 16);
    return pn532_data_exchange(cmd, sizeof(cmd), NULL, NULL, 300);
}

pn532_status_t pn532_ultralight_read_page(uint8_t page, uint8_t *data4) {
    if (!data4) return PN532_ERR_COMM;
    uint8_t cmd[] = { MIFARE_CMD_READ, page };
    uint8_t rx[16];
    uint8_t rx_len = sizeof(rx);

    pn532_status_t st = pn532_data_exchange(cmd, sizeof(cmd), rx, &rx_len, 200);
    if (st == PN532_OK && rx_len >= 4) {
        memcpy(data4, rx, 4);
        return PN532_OK;
    }
    return st;
}

pn532_status_t pn532_ultralight_read16(uint8_t page, uint8_t *data16) {
    if (!data16) return PN532_ERR_COMM;
    uint8_t cmd[] = { MIFARE_CMD_READ, page };
    uint8_t rx_len = 16;
    // A single MIFARE READ returns 4 pages (16 bytes) starting at page.
    return pn532_data_exchange(cmd, sizeof(cmd), data16, &rx_len, 200);
}

pn532_status_t pn532_ntag_pwd_auth(const uint8_t *pwd, uint8_t *pack_out) {
    if (!pwd) return PN532_ERR_COMM;
    uint8_t cmd[5] = { NTAG_CMD_PWD_AUTH, pwd[0], pwd[1], pwd[2], pwd[3] };
    uint8_t rx[4];
    uint8_t rx_len = sizeof(rx);
    pn532_status_t st = pn532_data_exchange(cmd, sizeof(cmd), rx, &rx_len, 300);
    if (st != PN532_OK || rx_len < 2) return (st == PN532_OK) ? PN532_ERR_AUTH : st;
    if (pack_out) memcpy(pack_out, rx, 2);
    return PN532_OK;
}

pn532_status_t pn532_ntag_get_version(uint8_t *version8) {
    if (!version8) return PN532_ERR_COMM;
    uint8_t cmd[] = { NTAG_CMD_GET_VERSION };
    uint8_t rx_len = 8;
    return pn532_data_exchange(cmd, sizeof(cmd), version8, &rx_len, 300);
}

pn532_status_t pn532_ultralight_write_page(uint8_t page, const uint8_t *data4) {
    if (!data4) return PN532_ERR_COMM;
    uint8_t cmd[2 + 4];
    cmd[0] = MIFARE_ULTRALIGHT_CMD_WRITE;
    cmd[1] = page;
    memcpy(&cmd[2], data4, 4);
    return pn532_data_exchange(cmd, sizeof(cmd), NULL, NULL, 300);
}

pn532_status_t pn532_write_magic_block0(const uint8_t *uid, uint8_t uid_len, uint8_t sak, uint16_t atqa) {
    if (!uid || uid_len != 4) return PN532_ERR_UNSUPPORTED;

    uint8_t block0[16];
    uint8_t bcc = 0;
    for (int i = 0; i < 4; i++) {
        block0[i] = uid[i];
        bcc ^= uid[i];
    }
    block0[4] = bcc;
    block0[5] = sak ? sak : 0x08;
    block0[6] = (uint8_t)((atqa >> 8) & 0xFF);
    block0[7] = (uint8_t)(atqa & 0xFF);

    const uint8_t def_manuf[8] = { 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69 };
    memcpy(&block0[8], def_manuf, 8);

    static const uint8_t def_key[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    (void)pn532_mifare_auth(0, MIFARE_CMD_AUTH_A, def_key, uid, 4);

    return pn532_mifare_write_block(0, block0);
}

pn532_status_t pn532_erase_tag_ex(const pn532_tag_info_t *tag, const uint8_t *custom_key,
                                  bool has_custom, uint8_t custom_type) {
    if (!tag) return PN532_ERR_COMM;

    if (tag->is_mifare_classic) {
        uint8_t zero_block[16];
        memset(zero_block, 0x00, sizeof(zero_block));
        uint8_t sectors = (tag->sak == 0x18) ? 40 : ((tag->sak == 0x09) ? 5 : 16);

        for (uint8_t s = 0; s < sectors; s++) {
            uint8_t first_block = (s < 32) ? (s * 4) : (128 + (s - 32) * 16);
            uint8_t num_blocks = (s < 32) ? 4 : 16;

            uint8_t dummy_key[6];
            uint8_t dummy_type = MIFARE_CMD_AUTH_A;
            bool authed = false;
            if (has_custom && custom_key) {
                authed = (pn532_mifare_auth(first_block, custom_type, custom_key, tag->uid, tag->uid_len) == PN532_OK);
                if (!authed) {
                    // Fall back to dictionary A then B.
                    authed = (pn532_mifare_auth_auto_type(first_block, tag->uid, tag->uid_len, dummy_key,
                                                          &dummy_type) == PN532_OK);
                }
            } else {
                authed = (pn532_mifare_auth_auto_type(first_block, tag->uid, tag->uid_len, NULL, NULL) == PN532_OK);
            }
            if (!authed) continue;

            for (uint8_t b = 0; b < num_blocks - 1; b++) {
                uint8_t blk = first_block + b;
                if (blk == 0) continue;
                pn532_mifare_write_block(blk, zero_block);
            }
        }
        return PN532_OK;
    } else if (tag->is_ultralight_or_ntag) {
        uint8_t zero_page[4] = { 0x00, 0x00, 0x00, 0x00 };
        // Only erase user pages (4 .. 4+data_pages-1). Lock/config/password
        // pages at the end are preserved so students cannot brick NTAGs.
        uint16_t user_end = (uint16_t)(4 + tag->data_pages);
        uint16_t max_pages = tag->total_pages ? tag->total_pages : 16;
        if (user_end > max_pages) user_end = max_pages;
        for (uint16_t p = 4; p < user_end; p++) {
            pn532_ultralight_write_page((uint8_t)p, zero_page);
        }
        return PN532_OK;
    }

    return PN532_ERR_UNSUPPORTED;
}

pn532_status_t pn532_erase_tag(const pn532_tag_info_t *tag) {
    return pn532_erase_tag_ex(tag, NULL, false, MIFARE_CMD_AUTH_A);
}

pn532_status_t pn532_read_ndef(char *buf, size_t max_len, char *type_out, size_t type_out_len) {
    if (!buf || max_len < 8) return PN532_ERR_COMM;

    // One MIFARE READ returns 16 bytes; 16 reads cover 256 bytes starting at page 4.
    // This fits Ultralight (64B), NTAG213 (180B window) and typical NDEF demos.
    uint8_t raw[256];
    memset(raw, 0, sizeof(raw));

    for (uint8_t i = 0; i < 16; i++) {
        if (pn532_ultralight_read16((uint8_t)(4 + i * 4), &raw[i * 16]) != PN532_OK) {
            if (i == 0) return PN532_ERR_COMM;
            break;
        }
    }

    uint16_t offset = 0;
    while (offset < sizeof(raw) && raw[offset] != 0x03 && raw[offset] != 0xFE) {
        offset++;
    }

    if (offset >= sizeof(raw) || raw[offset] != 0x03) {
        return PN532_ERR_NO_TAG;
    }

    offset++; // Skip 0x03 TLV tag
    if (offset >= sizeof(raw)) return PN532_ERR_COMM;
    uint16_t ndef_len;
    if (raw[offset] == 0xFF) {
        // Long-form TLV length: 0xFF followed by 2-byte big-endian length.
        if (offset + 2 >= sizeof(raw)) return PN532_ERR_COMM;
        ndef_len = ((uint16_t)raw[offset + 1] << 8) | raw[offset + 2];
        offset += 3;
    } else {
        ndef_len = raw[offset++];
    }
    if (ndef_len == 0 || (uint32_t)offset + ndef_len > sizeof(raw)) {
        return PN532_ERR_COMM;
    }

    uint8_t header = raw[offset++];
    (void)header;
    uint8_t type_len = raw[offset++];
    (void)type_len;
    uint8_t payload_len = raw[offset++];
    uint8_t rec_type = raw[offset++];

    if (rec_type == 0x55) { // URI ('U')
        if (type_out) snprintf(type_out, type_out_len, "URI");
        uint8_t prefix = raw[offset++];
        const char *pfx_str = "";
        switch (prefix) {
            case 0x01: pfx_str = "http://www."; break;
            case 0x02: pfx_str = "https://www."; break;
            case 0x03: pfx_str = "http://"; break;
            case 0x04: pfx_str = "https://"; break;
            case 0x06: pfx_str = "mailto:"; break;
            default:   pfx_str = ""; break;
        }
        uint8_t str_len = (payload_len > 1) ? (payload_len - 1) : 0;
        char body[64];
        if (str_len >= sizeof(body)) str_len = sizeof(body) - 1;
        memcpy(body, &raw[offset], str_len);
        body[str_len] = '\0';
        snprintf(buf, max_len, "%s%s", pfx_str, body);
        return PN532_OK;
    } else if (rec_type == 0x54) { // Text ('T')
        if (type_out) snprintf(type_out, type_out_len, "Text");
        uint8_t status = raw[offset++];
        uint8_t lang_len = status & 0x1F;
        offset += lang_len;
        uint8_t txt_len = (payload_len > (1 + lang_len)) ? (payload_len - (1 + lang_len)) : 0;
        if (txt_len >= max_len) txt_len = max_len - 1;
        memcpy(buf, &raw[offset], txt_len);
        buf[txt_len] = '\0';
        return PN532_OK;
    }

    return PN532_ERR_UNSUPPORTED;
}

pn532_status_t pn532_write_ndef_uri(const char *uri) {
    if (!uri || strlen(uri) == 0) return PN532_ERR_COMM;

    uint8_t prefix = 0x00;
    const char *body = uri;
    if (strncmp(uri, "https://www.", 12) == 0) { prefix = 0x02; body = uri + 12; }
    else if (strncmp(uri, "http://www.", 11) == 0) { prefix = 0x01; body = uri + 11; }
    else if (strncmp(uri, "https://", 8) == 0) { prefix = 0x04; body = uri + 8; }
    else if (strncmp(uri, "http://", 7) == 0) { prefix = 0x03; body = uri + 7; }

    uint8_t body_len = (uint8_t)strlen(body);
    uint8_t payload_len = 1 + body_len;
    uint8_t ndef_len = 4 + payload_len;

    uint8_t buffer[64];
    memset(buffer, 0, sizeof(buffer));

    uint8_t idx = 0;
    buffer[idx++] = 0x03; // NDEF TLV
    buffer[idx++] = ndef_len;
    buffer[idx++] = 0xD1;
    buffer[idx++] = 0x01;
    buffer[idx++] = payload_len;
    buffer[idx++] = 0x55; // 'U'
    buffer[idx++] = prefix;
    memcpy(&buffer[idx], body, body_len);
    idx += body_len;
    buffer[idx++] = 0xFE;

    uint8_t total_pages = (idx + 3) / 4;
    for (uint8_t p = 0; p < total_pages; p++) {
        pn532_status_t st = pn532_ultralight_write_page(4 + p, &buffer[p * 4]);
        if (st != PN532_OK) return st;
    }
    return PN532_OK;
}

pn532_status_t pn532_write_ndef_text(const char *text) {
    if (!text || strlen(text) == 0) return PN532_ERR_COMM;

    uint8_t text_len = (uint8_t)strlen(text);
    uint8_t payload_len = 1 + 2 + text_len;
    uint8_t ndef_len = 4 + payload_len;

    uint8_t buffer[64];
    memset(buffer, 0, sizeof(buffer));

    uint8_t idx = 0;
    buffer[idx++] = 0x03;
    buffer[idx++] = ndef_len;
    buffer[idx++] = 0xD1;
    buffer[idx++] = 0x01;
    buffer[idx++] = payload_len;
    buffer[idx++] = 0x54; // 'T'
    buffer[idx++] = 0x02;
    buffer[idx++] = 'e';
    buffer[idx++] = 'n';
    memcpy(&buffer[idx], text, text_len);
    idx += text_len;
    buffer[idx++] = 0xFE;

    uint8_t total_pages = (idx + 3) / 4;
    for (uint8_t p = 0; p < total_pages; p++) {
        pn532_status_t st = pn532_ultralight_write_page(4 + p, &buffer[p * 4]);
        if (st != PN532_OK) return st;
    }
    return PN532_OK;
}

const char *pn532_status_str(pn532_status_t status) {
    switch (status) {
        case PN532_OK:               return "Success";
        case PN532_ERR_TIMEOUT:      return "Timeout";
        case PN532_ERR_NO_TAG:       return "No tag detected";
        case PN532_ERR_AUTH:         return "Authentication failed";
        case PN532_ERR_COMM:         return "Communication error";
        case PN532_ERR_CHECKSUM:     return "Checksum mismatch";
        case PN532_ERR_WRITE:        return "Write failed";
        case PN532_ERR_UNSUPPORTED:  return "Operation unsupported for tag type";
        default:                     return "Unknown error";
    }
}
