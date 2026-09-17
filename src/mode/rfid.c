/**
 * @file rfid.c
 * @brief RFID / NFC Mode for Bus Pirate 5/6
 * @details Implements PN532 RFID/NFC reader/writer support,
 *          supporting MIFARE Classic, Ultralight, NTAG, UID magic cloning, and NDEF.
 * @author Bus Pirate Project
 * @date 2026
 */

#include <stdbool.h>
#include <errno.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pirate.h"
#include "system_config.h"
#include "command_struct.h"
#include "pirate/bio.h"
#include "pirate/pn532.h"
#include "pirate/storage.h"
#include "ui/ui_help.h"
#include "ui/ui_term.h"
#include "lib/bp_args/bp_cmd.h"
#include "usb_rx.h"
#include "fatfs/ff.h"
#include "mode/rfid.h"

static struct {
    uint32_t sda_pin;
    uint32_t scl_pin;
    uint32_t speed_khz;
    uint32_t initialized;
} rfid_config = { BIO0, BIO1, 100, 0 };

static const char pin_labels[][5] = { "SDA", "SCL" };

// Parameter constraints for setup
static const bp_val_constraint_t sda_pin_range = {
    .type = BP_VAL_UINT32,
    .u = { .min = BIO0, .max = BIO7, .def = BIO0 },
};
static const bp_val_constraint_t scl_pin_range = {
    .type = BP_VAL_UINT32,
    .u = { .min = BIO0, .max = BIO7, .def = BIO1 },
};
static const bp_val_choice_t speed_choices[] = {
    { "100", "Standard Mode (100kHz)", 0, 100 },
    { "400", "Fast Mode (400kHz)", 0, 400 },
};
static const bp_val_constraint_t speed_choice = {
    .type = BP_VAL_CHOICE,
    .choice = { .choices = speed_choices, .count = count_of(speed_choices), .def = 0 },
};

// Setup command definition
static const bp_command_opt_t rfid_setup_opts[] = {
    { "sda", 's', BP_ARG_REQUIRED, "SDA BIO pin (0-7)", 0 },
    { "scl", 'c', BP_ARG_REQUIRED, "SCL BIO pin (0-7)", 0 },
    { "speed", 'k', BP_ARG_REQUIRED, "kHz (100, 400)", 0 },
    { 0 }
};
const bp_command_def_t rfid_setup_def = {
    .name = "rfid", .description = 0, .opts = rfid_setup_opts,
};

// Helper: parse hex string into byte array
static size_t parse_hex_bytes(const char *hex_str, uint8_t *out, size_t max_out) {
    if (!hex_str || !out || max_out == 0) return 0;
    size_t count = 0;
    const char *p = hex_str;
    while (*p && count < max_out) {
        while (*p == ' ' || *p == ':' || *p == '-') p++;
        if (!*p) break;
        if (!isxdigit((unsigned char)p[0])) break;

        char byte_str[3] = { 0, 0, 0 };
        byte_str[0] = *p++;
        if (isxdigit((unsigned char)*p)) {
            byte_str[1] = *p++;
        }
        out[count++] = (uint8_t)strtoul(byte_str, NULL, 16);
    }
    return count;
}

static bool command_ready(void) {
    if (rfid_config.initialized && pn532_is_initialized()) return true;
    printf("PN532 RFID controller is not initialized. Check wiring and 3.3V power.\r\n");
    return false;
}

// -----------------------------------------------------------------------------
// Command: info
// -----------------------------------------------------------------------------
static const bp_command_def_t info_def = {
    .name = "info", .description = 0,
};

