/**
 * @file pn532.h
 * @brief PN532 RFID/NFC Controller Driver for Bus Pirate (High-Speed UART)
 * @details Driver for NXP PN532 NFC controller over High-Speed UART (HSU).
 *          Supports ISO14443A (MIFARE Classic 1K/4K, Ultralight, NTAG213/215/216),
 *          UID reading, memory dump, block/page write, magic card cloning, and NDEF.
 * @author Bus Pirate Project
 * @date 2026
 */

#ifndef BP_PN532_H
#define BP_PN532_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define PN532_PREAMBLE          0x00
#define PN532_STARTCODE1        0x00
#define PN532_STARTCODE2        0xFF
#define PN532_POSTAMBLE         0x00

#define PN532_HOSTTOPN532       0xD4
#define PN532_PN532TOHOST       0xD5

// PN532 Commands
#define PN532_CMD_DIAGNOSE              0x00
#define PN532_CMD_GETFIRMWAREVERSION    0x02
#define PN532_CMD_GETGENERALSTATUS      0x04
#define PN532_CMD_READREGISTER          0x06
#define PN532_CMD_WRITEREGISTER         0x08
#define PN532_CMD_READGPIO              0x0C
#define PN532_CMD_WRITEGPIO             0x0E
#define PN532_CMD_SETSERIALBAUDRATE     0x10
#define PN532_CMD_SETPARAMETERS         0x12
#define PN532_CMD_SAMCONFIGURATION      0x14
#define PN532_CMD_POWERDOWN             0x16
#define PN532_CMD_RFCONFIGURATION       0x32
#define PN532_CMD_INLISTPASSIVETARGET   0x4A
#define PN532_CMD_INDATAEXCHANGE        0x40
#define PN532_CMD_INCOMMUNICATETHRU     0x42
#define PN532_CMD_INDESELECT            0x44
#define PN532_CMD_INRELEASE             0x52
#define PN532_CMD_INAUTOPOLL             0x60
#define PN532_CMD_TGINITASTARGET         0x8C
#define PN532_CMD_TGGETDATA              0x86
#define PN532_CMD_TGSETDATA              0x8E
#define PN532_CMD_TGGETINITIATORCOMMAND  0x88
#define PN532_CMD_TGRESPONSETOINITIATOR  0x90

// MIFARE Commands
#define MIFARE_CMD_AUTH_A               0x60
#define MIFARE_CMD_AUTH_B               0x61
#define MIFARE_CMD_READ                 0x30
#define MIFARE_CMD_WRITE                0xA0
#define MIFARE_ULTRALIGHT_CMD_WRITE     0xA2

// Card Baud Rates
#define PN532_BAUDRATE_106KBPS_ISO14443A 0x00
#define PN532_BAUDRATE_212KBPS_FELICA   0x01
#define PN532_BAUDRATE_424KBPS_FELICA   0x02
#define PN532_BAUDRATE_106KBPS_ISO14443B 0x03
#define PN532_BAUDRATE_106KBPS_JEWEL     0x04

typedef struct {
    uint8_t uid[10];
    uint8_t uid_len;
    uint16_t atqa;
    uint8_t sak;
    char type_name[36];
    uint16_t total_pages;
    uint16_t data_pages;
    bool is_mifare_classic;
    bool is_ultralight_or_ntag;
} pn532_tag_info_t;

typedef enum {
    PN532_OK = 0,
    PN532_ERR_TIMEOUT,
    PN532_ERR_NO_TAG,
    PN532_ERR_AUTH,
    PN532_ERR_COMM,
    PN532_ERR_CHECKSUM,
    PN532_ERR_WRITE,
    PN532_ERR_UNSUPPORTED
} pn532_status_t;

/**
 * @brief Initialize PN532 hardware over High-Speed UART (HSU)
 * @param tx_bio BIO pin index for Bus Pirate TX (connects to PN532 RX)
 * @param rx_bio BIO pin index for Bus Pirate RX (connects to PN532 TX)
 * @param baudrate UART baud rate (default: 115200)
 * @return true on success
 */
bool pn532_init(uint8_t tx_bio, uint8_t rx_bio, uint32_t baudrate);

/**
 * @brief Deinitialize PN532 and release UART peripheral
 */
void pn532_deinit(void);

/**
 * @brief Check if PN532 UART is initialized
 */
bool pn532_is_initialized(void);

/**
 * @brief Check if PN532 is responding and in ready state
 */
bool pn532_is_connected(void);

/**
 * @brief Wake up PN532 and verify communication
 */
bool pn532_connect(void);

/**
 * @brief Send HSU wakeup packet to PN532
 */
bool pn532_wakeup(void);

/**
 * @brief Query firmware version and support capabilities
 */
bool pn532_get_firmware_version(uint8_t *ic, uint8_t *ver, uint8_t *rev, uint8_t *support);

/**
 * @brief Configure Secure Access Module (SAM) to normal mode
 */
bool pn532_sam_config(void);

/**
 * @brief Set passive activation retry limit
 */
bool pn532_set_passive_activation_retries(uint8_t max_retries);

