/**
 * @file pn532.h
 * @brief PN532 RFID/NFC Controller Driver for Bus Pirate
 * @details Driver for NXP PN532 NFC controller over I2C.
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

#define PN532_I2C_ADDR_7BIT     0x24
#define PN532_I2C_ADDR_WRITE    0x48
#define PN532_I2C_ADDR_READ     0x49

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
#define PN532_CMD_INJUMPFORDEP          0x56
#define PN532_CMD_INJUMPFORPSL          0x46
#define PN532_CMD_INLISTPASSIVETARGET   0x4A
#define PN532_CMD_INATR                 0x50
#define PN532_CMD_INPSL                 0x4E
#define PN532_CMD_INDATAEXCHANGE        0x40
#define PN532_CMD_INCOMMUNICATETHRU     0x42
#define PN532_CMD_INDESELECT            0x44
#define PN532_CMD_INRELEASE             0x52
#define PN532_CMD_INSELECT              0x54

// MIFARE Commands
#define MIFARE_CMD_AUTH_A               0x60
#define MIFARE_CMD_AUTH_B               0x61
#define MIFARE_CMD_READ                 0x30
#define MIFARE_CMD_WRITE                0xA0
#define MIFARE_CMD_TRANSFER             0xB0
#define MIFARE_CMD_DECREMENT            0xC0
#define MIFARE_CMD_INCREMENT            0xC1
#define MIFARE_CMD_RESTORE              0xC2
#define MIFARE_ULTRALIGHT_CMD_WRITE     0xA2

// Card Baud Rates
#define PN532_BAUDRATE_106KBPS_ISO14443A 0x00
#define PN532_BAUDRATE_212KBPS_FELICA   0x01
#define PN532_BAUDRATE_424KBPS_FELICA   0x02
#define PN532_BAUDRATE_106KBPS_ISO14443B 0x03

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
 * @brief Initialize PN532 hardware over I2C
 * @param sda_bio BIO pin index for SDA (e.g. BIO0)
 * @param scl_bio BIO pin index for SCL (e.g. BIO1)
 * @param speed_hz I2C clock rate (e.g. 100000 or 400000)
 * @return true if PN532 was detected and initialized
 */
bool pn532_init(uint8_t sda_bio, uint8_t scl_bio, uint32_t speed_hz);

/**
 * @brief Deinitialize PN532 and cleanup I2C PIO
 */
void pn532_deinit(void);

/**
 * @brief Check if PN532 is currently initialized
 */
bool pn532_is_initialized(void);

/**
 * @brief Wake up PN532 from low-power state
 */
bool pn532_wakeup(void);

/**
 * @brief Query firmware version and support capabilities
 * @param ic Chip identifier (0x32 for PN532)
 * @param ver Firmware major version
 * @param rev Firmware minor revision
 * @param support Bitmask of supported protocols
 */
bool pn532_get_firmware_version(uint8_t *ic, uint8_t *ver, uint8_t *rev, uint8_t *support);

/**
 * @brief Configure Secure Access Module (SAM) to normal mode
 */
bool pn532_sam_config(void);

/**
 * @brief Set passive activation retry limit
 * @param max_retries 0xFF for infinite, 0x00..0xFE for retry count
 */
bool pn532_set_passive_activation_retries(uint8_t max_retries);

/**
 * @brief Detect passive ISO14443A tag in RF field
 * @param tag Output structure populated with UID, SAK, ATQA, and type
 * @param timeout_ms Polling timeout in milliseconds
 */
pn532_status_t pn532_detect_tag(pn532_tag_info_t *tag, uint32_t timeout_ms);

/**
 * @brief Authenticate MIFARE Classic block using key
 * @param block Target block index (0..63 for 1K, 0..255 for 4K)
 * @param key_type MIFARE_CMD_AUTH_A or MIFARE_CMD_AUTH_B
 * @param key 6-byte authentication key
 * @param uid 4-byte card UID
 * @param uid_len Card UID length
 */
pn532_status_t pn532_mifare_auth(uint8_t block, uint8_t key_type, const uint8_t *key, const uint8_t *uid, uint8_t uid_len);

/**
 * @brief Authenticate block using built-in dictionary of common keys
 * @param block Target block index
 * @param uid 4-byte card UID
 * @param uid_len Card UID length
 * @param found_key Optional output pointer to store the matching 6-byte key
 */
pn532_status_t pn532_mifare_auth_auto(uint8_t block, const uint8_t *uid, uint8_t uid_len, uint8_t *found_key);

/**
 * @brief Read 16-byte block from MIFARE Classic card
 * @param block Block index (0..63 for 1K, 0..255 for 4K)
 * @param data16 Output buffer of at least 16 bytes
 */
pn532_status_t pn532_mifare_read_block(uint8_t block, uint8_t *data16);

/**
 * @brief Write 16-byte block to MIFARE Classic card
 * @param block Block index
 * @param data16 16 bytes of data to write
 */
pn532_status_t pn532_mifare_write_block(uint8_t block, const uint8_t *data16);

/**
 * @brief Read 4-byte page from MIFARE Ultralight or NTAG
 * @param page Page index (0..total_pages-1)
 * @param data4 Output buffer of at least 4 bytes
 */
pn532_status_t pn532_ultralight_read_page(uint8_t page, uint8_t *data4);

/**
 * @brief Write 4-byte page to MIFARE Ultralight or NTAG
 * @param page Page index (4..total_pages-1 for user memory)
 * @param data4 4 bytes of data to write
 */
pn532_status_t pn532_ultralight_write_page(uint8_t page, const uint8_t *data4);

/**
 * @brief Write UID to Block 0 of a Magic Card (UID changeable tag)
 * @param uid Target UID (4 bytes)
 * @param uid_len Length (must be 4)
 * @param sak SAK byte
 * @param atqa ATQA word
 */
pn532_status_t pn532_write_magic_block0(const uint8_t *uid, uint8_t uid_len, uint8_t sak, uint16_t atqa);

/**
 * @brief Erase data blocks/pages on card (zero out user memory)
 * @param tag Tag info structure
 */
pn532_status_t pn532_erase_tag(const pn532_tag_info_t *tag);

/**
 * @brief Read NDEF record from tag
 * @param buf Output buffer for payload string (URL or text)
 * @param max_len Maximum length of output buffer
 * @param type_out Output buffer for record type (e.g. "URI" or "Text")
 * @param type_out_len Maximum length of type_out
 */
pn532_status_t pn532_read_ndef(char *buf, size_t max_len, char *type_out, size_t type_out_len);

/**
 * @brief Write NDEF URI record (e.g. https://buspirate.com) to Ultralight/NTAG
 * @param uri URI string
 */
pn532_status_t pn532_write_ndef_uri(const char *uri);

/**
 * @brief Write NDEF Text record to Ultralight/NTAG
 * @param text Text string
 */
pn532_status_t pn532_write_ndef_text(const char *text);

/**
 * @brief Return user-readable string for status code
 */
const char *pn532_status_str(pn532_status_t status);

#endif // BP_PN532_H