static void info_handler(struct command_result *res) {
    if (bp_cmd_help_check(&info_def, res->help_flag)) return;
    if (!command_ready()) return;

    uint8_t ic = 0, ver = 0, rev = 0, sup = 0;
    if (!pn532_get_firmware_version(&ic, &ver, &rev, &sup)) {
        printf("Failed to query PN532 firmware information.\r\n");
        return;
    }

    printf("\r\n%s--- PN532 NFC/RFID Controller ---%s\r\n", ui_term_color_info(), ui_term_color_reset());
    printf("Chip IC:           0x%02X (PN5%02X)\r\n", ic, ic);
    printf("Firmware Version:  %u.%u\r\n", ver, rev);
    printf("Supported Modes:   0x%02X\r\n", sup);
    if (sup & 0x01) printf("  - ISO/IEC 14443 Type A (MIFARE)\r\n");
    if (sup & 0x02) printf("  - ISO/IEC 14443 Type B\r\n");
    if (sup & 0x04) printf("  - ISO/IEC 18092 (FeliCa / NFCIP-1)\r\n");
    printf("Interface:         I2C @ %u kHz (SDA: BIO%u, SCL: BIO%u)\r\n\r\n",
           rfid_config.speed_khz, rfid_config.sda_pin, rfid_config.scl_pin);
}

// -----------------------------------------------------------------------------
// Helper: Print Tag Summary
// -----------------------------------------------------------------------------
static void print_tag_header(const pn532_tag_info_t *tag) {
    printf("\r\n%s[+] Tag Detected:%s\r\n", ui_term_color_prompt(), ui_term_color_reset());
    printf("  UID:   ");
    for (uint8_t i = 0; i < tag->uid_len; i++) {
        printf("%02X ", tag->uid[i]);
    }
    printf("(%u bytes)\r\n", tag->uid_len);
    printf("  ATQA:  0x%04X\r\n", tag->atqa);
    printf("  SAK:   0x%02X\r\n", tag->sak);
    printf("  Type:  %s\r\n", tag->type_name);
}

// -----------------------------------------------------------------------------
// Command: read
// -----------------------------------------------------------------------------
static const bp_command_opt_t read_opts[] = {
    { "single", 's', BP_ARG_NONE, NULL, 0 },
    { 0 }
};
static const char *const read_usage[] = {
    "read [-s(ingle)]",
    "Continuous read mode:%s read",
    "Single tag read:%s read -s",
};
static const bp_command_def_t read_def = {
    .name = "read", .description = 0, .opts = read_opts,
    .usage = read_usage, .usage_count = count_of(read_usage),
};

static void read_handler(struct command_result *res) {
    if (bp_cmd_help_check(&read_def, res->help_flag)) return;
    if (!command_ready()) return;

    bool single = bp_cmd_find_flag(&read_def, 's');

    printf("\r\nWaiting for RFID/NFC tag... Press any key to stop.\r\n");

    uint8_t last_uid[10];
    uint8_t last_uid_len = 0;
    uint32_t last_seen_time = 0;

    while (true) {
        char c;
        if (rx_fifo_try_get(&c)) {
            printf("Stopped by user.\r\n");
            break;
        }

        pn532_tag_info_t tag;
        pn532_status_t st = pn532_detect_tag(&tag, 150);
        if (st == PN532_OK) {
            uint32_t now = to_ms_since_boot(get_absolute_time());
            bool same_tag = (last_uid_len == tag.uid_len && memcmp(last_uid, tag.uid, tag.uid_len) == 0);

            if (!same_tag || (now - last_seen_time) > 1500) {
                memcpy(last_uid, tag.uid, tag.uid_len);
                last_uid_len = tag.uid_len;
                last_seen_time = now;

                print_tag_header(&tag);

                // Read memory preview
                if (tag.is_mifare_classic) {
                    uint8_t key[6];
                    if (pn532_mifare_auth_auto(0, tag.uid, tag.uid_len, key) == PN532_OK) {
                        uint8_t blk0[16];
                        if (pn532_mifare_read_block(0, blk0) == PN532_OK) {
                            printf("  Block 00: ");
                            for (int i = 0; i < 16; i++) printf("%02X ", blk0[i]);
                            printf("\r\n");
                        }
                    } else {
                        printf("  (Default keys failed to authenticate Sector 0)\r\n");
                    }
                } else if (tag.is_ultralight_or_ntag) {
                    printf("  Pages 00-03 Preview:\r\n");
                    for (uint8_t p = 0; p < 4; p++) {
                        uint8_t page_data[4];
                        if (pn532_ultralight_read_page(p, page_data) == PN532_OK) {
                            printf("    Page %02u: %02X %02X %02X %02X\r\n",
                                   p, page_data[0], page_data[1], page_data[2], page_data[3]);
                        }
                    }
                }
                printf("\r\n");

                if (single) break;
            }
        }
        sleep_ms(50);
    }
}