/**
 * @brief Execute a raw PN532 host command
 * @details The command byte is the first byte in cmd. PN532 framing, ACK, and
 *          response validation are handled by the driver.
 */
pn532_status_t pn532_command(const uint8_t *cmd, uint8_t cmd_len,
                            uint8_t *response, uint8_t *response_len,
                            uint32_t timeout_ms);

/**
 * @brief Select one passive target using a PN532 BrTy value
 * @details Returns the unmodified InListPassiveTarget response so callers can
 *          decode Type A, Type B, FeliCa, or Jewel target information.
 */
pn532_status_t pn532_list_passive_target(uint8_t brty,
                                        const uint8_t *initiator_data,
                                        uint8_t initiator_data_len,
                                        uint8_t *response,
                                        uint8_t *response_len,
                                        uint32_t timeout_ms);

/** @brief Exchange bytes with the currently selected target (Tg=1). */
pn532_status_t pn532_data_exchange(const uint8_t *tx, uint8_t tx_len,
                                   uint8_t *rx, uint8_t *rx_len,
                                   uint32_t timeout_ms);

/** @brief Exchange a raw RF frame using InCommunicateThru. */
pn532_status_t pn532_communicate_thru(const uint8_t *tx, uint8_t tx_len,
                                     uint8_t *rx, uint8_t *rx_len,
                                     uint32_t timeout_ms);

/** @brief Enter PN532 target mode and wait for an initiator. */
pn532_status_t pn532_target_init(const uint8_t *params, uint8_t params_len,
                                uint8_t *activation, uint8_t *activation_len,
                                uint32_t timeout_ms);

/** @brief Receive an ISO14443-4/DEP payload while in target mode. */
pn532_status_t pn532_target_get_data(uint8_t *data, uint8_t *data_len,
                                    uint32_t timeout_ms);

/** @brief Send an ISO14443-4/DEP payload while in target mode. */
pn532_status_t pn532_target_set_data(const uint8_t *data, uint8_t data_len,
                                    uint32_t timeout_ms);

/**
 * @brief Detect passive ISO14443A tag in RF field
 */
pn532_status_t pn532_detect_tag(pn532_tag_info_t *tag, uint32_t timeout_ms);

/**
 * @brief Authenticate MIFARE Classic block using key
 */
pn532_status_t pn532_mifare_auth(uint8_t block, uint8_t key_type, const uint8_t *key, const uint8_t *uid, uint8_t uid_len);

/**
 * @brief Authenticate block using built-in dictionary of common keys
 */
pn532_status_t pn532_mifare_auth_auto(uint8_t block, const uint8_t *uid, uint8_t uid_len, uint8_t *found_key);

/**
 * @brief Read 16-byte block from MIFARE Classic card
 */
pn532_status_t pn532_mifare_read_block(uint8_t block, uint8_t *data16);

/**
 * @brief Write 16-byte block to MIFARE Classic card
 */
pn532_status_t pn532_mifare_write_block(uint8_t block, const uint8_t *data16);

/**
 * @brief Read 4-byte page from MIFARE Ultralight or NTAG
 */
pn532_status_t pn532_ultralight_read_page(uint8_t page, uint8_t *data4);

/**
 * @brief Write 4-byte page to MIFARE Ultralight or NTAG
 */
pn532_status_t pn532_ultralight_write_page(uint8_t page, const uint8_t *data4);

/**
 * @brief Write UID to Block 0 of a Magic Card (UID changeable tag)
 */
pn532_status_t pn532_write_magic_block0(const uint8_t *uid, uint8_t uid_len, uint8_t sak, uint16_t atqa);

/**
 * @brief Erase data blocks/pages on card (zero out user memory)
 */
pn532_status_t pn532_erase_tag(const pn532_tag_info_t *tag);

/**
 * @brief Read NDEF record from tag
 */
pn532_status_t pn532_read_ndef(char *buf, size_t max_len, char *type_out, size_t type_out_len);

/**
 * @brief Write NDEF URI record (e.g. https://buspirate.com) to Ultralight/NTAG
 */
pn532_status_t pn532_write_ndef_uri(const char *uri);

/**
 * @brief Write NDEF Text record to Ultralight/NTAG
 */
pn532_status_t pn532_write_ndef_text(const char *text);

/**
 * @brief Return user-readable string for status code
 */
const char *pn532_status_str(pn532_status_t status);

/**
 * @brief Get count of bytes received over UART during last connection attempt
 */
uint32_t pn532_get_diag_rx_count(void);

/**
 * @brief Get accumulated PL011 receive error flags from the last connection attempt
 * @return Bit 0 framing, bit 1 parity, bit 2 break, bit 3 overrun
 */
uint32_t pn532_get_diag_uart_errors(void);

/**
 * @brief Get the last sampled raw level of the PN532 TX / Bus Pirate RX line
 * @return true when the line is at the UART idle (high) level
 */
bool pn532_get_diag_rx_line_high(void);

/**
 * @brief Get last received bytes for debugging
 */
void pn532_get_diag_last_bytes(uint8_t *buf, uint8_t *len);

#endif // BP_PN532_H
