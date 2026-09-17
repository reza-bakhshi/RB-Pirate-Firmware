/**
 * @file pn532.c
 * @brief PN532 RFID/NFC Controller Driver for Bus Pirate
 * @details Implements communication with NXP PN532 over I2C.
 * @author Bus Pirate Project
 * @date 2026
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "pirate.h"
#include "pirate/bio.h"
#include "pirate/hwi2c_pio.h"
#include "pirate/pn532.h"

static bool pn532_is_ready_flag = false;
static uint8_t pn532_sda_bio_pin = 0;
static uint8_t pn532_scl_bio_pin = 1;

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

// Wait for PN532 to signal ready status bit (0x01) on I2C read
static bool pn532_wait_ready(uint32_t timeout_ms) {
    uint32_t start = to_ms_since_boot(get_absolute_time());
    do {
        uint8_t rdy = 0;
        hwi2c_status_t st = pio_i2c_read_array_timeout(PN532_I2C_ADDR_READ, &rdy, 1, 0x1fff);
        if (st == HWI2C_OK && (rdy & 0x01)) {
            return true;
        }
        sleep_ms(1);
    } while ((to_ms_since_boot(get_absolute_time()) - start) < timeout_ms);
    return false;
}

// Send standard PN532 frame over I2C
static bool pn532_send_command(const uint8_t *cmd, uint8_t len) {
    if (len > 64) return false;
    uint8_t pkt[8 + 64];
    uint8_t packet_len = len + 1; // includes TFI (0xD4)

    pkt[0] = PN532_PREAMBLE;
    pkt[1] = PN532_STARTCODE1;
    pkt[2] = PN532_STARTCODE2;
    pkt[3] = packet_len;
    pkt[4] = (uint8_t)(~packet_len + 1);
    pkt[5] = PN532_HOSTTOPN532;

    uint8_t sum = PN532_HOSTTOPN532;
    for (uint8_t i = 0; i < len; i++) {
        pkt[6 + i] = cmd[i];
        sum += cmd[i];
    }
    pkt[6 + len] = (uint8_t)(~sum + 1);
    pkt[7 + len] = PN532_POSTAMBLE;

    hwi2c_status_t st = pio_i2c_write_array_timeout(PN532_I2C_ADDR_WRITE, pkt, 8 + len, 0xfffff);
    return (st == HWI2C_OK);
}

// Read and verify ACK frame from PN532
static bool pn532_check_ack(uint32_t timeout_ms) {
    if (!pn532_wait_ready(timeout_ms)) return false;

    uint8_t buf[7]; // 1 status byte + 6 ACK bytes
    hwi2c_status_t st = pio_i2c_read_array_timeout(PN532_I2C_ADDR_READ, buf, sizeof(buf), 0xfffff);
    if (st != HWI2C_OK) return false;

    static const uint8_t expected_ack[] = { 0x01, 0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00 };
    return (memcmp(buf, expected_ack, sizeof(expected_ack)) == 0);
}

// Read and verify response frame from PN532
static bool pn532_read_response(uint8_t cmd_code, uint8_t *resp, uint8_t *resp_len, uint32_t timeout_ms) {
    if (!pn532_wait_ready(timeout_ms)) return false;

    uint8_t buf[64];
    memset(buf, 0, sizeof(buf));
    hwi2c_status_t st = pio_i2c_read_array_timeout(PN532_I2C_ADDR_READ, buf, sizeof(buf), 0xfffff);
    if (st != HWI2C_OK) return false;

    if (buf[0] != 0x01) return false; // Not ready
    if (buf[1] != 0x00 || buf[2] != 0x00 || buf[3] != 0xFF) return false; // Preamble error

    uint8_t len = buf[4];
    uint8_t lcs = buf[5];
    if ((uint8_t)(len + lcs) != 0x00) return false;

    if (buf[6] != PN532_PN532TOHOST) return false;
    if (buf[7] != (cmd_code + 1)) return false;

    uint8_t data_len = len - 2; // Subtract TFI and CMD+1
    if (resp && resp_len) {
        if (data_len > *resp_len) data_len = *resp_len;
        memcpy(resp, &buf[8], data_len);
        *resp_len = data_len;
    }
    return true;
}

// Send command, verify ACK, and read response
static bool pn532_transceive(const uint8_t *cmd, uint8_t cmd_len, uint8_t *resp, uint8_t *resp_len, uint32_t timeout_ms) {
    if (!pn532_send_command(cmd, cmd_len)) return false;
    if (!pn532_check_ack(100)) return false;
    return pn532_read_response(cmd[0], resp, resp_len, timeout_ms);
}

// Low-level InDataExchange command
static pn532_status_t pn532_in_data_exchange(const uint8_t *tx, uint8_t tx_len, uint8_t *rx, uint8_t *rx_len, uint32_t timeout_ms) {
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

bool pn532_init(uint8_t sda_bio, uint8_t scl_bio, uint32_t speed_hz) {
    pn532_sda_bio_pin = sda_bio;
    pn532_scl_bio_pin = scl_bio;

    pio_i2c_init(bio2bufiopin[sda_bio],
                 bio2bufiopin[scl_bio],
                 bio2bufdirpin[sda_bio],
                 bio2bufdirpin[scl_bio],
                 speed_hz ? speed_hz : 100000,
                 true);

    pn532_wakeup();
    sleep_ms(10);

    uint8_t ic, ver, rev, sup;
    if (!pn532_get_firmware_version(&ic, &ver, &rev, &sup)) {
        // Retry once after wakeup pulse
        pn532_wakeup();
        sleep_ms(20);
        if (!pn532_get_firmware_version(&ic, &ver, &rev, &sup)) {
            pio_i2c_cleanup();
            pn532_is_ready_flag = false;
            return false;
        }
    }

    if (!pn532_sam_config()) {
        pio_i2c_cleanup();
        pn532_is_ready_flag = false;
        return false;
    }

    pn532_set_passive_activation_retries(0x02);
    pn532_is_ready_flag = true;
    return true;
}

void pn532_deinit(void) {
    if (pn532_is_ready_flag) {
        // Put PN532 into low power state before cleanup
        uint8_t pwr_cmd[] = { PN532_CMD_POWERDOWN, 0xC0 };
        pn532_send_command(pwr_cmd, sizeof(pwr_cmd));
        sleep_ms(5);
        pio_i2c_cleanup();
        pn532_is_ready_flag = false;
    }
}

bool pn532_is_initialized(void) {
    return pn532_is_ready_flag;
}

bool pn532_wakeup(void) {
    // Send dummy write to wake PN532 I2C peripheral from sleep
    uint8_t dummy = 0x00;
    pio_i2c_write_array_timeout(PN532_I2C_ADDR_WRITE, &dummy, 1, 0x1fff);
    sleep_ms(2);
    return true;
}

bool pn532_get_firmware_version(uint8_t *ic, uint8_t *ver, uint8_t *rev, uint8_t *support) {
    uint8_t cmd[] = { PN532_CMD_GETFIRMWAREVERSION };
    uint8_t resp[8];
    uint8_t resp_len = sizeof(resp);

    if (!pn532_transceive(cmd, sizeof(cmd), resp, &resp_len, 200)) return false;
    if (resp_len < 4) return false;

    if (ic) *ic = resp[0];
    if (ver) *ver = resp[1];
    if (rev) *rev = resp[2];
    if (support) *support = resp[3];
    return true;
}

bool pn532_sam_config(void) {
    uint8_t cmd[] = { PN532_CMD_SAMCONFIGURATION, 0x01, 0x14, 0x01 };
    uint8_t resp[4];
    uint8_t resp_len = sizeof(resp);
    return pn532_transceive(cmd, sizeof(cmd), resp, &resp_len, 200);
}

bool pn532_set_passive_activation_retries(uint8_t max_retries) {
    uint8_t cmd[] = { PN532_CMD_RFCONFIGURATION, 0x05, 0x01, 0x00, max_retries };
    uint8_t resp[4];
    uint8_t resp_len = sizeof(resp);
    return pn532_transceive(cmd, sizeof(cmd), resp, &resp_len, 200);
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
            // Could be Ultralight or NTAG; check page 3 (CC)
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
        return PN532_ERR_NO_TAG; // No targets found
    }

    // resp[0] = NbTargets (1)
    // resp[1] = Target index (1)
    // resp[2..3] = SENS_RES (ATQA)
    // resp[4] = SEL_RES (SAK)
    // resp[5] = NFCIDLength
    // resp[6..] = NFCID (UID)
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

    return pn532_in_data_exchange(auth_payload, sizeof(auth_payload), NULL, NULL, 200);
}

pn532_status_t pn532_mifare_auth_auto(uint8_t block, const uint8_t *uid, uint8_t uid_len, uint8_t *found_key) {
    for (uint32_t i = 0; i < common_keys_count; i++) {
        if (pn532_mifare_auth(block, MIFARE_CMD_AUTH_A, common_mifare_keys[i], uid, uid_len) == PN532_OK) {
            if (found_key) memcpy(found_key, common_mifare_keys[i], 6);
            return PN532_OK;
        }
    }
    return PN532_ERR_AUTH;
}

pn532_status_t pn532_mifare_read_block(uint8_t block, uint8_t *data16) {
    if (!data16) return PN532_ERR_COMM;
    uint8_t cmd[] = { MIFARE_CMD_READ, block };
    uint8_t rx_len = 16;
    return pn532_in_data_exchange(cmd, sizeof(cmd), data16, &rx_len, 200);
}

pn532_status_t pn532_mifare_write_block(uint8_t block, const uint8_t *data16) {
    if (!data16) return PN532_ERR_COMM;
    uint8_t cmd[2 + 16];
    cmd[0] = MIFARE_CMD_WRITE;
    cmd[1] = block;
    memcpy(&cmd[2], data16, 16);
    return pn532_in_data_exchange(cmd, sizeof(cmd), NULL, NULL, 300);
}

pn532_status_t pn532_ultralight_read_page(uint8_t page, uint8_t *data4) {
    if (!data4) return PN532_ERR_COMM;
    uint8_t cmd[] = { MIFARE_CMD_READ, page };
    uint8_t rx[16];
    uint8_t rx_len = sizeof(rx);

    pn532_status_t st = pn532_in_data_exchange(cmd, sizeof(cmd), rx, &rx_len, 200);
    if (st == PN532_OK && rx_len >= 4) {
        memcpy(data4, rx, 4);
        return PN532_OK;
    }
    return st;
}

pn532_status_t pn532_ultralight_write_page(uint8_t page, const uint8_t *data4) {
    if (!data4) return PN532_ERR_COMM;
    uint8_t cmd[2 + 4];
    cmd[0] = MIFARE_ULTRALIGHT_CMD_WRITE;
    cmd[1] = page;
    memcpy(&cmd[2], data4, 4);
    return pn532_in_data_exchange(cmd, sizeof(cmd), NULL, NULL, 300);
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

    // Standard manufacturer default trailer bytes
    const uint8_t def_manuf[8] = { 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69 };
    memcpy(&block0[8], def_manuf, 8);

    // Try direct write after sector 0 authentication with default key
    static const uint8_t def_key[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    (void)pn532_mifare_auth(0, MIFARE_CMD_AUTH_A, def_key, uid, 4);

    return pn532_mifare_write_block(0, block0);
}

pn532_status_t pn532_erase_tag(const pn532_tag_info_t *tag) {
    if (!tag) return PN532_ERR_COMM;

    if (tag->is_mifare_classic) {
        uint8_t zero_block[16];
        memset(zero_block, 0x00, sizeof(zero_block));
        uint8_t sectors = (tag->sak == 0x18) ? 40 : ((tag->sak == 0x09) ? 5 : 16);

        for (uint8_t s = 0; s < sectors; s++) {
            uint8_t first_block = (s < 32) ? (s * 4) : (128 + (s - 32) * 16);
            uint8_t num_blocks = (s < 32) ? 4 : 16;

            if (pn532_mifare_auth_auto(first_block, tag->uid, tag->uid_len, NULL) != PN532_OK) {
                continue;
            }

            for (uint8_t b = 0; b < num_blocks - 1; b++) {
                uint8_t blk = first_block + b;
                if (blk == 0) continue; // Never erase block 0 of standard card
                pn532_mifare_write_block(blk, zero_block);
            }
        }
        return PN532_OK;
    } else if (tag->is_ultralight_or_ntag) {
        uint8_t zero_page[4] = { 0x00, 0x00, 0x00, 0x00 };
        uint16_t max_pages = tag->total_pages ? tag->total_pages : 16;
        for (uint16_t p = 4; p < max_pages; p++) {
            pn532_ultralight_write_page((uint8_t)p, zero_page);
        }
        return PN532_OK;
    }

    return PN532_ERR_UNSUPPORTED;
}

pn532_status_t pn532_read_ndef(char *buf, size_t max_len, char *type_out, size_t type_out_len) {
    if (!buf || max_len < 8) return PN532_ERR_COMM;

    uint8_t raw[64];
    memset(raw, 0, sizeof(raw));

    // Read user data starting at page 4 (pages 4, 5, 6, 7 = 16 bytes)
    for (uint8_t i = 0; i < 4; i++) {
        if (pn532_ultralight_read_page(4 + i * 4, &raw[i * 16]) != PN532_OK) {
            break;
        }
    }

    // Find NDEF TLV (0x03)
    uint8_t offset = 0;
    while (offset < sizeof(raw) && raw[offset] != 0x03 && raw[offset] != 0xFE) {
        offset++;
    }

    if (offset >= sizeof(raw) || raw[offset] != 0x03) {
        return PN532_ERR_NO_TAG; // No NDEF TLV found
    }

    offset++; // Skip 0x03
    uint8_t ndef_len = raw[offset++];
    if (ndef_len == 0 || offset + ndef_len > sizeof(raw)) {
        return PN532_ERR_COMM;
    }

    // Parse NDEF Record Header: 0xD1 or similar
    uint8_t header = raw[offset++];
    uint8_t type_len = raw[offset++];
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
        offset += lang_len; // Skip language code
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
    uint8_t ndef_len = 4 + payload_len; // Header(1) + TypeLen(1) + PayLen(1) + Type(1) + Payload

    uint8_t buffer[64];
    memset(buffer, 0, sizeof(buffer));

    uint8_t idx = 0;
    buffer[idx++] = 0x03; // NDEF TLV
    buffer[idx++] = ndef_len;
    buffer[idx++] = 0xD1; // MB=1, ME=1, SR=1, TNF=0x01
    buffer[idx++] = 0x01; // Type length
    buffer[idx++] = payload_len;
    buffer[idx++] = 0x55; // 'U'
    buffer[idx++] = prefix;
    memcpy(&buffer[idx], body, body_len);
    idx += body_len;
    buffer[idx++] = 0xFE; // Terminator TLV

    // Write page by page (4 bytes each) starting from page 4
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
    uint8_t payload_len = 1 + 2 + text_len; // Status(1) + "en"(2) + text
    uint8_t ndef_len = 4 + payload_len;

    uint8_t buffer[64];
    memset(buffer, 0, sizeof(buffer));

    uint8_t idx = 0;
    buffer[idx++] = 0x03; // NDEF TLV
    buffer[idx++] = ndef_len;
    buffer[idx++] = 0xD1;
    buffer[idx++] = 0x01; // Type length
    buffer[idx++] = payload_len;
    buffer[idx++] = 0x54; // 'T'
    buffer[idx++] = 0x02; // Status byte: UTF-8, len=2
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