// -----------------------------------------------------------------------------
// Command: dump
// -----------------------------------------------------------------------------
static const bp_command_opt_t dump_opts[] = {
    { "file", 'f', BP_ARG_REQUIRED, "save to TF-card file", 0 },
    { 0 }
};
static const char *const dump_usage[] = {
    "dump [-f filename]",
    "Dump memory to terminal:%s dump",
    "Dump memory to TF-card:%s dump -f card.txt",
};
static const bp_command_def_t dump_def = {
    .name = "dump", .description = 0, .opts = dump_opts,
    .usage = dump_usage, .usage_count = count_of(dump_usage),
};

static void dump_handler(struct command_result *res) {
    if (bp_cmd_help_check(&dump_def, res->help_flag)) return;
    if (!command_ready()) return;

    char file_name[32] = { 0 };
    bool save_file = bp_cmd_get_string(&dump_def, 'f', file_name, sizeof(file_name));

    FIL fil;
    bool fil_open = false;
    if (save_file) {
        FRESULT fr = f_open(&fil, file_name, FA_WRITE | FA_CREATE_ALWAYS);
        if (fr != FR_OK) {
            printf("Failed to open file '%s' on storage for writing.\r\n", file_name);
            return;
        }
        fil_open = true;
    }

    printf("Place tag on reader... Press any key to cancel.\r\n");
    pn532_tag_info_t tag;
    while (true) {
        char c;
        if (rx_fifo_try_get(&c)) {
            printf("Dump cancelled.\r\n");
            if (fil_open) f_close(&fil);
            return;
        }
        if (pn532_detect_tag(&tag, 150) == PN532_OK) break;
        sleep_ms(50);
    }

    print_tag_header(&tag);
    if (fil_open) {
        f_printf(&fil, "# RFID/NFC Tag Dump\r\nUID: ");
        for (uint8_t i = 0; i < tag.uid_len; i++) f_printf(&fil, "%02X ", tag.uid[i]);
        f_printf(&fil, "\r\nATQA: 0x%04X\r\nSAK: 0x%02X\r\nType: %s\r\n\r\n", tag.atqa, tag.sak, tag.type_name);
    }

    if (tag.is_mifare_classic) {
        uint8_t sectors = (tag.sak == 0x18) ? 40 : ((tag.sak == 0x09) ? 5 : 16);
        printf("\r\nReading MIFARE Classic (%u sectors)...\r\n\r\n", sectors);

        for (uint8_t s = 0; s < sectors; s++) {
            uint8_t first_block = (s < 32) ? (s * 4) : (128 + (s - 32) * 16);
            uint8_t num_blocks = (s < 32) ? 4 : 16;
            uint8_t key[6];

            if (pn532_mifare_auth_auto(first_block, tag.uid, tag.uid_len, key) != PN532_OK) {
                printf("Sector %02u: [Authentication Failed]\r\n", s);
                if (fil_open) f_printf(&fil, "Sector %02u: [Auth Failed]\r\n", s);
                continue;
            }

            printf("Sector %02u (Key: %02X%02X%02X%02X%02X%02X):\r\n",
                   s, key[0], key[1], key[2], key[3], key[4], key[5]);
            if (fil_open) {
                f_printf(&fil, "Sector %02u (Key: %02X%02X%02X%02X%02X%02X):\r\n",
                         s, key[0], key[1], key[2], key[3], key[4], key[5]);
            }

            for (uint8_t b = 0; b < num_blocks; b++) {
                uint8_t blk = first_block + b;
                uint8_t blk_data[16];
                if (pn532_mifare_read_block(blk, blk_data) == PN532_OK) {
                    printf("  Block %03u: ", blk);
                    if (fil_open) f_printf(&fil, "  Block %03u: ", blk);
                    for (int i = 0; i < 16; i++) {
                        printf("%02X ", blk_data[i]);
                        if (fil_open) f_printf(&fil, "%02X ", blk_data[i]);
                    }
                    printf(" |");
                    if (fil_open) f_printf(&fil, " |");
                    for (int i = 0; i < 16; i++) {
                        char ch = (blk_data[i] >= 32 && blk_data[i] <= 126) ? (char)blk_data[i] : '.';
                        printf("%c", ch);
                        if (fil_open) f_printf(&fil, "%c", ch);
                    }
                    printf("|\r\n");
                    if (fil_open) f_printf(&fil, "|\r\n");
                }
            }
        }
    } else if (tag.is_ultralight_or_ntag) {
        uint16_t pages = tag.total_pages ? tag.total_pages : 16;
        printf("\r\nReading MIFARE Ultralight / NTAG (%u pages)...\r\n\r\n", pages);

        for (uint16_t p = 0; p < pages; p++) {
            uint8_t page_data[4];
            if (pn532_ultralight_read_page((uint8_t)p, page_data) == PN532_OK) {
                printf("Page %03u: %02X %02X %02X %02X  |", p,
                       page_data[0], page_data[1], page_data[2], page_data[3]);
                if (fil_open) {
                    f_printf(&fil, "Page %03u: %02X %02X %02X %02X  |", p,
                             page_data[0], page_data[1], page_data[2], page_data[3]);
                }
                for (int i = 0; i < 4; i++) {
                    char ch = (page_data[i] >= 32 && page_data[i] <= 126) ? (char)page_data[i] : '.';
                    printf("%c", ch);
                    if (fil_open) f_printf(&fil, "%c", ch);
                }
                printf("|\r\n");
                if (fil_open) f_printf(&fil, "|\r\n");
            } else {
                printf("Page %03u: [Read Error]\r\n", p);
                if (fil_open) f_printf(&fil, "Page %03u: [Read Error]\r\n", p);
                break;
            }
        }
    } else {
        printf("Direct dump is not supported for this tag type (SAK: 0x%02X).\r\n", tag.sak);
    }

    if (fil_open) {
        f_close(&fil);
        printf("\r\nDump saved successfully to '%s'.\r\n", file_name);
    }
}

// -----------------------------------------------------------------------------
// Command: write
// -----------------------------------------------------------------------------
static const bp_command_opt_t write_opts[] = {
    { "block", 'b', BP_ARG_REQUIRED, "block or page index", 0 },
    { "data", 'd', BP_ARG_REQUIRED, "hex data (4 or 16 bytes)", 0 },
    { "uid", 'u', BP_ARG_REQUIRED, "magic UID hex (4 bytes)", 0 },
    { 0 }
};
static const char *const write_usage[] = {
    "write [-b block -d hex] | [-u uid_hex]",
    "Write 16 bytes to MIFARE block 4:%s write -b 4 -d 00112233445566778899AABBCCDDEEFF",
    "Write 4 bytes to Ultralight page 4:%s write -b 4 -d 01020304",
    "Write magic UID (4 bytes):%s write -u 12345678",
};
static const bp_command_def_t write_def = {
    .name = "write", .description = 0, .opts = write_opts,
    .usage = write_usage, .usage_count = count_of(write_usage),
};

static void write_handler(struct command_result *res) {
    if (bp_cmd_help_check(&write_def, res->help_flag)) return;
    if (!command_ready()) return;

    char uid_str[32] = { 0 };
    if (bp_cmd_get_string(&write_def, 'u', uid_str, sizeof(uid_str))) {
        uint8_t target_uid[10];
        size_t uid_len = parse_hex_bytes(uid_str, target_uid, sizeof(target_uid));
        if (uid_len != 4) {
            printf("Error: UID write requires exactly 4 bytes (e.g. 12345678 or 12:34:56:78).\r\n");
            return;
        }

        printf("Place target MAGIC card on reader... Press any key to cancel.\r\n");
        pn532_tag_info_t tag;
        while (true) {
            char c;
            if (rx_fifo_try_get(&c)) { printf("Cancelled.\r\n"); return; }
            if (pn532_detect_tag(&tag, 150) == PN532_OK) break;
            sleep_ms(50);
        }

        printf("Target detected. Writing Block 0 UID: %02X %02X %02X %02X...\r\n",
               target_uid[0], target_uid[1], target_uid[2], target_uid[3]);

        pn532_status_t st = pn532_write_magic_block0(target_uid, 4, tag.sak, tag.atqa);
        if (st == PN532_OK) {
            printf("%s[+] Success! UID successfully updated on Magic Card.%s\r\n",
                   ui_term_color_prompt(), ui_term_color_reset());
        } else {
            printf("[-] Failed to write block 0. Ensure the card is a Gen2 Magic UID rewritable tag.\r\n");
        }
        return;
    }

    uint32_t block_idx = 0;
    if (!bp_cmd_flag(&write_def, 'b', &block_idx)) {
        printf("Missing block/page index (-b) or UID flag (-u). See 'write -h'.\r\n");
        return;
    }

    char data_str[64] = { 0 };
    if (!bp_cmd_get_string(&write_def, 'd', data_str, sizeof(data_str))) {
        printf("Missing hex data (-d). See 'write -h'.\r\n");
        return;
    }

    uint8_t payload[32];
    size_t data_len = parse_hex_bytes(data_str, payload, sizeof(payload));
    if (data_len != 4 && data_len != 16) {
        printf("Error: Data length must be 4 bytes (Ultralight) or 16 bytes (MIFARE Classic).\r\n");
        return;
    }

    printf("Place target tag on reader... Press any key to cancel.\r\n");
    pn532_tag_info_t tag;
    while (true) {
        char c;
        if (rx_fifo_try_get(&c)) { printf("Cancelled.\r\n"); return; }
        if (pn532_detect_tag(&tag, 150) == PN532_OK) break;
        sleep_ms(50);
    }

    if (data_len == 16 && tag.is_mifare_classic) {
        uint8_t key[6];
        if (pn532_mifare_auth_auto((uint8_t)block_idx, tag.uid, tag.uid_len, key) != PN532_OK) {
            printf("Error: Authentication failed for block %u.\r\n", block_idx);
            return;
        }
        pn532_status_t st = pn532_mifare_write_block((uint8_t)block_idx, payload);
        if (st == PN532_OK) {
            printf("%s[+] Successfully wrote 16 bytes to Block %u.%s\r\n",
                   ui_term_color_prompt(), ui_term_color_reset(), block_idx);
        } else {
            printf("[-] Failed to write Block %u (%s).\r\n", block_idx, pn532_status_str(st));
        }
    } else if (data_len == 4 && tag.is_ultralight_or_ntag) {
        pn532_status_t st = pn532_ultralight_write_page((uint8_t)block_idx, payload);
        if (st == PN532_OK) {
            printf("%s[+] Successfully wrote 4 bytes to Page %u.%s\r\n",
                   ui_term_color_prompt(), ui_term_color_reset(), block_idx);
        } else {
            printf("[-] Failed to write Page %u (%s).\r\n", block_idx, pn532_status_str(st));
        }
    } else {
        printf("Data size (%u bytes) does not match tag architecture (%s).\r\n",
               (unsigned int)data_len, tag.type_name);
    }
}

// -----------------------------------------------------------------------------
// Command: clone
// -----------------------------------------------------------------------------
static const bp_command_def_t clone_def = {
    .name = "clone", .description = 0,
};

static void clone_handler(struct command_result *res) {
    if (bp_cmd_help_check(&clone_def, res->help_flag)) return;
    if (!command_ready()) return;

    printf("\r\n=== RFID UID Cloning Tool ===\r\n");
    printf("[Step 1] Place SOURCE card on the reader... Press any key to cancel.\r\n");

    pn532_tag_info_t src_tag;
    while (true) {
        char c;
        if (rx_fifo_try_get(&c)) { printf("Cloning cancelled.\r\n"); return; }
        if (pn532_detect_tag(&src_tag, 150) == PN532_OK) break;
        sleep_ms(50);
    }

    if (src_tag.uid_len != 4) {
        printf("Cloning currently supports 4-byte MIFARE Classic UIDs (found %u bytes).\r\n", src_tag.uid_len);
        return;
    }

    print_tag_header(&src_tag);
    printf("\r\n[Step 2] Remove source card.\r\n");
    printf("Place target MAGIC card on reader and press any key when ready...\r\n");

    // Wait for keypress
    while (true) {
        char c;
        if (rx_fifo_try_get(&c)) break;
        sleep_ms(50);
    }

    pn532_tag_info_t tgt_tag;
    printf("Detecting target card...\r\n");
    while (true) {
        char c;
        if (rx_fifo_try_get(&c)) { printf("Cloning cancelled.\r\n"); return; }
        if (pn532_detect_tag(&tgt_tag, 150) == PN532_OK) break;
        sleep_ms(50);
    }

    printf("Target detected (UID: ");
    for (uint8_t i = 0; i < tgt_tag.uid_len; i++) printf("%02X ", tgt_tag.uid[i]);
    printf(").\r\nCloning source UID to block 0...\r\n");

    pn532_status_t st = pn532_write_magic_block0(src_tag.uid, 4, src_tag.sak, src_tag.atqa);
    if (st == PN532_OK) {
        printf("%s[+] Clone complete! UID successfully written to Magic Card.%s\r\n\r\n",
               ui_term_color_prompt(), ui_term_color_reset());
    } else {
        printf("[-] Failed to clone. The target card must be a Gen2 Magic UID rewritable tag.\r\n\r\n");
    }
}

// -----------------------------------------------------------------------------
// Command: erase
// -----------------------------------------------------------------------------
static const bp_command_def_t erase_def = {
    .name = "erase", .description = 0,
};

static void erase_handler(struct command_result *res) {
    if (bp_cmd_help_check(&erase_def, res->help_flag)) return;
    if (!command_ready()) return;

    printf("WARNING: This will overwrite user data blocks with 0x00.\r\n");
    printf("Continue? (y/n): ");
    char c = 0;
    while (true) {
        if (rx_fifo_try_get(&c)) {
            printf("%c\r\n", c);
            break;
        }
        sleep_ms(20);
    }
    if (c != 'y' && c != 'Y') {
        printf("Erase aborted.\r\n");
        return;
    }

    printf("Place tag on reader... Press any key to cancel.\r\n");
    pn532_tag_info_t tag;
    while (true) {
        char k;
        if (rx_fifo_try_get(&k)) { printf("Cancelled.\r\n"); return; }
        if (pn532_detect_tag(&tag, 150) == PN532_OK) break;
        sleep_ms(50);
    }

    print_tag_header(&tag);
    printf("Erasing tag data blocks...\r\n");

    pn532_status_t st = pn532_erase_tag(&tag);
    if (st == PN532_OK) {
        printf("%s[+] Tag data blocks erased successfully.%s\r\n",
               ui_term_color_prompt(), ui_term_color_reset());
    } else {
        printf("[-] Failed to erase tag: %s\r\n", pn532_status_str(st));
    }
}

// -----------------------------------------------------------------------------
// Command: ndef
// -----------------------------------------------------------------------------
static const bp_command_opt_t ndef_opts[] = {
    { "uri", 'u', BP_ARG_REQUIRED, "NDEF URL / URI", 0 },
    { "msg", 'm', BP_ARG_REQUIRED, "NDEF text message", 0 },
    { 0 }
};
static const char *const ndef_usage[] = {
    "ndef [-u uri] | [-m message]",
    "Read existing NDEF record:%s ndef",
    "Write website URL:%s ndef -u https://buspirate.com",
    "Write text message:%s ndef -m \"Bus Pirate 5\"",
};
static const bp_command_def_t ndef_def = {
    .name = "ndef", .description = 0, .opts = ndef_opts,
    .usage = ndef_usage, .usage_count = count_of(ndef_usage),
};

static void ndef_handler(struct command_result *res) {
    if (bp_cmd_help_check(&ndef_def, res->help_flag)) return;
    if (!command_ready()) return;

    char uri_str[64] = { 0 };
    char msg_str[64] = { 0 };
    bool has_uri = bp_cmd_get_string(&ndef_def, 'u', uri_str, sizeof(uri_str));
    bool has_msg = bp_cmd_get_string(&ndef_def, 'm', msg_str, sizeof(msg_str));

    printf("Place Ultralight/NTAG tag on reader... Press any key to cancel.\r\n");
    pn532_tag_info_t tag;
    while (true) {
        char c;
        if (rx_fifo_try_get(&c)) { printf("Cancelled.\r\n"); return; }
        if (pn532_detect_tag(&tag, 150) == PN532_OK) break;
        sleep_ms(50);
    }

    print_tag_header(&tag);

    if (has_uri) {
        printf("Writing NDEF URI: %s\r\n", uri_str);
        pn532_status_t st = pn532_write_ndef_uri(uri_str);
        if (st == PN532_OK) {
            printf("%s[+] Successfully wrote NDEF URI!%s\r\n", ui_term_color_prompt(), ui_term_color_reset());
        } else {
            printf("[-] Failed writing NDEF URI (%s).\r\n", pn532_status_str(st));
        }
    } else if (has_msg) {
        printf("Writing NDEF Text: %s\r\n", msg_str);
        pn532_status_t st = pn532_write_ndef_text(msg_str);
        if (st == PN532_OK) {
            printf("%s[+] Successfully wrote NDEF Text!%s\r\n", ui_term_color_prompt(), ui_term_color_reset());
        } else {
            printf("[-] Failed writing NDEF Text (%s).\r\n", pn532_status_str(st));
        }
    } else {
        char payload[80] = { 0 };
        char type_str[16] = { 0 };
        pn532_status_t st = pn532_read_ndef(payload, sizeof(payload), type_str, sizeof(type_str));
        if (st == PN532_OK) {
            printf("%s[+] NDEF Record Found (%s):%s\r\n", ui_term_color_prompt(), type_str, ui_term_color_reset());
            printf("    %s\r\n", payload);
        } else {
            printf("No readable NDEF record found on tag (%s).\r\n", pn532_status_str(st));
        }
    }
}

// Command table
const struct _mode_command_struct rfid_commands[] = {
    { .func = read_handler, .def = &read_def, .supress_fala_capture = true },
    { .func = dump_handler, .def = &dump_def, .supress_fala_capture = true },
    { .func = write_handler, .def = &write_def, .supress_fala_capture = true },
    { .func = clone_handler, .def = &clone_def, .supress_fala_capture = true },
    { .func = erase_handler, .def = &erase_def, .supress_fala_capture = true },
    { .func = ndef_handler, .def = &ndef_def, .supress_fala_capture = true },
    { .func = info_handler, .def = &info_def, .supress_fala_capture = true },
};
const uint32_t rfid_commands_count = count_of(rfid_commands);

// Setup UI
uint32_t rfid_setup(void) {
    const char config_file[] = "bprfid.bp";
    const mode_config_t config[] = {
        { "$.sda_pin", &rfid_config.sda_pin, MODE_CONFIG_FORMAT_DECIMAL },
        { "$.scl_pin", &rfid_config.scl_pin, MODE_CONFIG_FORMAT_DECIMAL },
        { "$.speed_khz", &rfid_config.speed_khz, MODE_CONFIG_FORMAT_DECIMAL },
    };

    uint32_t sda, scl, speed;
    bp_cmd_status_t status = bp_cmd_flag(&rfid_setup_def, 's', &sda);
    if (status == BP_CMD_INVALID) return 0;

    if (status == BP_CMD_MISSING) {
        if (!storage_load_mode(config_file, config, count_of(config)) ||
            rfid_config.sda_pin > BIO7 || rfid_config.scl_pin > BIO7 ||
            (rfid_config.speed_khz != 100 && rfid_config.speed_khz != 400)) {
            rfid_config.sda_pin = BIO0;
            rfid_config.scl_pin = BIO1;
            rfid_config.speed_khz = 100;
        }

        if (bp_cmd_prompt(&sda_pin_range, &sda) != BP_CMD_OK) return 0;
        if (bp_cmd_prompt(&scl_pin_range, &scl) != BP_CMD_OK) return 0;
        if (bp_cmd_prompt(&speed_choice, &speed) != BP_CMD_OK) return 0;
    } else {
        status = bp_cmd_flag(&rfid_setup_def, 'c', &scl);
        if (status == BP_CMD_INVALID) return 0;
        if (status == BP_CMD_MISSING) scl = BIO1;

        status = bp_cmd_flag(&rfid_setup_def, 'k', &speed);
        if (status == BP_CMD_INVALID) return 0;
        if (status == BP_CMD_MISSING) speed = 100;
    }

    if (sda == scl) {
        printf("Error: SDA and SCL cannot use the same pin.\r\n");
        return 0;
    }

    rfid_config.sda_pin = sda;
    rfid_config.scl_pin = scl;
    rfid_config.speed_khz = speed;

    storage_save_mode(config_file, config, count_of(config));
    return 1;
}

uint32_t rfid_setup_exc(void) {
    system_bio_update_purpose_and_label(true, (uint8_t)rfid_config.sda_pin, BP_PIN_MODE, pin_labels[0]);
    system_bio_update_purpose_and_label(true, (uint8_t)rfid_config.scl_pin, BP_PIN_MODE, pin_labels[1]);

    rfid_config.initialized = pn532_init((uint8_t)rfid_config.sda_pin,
                                        (uint8_t)rfid_config.scl_pin,
                                        rfid_config.speed_khz * 1000);

    if (!rfid_config.initialized) {
        printf("\r\n[-] PN532 not detected. Check I2C wiring (SDA/SCL) and 3.3V power.\r\n");
        rfid_cleanup();
        return 0;
    }

    uint8_t ic, ver, rev, sup;
    pn532_get_firmware_version(&ic, &ver, &rev, &sup);
    printf("\r\n[+] PN532 detected (IC=0x%02X, Ver %u.%u), I2C @ %ukHz.\r\n",
           ic, ver, rev, rfid_config.speed_khz);
    return 1;
}

void rfid_cleanup(void) {
    if (rfid_config.initialized) {
        pn532_deinit();
    }
    rfid_config.initialized = 0;
    system_bio_update_purpose_and_label(false, (uint8_t)rfid_config.sda_pin, BP_PIN_MODE, 0);
    system_bio_update_purpose_and_label(false, (uint8_t)rfid_config.scl_pin, BP_PIN_MODE, 0);
}

void rfid_settings(void) {
    printf("PN532 SDA: BIO%u\r\n", rfid_config.sda_pin);
    printf("PN532 SCL: BIO%u\r\n", rfid_config.scl_pin);
    printf("I2C Speed: %u kHz\r\n", rfid_config.speed_khz);
}

void rfid_help(void) {
    printf("NXP PN532 RFID / NFC Reader/Writer Mode.\r\n\r\n");
    printf("Wiring: Connect PN532 to 3.3V and GND. Connect SDA to BIO%u, SCL to BIO%u.\r\n",
           rfid_config.sda_pin, rfid_config.scl_pin);
    printf("Ensure I2C pull-up resistors (P command) or onboard pull-ups are enabled.\r\n\r\n");
    ui_help_mode_commands(rfid_commands, rfid_commands_count);
}

bool rfid_preflight_sanity_check(void) {
    return ui_help_sanity_check(true, 0x00);
}
