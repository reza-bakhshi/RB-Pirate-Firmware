/**
 * @file rfid.c
 * @brief RFID / NFC Mode for Bus Pirate 5/6
 * @details Implements PN532 RFID/NFC reader/writer support over High-Speed UART (HSU),
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
#include "pirate/psu.h"
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
    uint32_t pin_pair;
    uint32_t tx_pin;
    uint32_t rx_pin;
    uint32_t baudrate;
    uint32_t initialized;
} rfid_config = { 0, BIO4, BIO5, 115200, 0 };

static const char pin_labels[][5] = { "TX->", "RX<-" };

// Parameter choices for setup
static const bp_val_choice_t pin_choices[] = {
    { "0", "TX: BIO4, RX: BIO5 (uart0 - Default)", 0, 0 },
    { "1", "TX: BIO0, RX: BIO1 (uart1)", 0, 1 },
};
static const bp_val_constraint_t pin_choice = {
    .type = BP_VAL_CHOICE,
    .choice = { .choices = pin_choices, .count = count_of(pin_choices), .def = 0 },
    .prompt = (uint32_t)(uintptr_t)"Select UART Pin Pair",
    .hint = (uint32_t)(uintptr_t)"0: TX=BIO4, RX=BIO5 | 1: TX=BIO0, RX=BIO1",
};

static const bp_val_choice_t baud_choices[] = {
    { "115200", "115200 baud (PN532 default)", 0, 115200 },
    { "230400", "230400 baud (SetSerialBaudrate switch)", 0, 230400 },
    { "460800", "460800 baud (SetSerialBaudrate switch)", 0, 460800 },
    { "921600", "921600 baud (short wires only)", 0, 921600 },
    { "9600", "9600 baud", 0, 9600 },
};
static const bp_val_constraint_t baud_choice = {
    .type = BP_VAL_CHOICE,
    .choice = { .choices = baud_choices, .count = count_of(baud_choices), .def = 0 },
    .prompt = (uint32_t)(uintptr_t)"Select Baud Rate",
    .hint = (uint32_t)(uintptr_t)"Default: 115200",
};

static bool rfid_baud_supported(uint32_t baud) {
    for (size_t i = 0; i < count_of(baud_choices); i++) {
        if (baud_choices[i].value == baud) return true;
    }
    return false;
}

// Setup command definition
static const bp_command_opt_t rfid_setup_opts[] = {
    { "pins", 'p', BP_ARG_REQUIRED, "pair", (uint32_t)(uintptr_t)"Select UART pins: 0=BIO4/BIO5, 1=BIO0/BIO1", &pin_choice },
    { "baud", 'b', BP_ARG_REQUIRED, "baud", (uint32_t)(uintptr_t)"UART baud rate (default 115200)", &baud_choice },
    { 0 }
};
const bp_command_def_t rfid_setup_def = {
    .name = "rfid",
    .description = (uint32_t)(uintptr_t)"RFID/NFC mode using PN532 over UART (HSU)",
    .opts = rfid_setup_opts,
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

// Helper: try a user-supplied key first, then fall back to the dictionary.
// Tries Key A then Key B in dictionary mode, like nfc-mfclassic, and reports
// the working key bytes and type (0x60=A, 0x61=B) for teaching purposes.
static pn532_status_t rfid_auth_sector(uint8_t first_block, const uint8_t *uid, uint8_t uid_len,
                                       const uint8_t *custom_key, bool has_custom, uint8_t custom_type,
                                       uint8_t *out_key, uint8_t *out_type) {
    if (has_custom && custom_key) {
        if (pn532_mifare_auth_key(first_block, uid, uid_len, custom_key, custom_type) == PN532_OK) {
            if (out_key) memcpy(out_key, custom_key, 6);
            if (out_type) *out_type = custom_type;
            return PN532_OK;
        }
        // Fall through to dictionary so one typo does not hide a known key.
    }
    uint8_t found_key[6];
    uint8_t found_type = MIFARE_CMD_AUTH_A;
    pn532_status_t st = pn532_mifare_auth_auto_type(first_block, uid, uid_len, found_key, &found_type);
    if (st == PN532_OK) {
        if (out_key) memcpy(out_key, found_key, 6);
        if (out_type) *out_type = found_type;
    }
    return st;
}

// Returns true when -k <12 hex digits> was given and valid. type_out defaults
// to Key A unless -B was passed.
static bool rfid_parse_custom_key(const bp_command_def_t *def, uint8_t *key_out, uint8_t *type_out) {
    char key_str[32] = { 0 };
    if (!bp_cmd_get_string(def, 'k', key_str, sizeof(key_str))) return false;
    if (parse_hex_bytes(key_str, key_out, 6) != 6) {
        printf("Invalid key '%s': need exactly 6 bytes (12 hex digits, e.g. FFFFFFFFFFFF).\r\n", key_str);
        return false;
    }
    if (type_out) *type_out = bp_cmd_find_flag(def, 'B') ? MIFARE_CMD_AUTH_B : MIFARE_CMD_AUTH_A;
    return true;
}

static bool rfid_is_trailer(uint8_t block, uint8_t sak) {
    (void)sak;
    if (block < 128) return (block % 4) == 3;
    return ((block - 128) % 16) == 15;
}

// Lazy connect check with diagnostic message if unpowered
static bool command_ready(void) {
    if (!pn532_is_initialized()) {
        printf("%sRFID mode is not configured.%s\r\n", ui_term_color_error(), ui_term_color_reset());
        return false;
    }
    if (!pn532_is_connected()) {
        // Attempt lazy connection in case power was just enabled
        if (!pn532_connect()) {
            printf("\r\n%s[-] PN532 RFID module not responding.%s\r\n", ui_term_color_error(), ui_term_color_reset());
            uint32_t rx_count = pn532_get_diag_rx_count();
            uint32_t uart_errors = pn532_get_diag_uart_errors();
            if (rx_count == 0) {
                if (uart_errors & 0x04u) {
                    printf(" %sDiagnostics:%s UART break detected on RX (BIO%u); the line is held LOW.\r\n",
                           ui_term_color_info(), ui_term_color_reset(), (unsigned int)rfid_config.rx_pin);
                    printf(" The PN532 is probably not in HSU mode (an I2C SDA line can remain LOW),\r\n");
                    printf(" or PN532 TX is shorted to ground. Set both selectors to OFF, then power-cycle it.\r\n");
                } else {
                    printf(" %sDiagnostics:%s 0 bytes received on RX pin (BIO%u); line is idle %s.\r\n",
                           ui_term_color_info(), ui_term_color_reset(), (unsigned int)rfid_config.rx_pin,
                           pn532_get_diag_rx_line_high() ? "HIGH" : "LOW");
                }
                if (uart_errors & ~0x04u) {
                    printf(" UART errors:%s%s%s (0x%02X).\r\n",
                           (uart_errors & 0x01u) ? " framing" : "",
                           (uart_errors & 0x02u) ? " parity" : "",
                           (uart_errors & 0x08u) ? " overrun" : "",
                           (unsigned int)uart_errors);
                }
                printf(" 1. %sCheck power:%s VREG is %s. %s\r\n",
                       ui_term_color_prompt(), ui_term_color_reset(),
                       psu_status.enabled ? "enabled" : "OFF",
                       psu_status.enabled ? "" : "Use W 3.3 to power the PN532 module.");
                printf(" 2. %sCheck DIP switches:%s On PN532 board, set %sSEL0 = 0 (OFF), SEL1 = 0 (OFF)%s for HSU mode.\r\n",
                       ui_term_color_prompt(), ui_term_color_reset(), ui_term_color_prompt(), ui_term_color_reset());
                printf(" 3. %sCheck wiring:%s Bus Pirate TX (BIO%u) -> PN532 RX, Bus Pirate RX (BIO%u) -> PN532 TX.\r\n",
                       ui_term_color_prompt(), ui_term_color_reset(), (unsigned int)rfid_config.tx_pin, (unsigned int)rfid_config.rx_pin);
                printf("    Try swapping the TX and RX jumper wires if module silkscreen is reversed.\r\n\r\n");
            } else {
                printf(" %sDiagnostics:%s Received %u bytes on UART, but frame handshake failed.\r\n",
                       ui_term_color_info(), ui_term_color_reset(), (unsigned int)rx_count);
                uint8_t last_bytes[16];
                uint8_t count = sizeof(last_bytes);
                pn532_get_diag_last_bytes(last_bytes, &count);
                printf(" Last bytes: ");
                for (uint8_t i = 0; i < count; i++) printf("%02X ", last_bytes[i]);
                printf("\r\n\r\n");
            }
            return false;
        }
    }
    return true;
}

// -----------------------------------------------------------------------------
// Command: info
// -----------------------------------------------------------------------------
static const char *const info_usage[] = {
    "info",
    "Display controller details and RF modes:%s info",
};
static const bp_command_def_t info_def = {
    .name = "info",
    .description = (uint32_t)(uintptr_t)"Show PN532 firmware version and connection status",
    .usage = info_usage,
    .usage_count = count_of(info_usage),
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
    printf("Interface:         HSU (UART) @ %u baud (TX: BIO%u, RX: BIO%u)\r\n\r\n",
           rfid_config.baudrate, rfid_config.tx_pin, rfid_config.rx_pin);
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

static void print_hex_line(const uint8_t *data, uint8_t length) {
    for (uint8_t i = 0; i < length; i++) printf("%02X%s", data[i], (i + 1 == length) ? "" : " ");
    printf("\r\n");
}

// -----------------------------------------------------------------------------
// Command: scan (multi-protocol equivalent of the target part of nfc-list)
// -----------------------------------------------------------------------------
static const bp_command_opt_t scan_opts[] = {
    { "types", 't', BP_ARG_REQUIRED, "mask", (uint32_t)(uintptr_t)"libnfc type mask (default 783: A, F212, F424, B, Jewel, Barcode)" },
    { "verbose", 'v', BP_ARG_NONE, NULL, (uint32_t)(uintptr_t)"Show the complete PN532 activation response" },
    { 0 }
};
static const char *const scan_usage[] = {
    "scan [-t mask] [-v]",
    "Scan all PN532 passive protocols:%s scan",
    "Scan ISO14443A only:%s scan -t 1",
    "Mask bits: 1=A, 2=FeliCa212, 4=FeliCa424, 8=B, 256=Jewel, 512=Barcode",
};
static const bp_command_def_t scan_def = {
    .name = "scan",
    .description = (uint32_t)(uintptr_t)"Poll ISO14443A/B, FeliCa, and Jewel tags",
    .opts = scan_opts,
    .usage = scan_usage,
    .usage_count = count_of(scan_usage),
};

typedef struct {
    uint16_t mask;
    uint8_t brty;
    const char *name;
    const uint8_t *init;
    uint8_t init_len;
} rfid_poll_type_t;

static const uint8_t felica_poll_any[] = { 0x00, 0xFF, 0xFF, 0x01, 0x00 };
static const uint8_t iso14443b_poll[] = { 0x00 };
static const rfid_poll_type_t poll_types[] = {
    { 1,   PN532_BAUDRATE_106KBPS_ISO14443A, "ISO14443A 106 kbps", NULL, 0 },
    { 2,   PN532_BAUDRATE_212KBPS_FELICA,    "FeliCa 212 kbps", felica_poll_any, sizeof(felica_poll_any) },
    { 4,   PN532_BAUDRATE_424KBPS_FELICA,    "FeliCa 424 kbps", felica_poll_any, sizeof(felica_poll_any) },
    { 8,   PN532_BAUDRATE_106KBPS_ISO14443B, "ISO14443B 106 kbps", iso14443b_poll, sizeof(iso14443b_poll) },
    { 256, PN532_BAUDRATE_106KBPS_JEWEL,     "Jewel / Topaz 106 kbps", NULL, 0 },
};

static void print_poll_result(const rfid_poll_type_t *type, const uint8_t *response,
                              uint8_t response_len, bool verbose) {
    printf("%s[+] %s target found%s\r\n", ui_term_color_prompt(), type->name, ui_term_color_reset());

    if (type->brty == PN532_BAUDRATE_106KBPS_ISO14443A && response_len >= 6) {
        uint8_t uid_len = response[5];
        if (uid_len > response_len - 6) uid_len = response_len - 6;
        printf("    ATQA: %02X%02X  SAK: %02X  UID: ", response[2], response[3], response[4]);
        print_hex_line(&response[6], uid_len);
    } else if ((type->brty == PN532_BAUDRATE_212KBPS_FELICA ||
                type->brty == PN532_BAUDRATE_424KBPS_FELICA) && response_len >= 11) {
        // POL_RES starts with response code 0x01 followed by the 8-byte NFCID2.
        printf("    NFCID2: ");
        print_hex_line(&response[3], 8);
        if (response_len >= 21) {
            printf("    System code: %02X%02X\r\n", response[19], response[20]);
        }
    } else if (type->brty == PN532_BAUDRATE_106KBPS_JEWEL && response_len >= 8) {
        printf("    SENS_RES: %02X%02X  Jewel ID: ", response[2], response[3]);
        print_hex_line(&response[4], 4);
    } else if (type->brty == PN532_BAUDRATE_106KBPS_ISO14443B && response_len >= 13) {
        // PN532 InListPassiveTarget (B): [NbTg][ATQB 12B: PUPI 4 + AppData 4 + ProtInfo 3 + ...]
        // ATQB layout per ISO14443-3B: PUPI(4) ApplicationData(4) ProtocolInfo(3).
        printf("    PUPI (pseudo-UID): ");
        print_hex_line(&response[2], 4);
        printf("    Application Data: ");
        print_hex_line(&response[6], 4);
        printf("    Protocol Info: ");
        print_hex_line(&response[10], 3);
        // Protocol Info byte 1 bit 4..0 = max frame size code: 16 << ((info>>4)&0x0F)... simplified:
        uint8_t max_frame_code = (response_len > 13) ? (response[10] >> 4) & 0x0F : 0;
        printf("    Type-B tag (ISO14443-4 compatible if ProtInfo indicates; use transceive -p b).\r\n");
        (void)max_frame_code;
    } else {
        printf("    Activation data: ");
        print_hex_line(response, response_len);
    }

    if (verbose) {
        printf("    Raw InListPassiveTarget response: ");
        print_hex_line(response, response_len);
    }
}

static void scan_handler(struct command_result *res) {
    if (bp_cmd_help_check(&scan_def, res->help_flag)) return;
    if (!command_ready()) return;

    uint32_t mask = 1 | 2 | 4 | 8 | 256 | 512;
    (void)bp_cmd_get_uint32(&scan_def, 't', &mask);
    bool verbose = bp_cmd_find_flag(&scan_def, 'v');
    bool found = false;

    printf("\r\nScanning NFC/RFID protocols...\r\n");
    for (size_t i = 0; i < count_of(poll_types); i++) {
        if (!(mask & poll_types[i].mask)) continue;

        uint8_t response[64];
        uint8_t response_len = sizeof(response);
        pn532_status_t st = pn532_list_passive_target(poll_types[i].brty,
                                                       poll_types[i].init,
                                                       poll_types[i].init_len,
                                                       response, &response_len, 350);
        if (st == PN532_OK) {
            found = true;
            print_poll_result(&poll_types[i], response, response_len, verbose);
        } else if (verbose) {
            printf("[-] %-26s %s\r\n", poll_types[i].name,
                   st == PN532_ERR_NO_TAG ? "no target" : pn532_status_str(st));
        }
    }

    if (mask & 512) {
        printf("[i] NFC Barcode needs manual ISO14443A-2 bit/parity capture; it is not\r\n"
               "    available through PN532 InListPassiveTarget and is not implemented yet.\r\n");
    }
    if (!found) printf("No supported target found.\r\n");
}

// -----------------------------------------------------------------------------
// Command: transceive (raw tag frames and ISO14443-4 APDUs)
// -----------------------------------------------------------------------------
static const bp_command_opt_t transceive_opts[] = {
    { "protocol", 'p', BP_ARG_REQUIRED, "a|b|f212|f424|jewel", (uint32_t)(uintptr_t)"Protocol used to select the target" },
    { "data", 'd', BP_ARG_REQUIRED, "hex", (uint32_t)(uintptr_t)"Frame or APDU bytes in hexadecimal" },
    { "timeout", 't', BP_ARG_REQUIRED, "ms", (uint32_t)(uintptr_t)"Response timeout in milliseconds (default 1000)" },
    { "thru", 'c', BP_ARG_NONE, NULL, (uint32_t)(uintptr_t)"Use InCommunicateThru instead of target-aware InDataExchange" },
    { 0 }
};
static const char *const transceive_usage[] = {
    "transceive -p a|b|f212|f424|jewel -d hex [-t ms] [-c]",
    "Send an ISO14443-4 SELECT APDU:%s transceive -p a -d 00A4040000",
    "DESFire GetVersion (ISO14443-4):%s transceive -p a -d 9060000000",
    "DESFire SELECT application:%s transceive -p a -d 905A000003112233",
    "Send a FeliCa frame:%s transceive -p f212 -d 0600FFFFFFFFFFFF",
    "Type-B ATTRIB example:%s transceive -p b -d <ATTRIB bytes> -c",
    "Use transparent RF exchange:%s transceive -p a -d 3004 -c",
};
static const bp_command_def_t transceive_def = {
    .name = "transceive",
    .description = (uint32_t)(uintptr_t)"Select a target and exchange raw frames or APDUs",
    .opts = transceive_opts,
    .usage = transceive_usage,
    .usage_count = count_of(transceive_usage),
};

static bool protocol_from_name(const char *name, uint8_t *brty,
                               const uint8_t **init, uint8_t *init_len) {
    *init = NULL;
    *init_len = 0;
    if (!strcmp(name, "a")) *brty = PN532_BAUDRATE_106KBPS_ISO14443A;
    else if (!strcmp(name, "b")) {
        *brty = PN532_BAUDRATE_106KBPS_ISO14443B;
        *init = iso14443b_poll;
        *init_len = sizeof(iso14443b_poll);
    } else if (!strcmp(name, "f212")) {
        *brty = PN532_BAUDRATE_212KBPS_FELICA;
        *init = felica_poll_any;
        *init_len = sizeof(felica_poll_any);
    } else if (!strcmp(name, "f424")) {
        *brty = PN532_BAUDRATE_424KBPS_FELICA;
        *init = felica_poll_any;
        *init_len = sizeof(felica_poll_any);
    } else if (!strcmp(name, "jewel")) *brty = PN532_BAUDRATE_106KBPS_JEWEL;
    else return false;
    return true;
}

static void transceive_handler(struct command_result *res) {
    if (bp_cmd_help_check(&transceive_def, res->help_flag)) return;
    if (!command_ready()) return;

    char protocol[12] = { 0 };
    char data_text[160] = { 0 };
    if (!bp_cmd_get_string(&transceive_def, 'p', protocol, sizeof(protocol)) ||
        !bp_cmd_get_string(&transceive_def, 'd', data_text, sizeof(data_text))) {
        bp_cmd_help_show(&transceive_def);
        return;
    }

    uint8_t brty;
    const uint8_t *init;
    uint8_t init_len;
    if (!protocol_from_name(protocol, &brty, &init, &init_len)) {
        printf("Unknown protocol '%s'. Use a, b, f212, f424, or jewel.\r\n", protocol);
        return;
    }

    uint8_t tx[60];
    size_t tx_len = parse_hex_bytes(data_text, tx, sizeof(tx));
    if (!tx_len) {
        printf("No valid hexadecimal bytes supplied.\r\n");
        return;
    }

    uint32_t timeout = 1000;
    (void)bp_cmd_get_uint32(&transceive_def, 't', &timeout);
    if (timeout < 10) timeout = 10;
    if (timeout > 10000) timeout = 10000;

    uint8_t activation[64];
    uint8_t activation_len = sizeof(activation);
    pn532_status_t st = pn532_list_passive_target(brty, init, init_len,
                                                  activation, &activation_len, timeout);
    if (st != PN532_OK) {
        printf("Target selection failed: %s.\r\n", pn532_status_str(st));
        return;
    }

    printf("TX (%u): ", (unsigned int)tx_len);
    print_hex_line(tx, (uint8_t)tx_len);
    uint8_t rx[64];
    uint8_t rx_len = sizeof(rx);
    if (bp_cmd_find_flag(&transceive_def, 'c')) {
        st = pn532_communicate_thru(tx, (uint8_t)tx_len, rx, &rx_len, timeout);
    } else {
        st = pn532_data_exchange(tx, (uint8_t)tx_len, rx, &rx_len, timeout);
    }

    if (st != PN532_OK) {
        printf("Exchange failed: %s.\r\n", pn532_status_str(st));
        return;
    }
    printf("RX (%u): ", rx_len);
    print_hex_line(rx, rx_len);
}

// -----------------------------------------------------------------------------
// Command: jewel (libnfc-compatible 120-byte JWD backup/restore)
// -----------------------------------------------------------------------------
static const bp_command_opt_t jewel_opts[] = {
    { "read", 'r', BP_ARG_REQUIRED, "dump.jwd", (uint32_t)(uintptr_t)"Read Jewel/Topaz memory to a 120-byte JWD file" },
    { "write", 'w', BP_ARG_REQUIRED, "dump.jwd", (uint32_t)(uintptr_t)"Write a 120-byte JWD file to a Jewel/Topaz tag" },
    { "lock", 'l', BP_ARG_NONE, NULL, (uint32_t)(uintptr_t)"Allow writing lock bytes (irreversible)" },
    { "otp", 'o', BP_ARG_NONE, NULL, (uint32_t)(uintptr_t)"Allow writing one-time-programmable bytes" },
    { 0 }
};
static const char *const jewel_usage[] = {
    "jewel -r dump.jwd | -w dump.jwd [-l] [-o]",
    "Read a libnfc-compatible Jewel dump:%s jewel -r card.jwd",
    "Restore writable data:%s jewel -w card.jwd",
};
static const bp_command_def_t jewel_def = {
    .name = "jewel",
    .description = (uint32_t)(uintptr_t)"Read or write Jewel/Topaz static memory",
    .opts = jewel_opts,
    .usage = jewel_usage,
    .usage_count = count_of(jewel_usage),
};

static pn532_status_t jewel_select(uint8_t id[4]) {
    uint8_t activation[16];
    uint8_t activation_len = sizeof(activation);
    pn532_status_t st = pn532_list_passive_target(PN532_BAUDRATE_106KBPS_JEWEL,
                                                   NULL, 0, activation,
                                                   &activation_len, 1000);
    if (st != PN532_OK) return st;
    if (activation_len < 8) return PN532_ERR_COMM;
    memcpy(id, &activation[4], 4);
    return PN532_OK;
}

static void jewel_handler(struct command_result *res) {
    if (bp_cmd_help_check(&jewel_def, res->help_flag)) return;
    if (!command_ready()) return;

    char file_name[32] = { 0 };
    bool read_action = bp_cmd_get_string(&jewel_def, 'r', file_name, sizeof(file_name));
    bool write_action = bp_cmd_get_string(&jewel_def, 'w', file_name, sizeof(file_name));
    if (read_action == write_action) { bp_cmd_help_show(&jewel_def); return; }

    uint8_t jewel_id[4];
    pn532_status_t st = jewel_select(jewel_id);
    if (st != PN532_OK) { printf("No Jewel/Topaz tag found.\r\n"); return; }
    printf("Jewel ID: "); print_hex_line(jewel_id, sizeof(jewel_id));

    uint8_t image[120];
    if (read_action) {
        printf("Reading 120 bytes: ");
        for (uint8_t address = 0; address < sizeof(image); address++) {
            uint8_t cmd[] = { 0x01, address };
            uint8_t response_len = 1;
            st = pn532_data_exchange(cmd, sizeof(cmd), &image[address], &response_len, 300);
            if (st != PN532_OK || response_len != 1) break;
            if ((address & 7) == 7) printf(".");
        }
        printf("\r\n");
        if (st != PN532_OK) { printf("Jewel read failed: %s.\r\n", pn532_status_str(st)); return; }
        FIL fil;
        if (f_open(&fil, file_name, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) {
            printf("Failed to create '%s'.\r\n", file_name); return;
        }
        UINT written;
        FRESULT fr = f_write(&fil, image, sizeof(image), &written);
        f_close(&fil);
        if (fr == FR_OK && written == sizeof(image)) printf("Saved 120-byte JWD image to '%s'.\r\n", file_name);
        else printf("Failed to save Jewel image.\r\n");
        return;
    }

    FIL fil;
    if (f_open(&fil, file_name, FA_READ) != FR_OK) {
        printf("Failed to open '%s'.\r\n", file_name);
        return;
    }
    if (f_size(&fil) != sizeof(image)) {
        f_close(&fil);
        printf("Jewel restore requires an exact 120-byte JWD file.\r\n");
        return;
    }
    UINT count;
    FRESULT fr = f_read(&fil, image, sizeof(image), &count);
    f_close(&fil);
    if (fr != FR_OK || count != sizeof(image)) { printf("Failed to read '%s'.\r\n", file_name); return; }

    bool write_lock = bp_cmd_find_flag(&jewel_def, 'l');
    bool write_otp = bp_cmd_find_flag(&jewel_def, 'o');
    printf("Writing Jewel data (block 0 and reserved block 13 are always preserved)...\r\n");
    uint8_t write_count = 0;
    for (uint8_t address = 8; address < sizeof(image); address++) {
        uint8_t block = address >> 3;
        if (block == 0x0D) continue;
        if (block == 0x0E) {
            if (address < 114 && !write_lock) continue;
            if (address >= 114 && !write_otp) continue;
        }
        uint8_t cmd[] = { 0x53, address, image[address] };
        uint8_t response[2];
        uint8_t response_len = sizeof(response);
        st = pn532_data_exchange(cmd, sizeof(cmd), response, &response_len, 400);
        if (st != PN532_OK) break;
        write_count++;
    }
    if (st == PN532_OK) printf("Wrote %u Jewel bytes.\r\n", write_count);
    else printf("Jewel write stopped after %u bytes: %s.\r\n", write_count, pn532_status_str(st));
}

// -----------------------------------------------------------------------------
// Command: forum3 (NFC Forum Type 3 / FeliCa NDEF extraction)
// -----------------------------------------------------------------------------
static const bp_command_opt_t forum3_opts[] = {
    { "file", 'f', BP_ARG_REQUIRED, "filename", (uint32_t)(uintptr_t)"Write the raw NDEF message to a file" },
    { 0 }
};
static const char *const forum3_usage[] = {
    "forum3 [-f ndef.bin]",
    "Inspect the Type 3 attribute and print NDEF bytes:%s forum3",
    "Extract the NDEF message:%s forum3 -f message.ndef",
};
static const bp_command_def_t forum3_def = {
    .name = "forum3",
    .description = (uint32_t)(uintptr_t)"Read an NFC Forum Type 3 (FeliCa) NDEF message",
    .opts = forum3_opts,
    .usage = forum3_usage,
    .usage_count = count_of(forum3_usage),
};

static pn532_status_t felica_read_ndef_block(const uint8_t idm[8], uint8_t block,
                                             uint8_t data[16]) {
    // FeliCa CHECK: service 0x000B (little endian), one 2-byte block-list item.
    uint8_t frame[16] = {
        16, 0x06,
        0, 0, 0, 0, 0, 0, 0, 0,
        0x01, 0x0B, 0x00, 0x01, 0x80, block,
    };
    memcpy(&frame[2], idm, 8);

    uint8_t response[40];
    uint8_t response_len = sizeof(response);
    pn532_status_t st = pn532_data_exchange(frame, sizeof(frame), response, &response_len, 1000);
    if (st != PN532_OK) return st;
    if (response_len < 29 || response[0] != response_len || response[1] != 0x07 ||
        memcmp(&response[2], idm, 8) || response[10] || response[11] || response[12] != 1) {
        return PN532_ERR_COMM;
    }
    memcpy(data, &response[13], 16);
    return PN532_OK;
}

static void forum3_handler(struct command_result *res) {
    if (bp_cmd_help_check(&forum3_def, res->help_flag)) return;
    if (!command_ready()) return;

    char file_name[32] = { 0 };
    bool save_file = bp_cmd_get_string(&forum3_def, 'f', file_name, sizeof(file_name));
    static const uint8_t forum3_poll[] = { 0x00, 0x12, 0xFC, 0x01, 0x00 };
    uint8_t activation[32];
    uint8_t activation_len = sizeof(activation);
    pn532_status_t st = pn532_list_passive_target(PN532_BAUDRATE_212KBPS_FELICA,
                                                   forum3_poll, sizeof(forum3_poll),
                                                   activation, &activation_len, 1000);
    if (st != PN532_OK || activation_len < 11 || activation[2] != 0x01) {
        printf("No NFC Forum Type 3 / FeliCa target found.\r\n");
        return;
    }
    uint8_t idm[8];
    memcpy(idm, &activation[3], sizeof(idm));
    printf("NFCID2: ");
    print_hex_line(idm, sizeof(idm));

    uint8_t attr[16];
    st = felica_read_ndef_block(idm, 0, attr);
    if (st != PN532_OK) {
        printf("Failed to read the Type 3 NDEF attribute block: %s.\r\n", pn532_status_str(st));
        return;
    }

    uint16_t calculated_checksum = 0;
    for (uint8_t i = 0; i < 14; i++) calculated_checksum += attr[i];
    uint16_t stored_checksum = ((uint16_t)attr[14] << 8) | attr[15];
    uint16_t max_blocks = ((uint16_t)attr[3] << 8) | attr[4];
    uint32_t ndef_len = ((uint32_t)attr[11] << 16) | ((uint32_t)attr[12] << 8) | attr[13];

    printf("Mapping: %u.%u, Nbr: %u, Nbw: %u, capacity: %u bytes\r\n",
           attr[0] >> 4, attr[0] & 0x0F, attr[1], attr[2], max_blocks * 16u);
    printf("Access: %s, write state: %s, NDEF length: %lu bytes\r\n",
           attr[10] == 1 ? "read/write" : "read-only",
           attr[9] == 0 ? "finished" : "in progress",
           (unsigned long)ndef_len);
    if (calculated_checksum != stored_checksum || ndef_len > (uint32_t)max_blocks * 16u) {
        printf("Invalid Type 3 attribute block (checksum/capacity mismatch).\r\n");
        return;
    }
    if (!ndef_len) {
        printf("The Type 3 tag contains an empty NDEF message.\r\n");
        return;
    }

    FIL fil;
    if (save_file && f_open(&fil, file_name, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) {
        printf("Failed to create '%s'.\r\n", file_name);
        return;
    }

    uint32_t remaining = ndef_len;
    uint16_t block = 1;
    while (remaining) {
        if (block > 255) { st = PN532_ERR_UNSUPPORTED; break; }
        uint8_t data[16];
        st = felica_read_ndef_block(idm, (uint8_t)block, data);
        if (st != PN532_OK) break;
        uint8_t chunk = remaining > sizeof(data) ? sizeof(data) : (uint8_t)remaining;
        if (save_file) {
            UINT written;
            if (f_write(&fil, data, chunk, &written) != FR_OK || written != chunk) {
                st = PN532_ERR_WRITE;
                break;
            }
        } else {
            printf("Block %03u: ", block);
            print_hex_line(data, chunk);
        }
        remaining -= chunk;
        block++;
    }
    if (save_file) f_close(&fil);
    if (st == PN532_OK && remaining == 0) {
        if (save_file) printf("Extracted %lu NDEF bytes to '%s'.\r\n", (unsigned long)ndef_len, file_name);
    } else {
        printf("Type 3 NDEF read stopped after %lu bytes: %s.\r\n",
               (unsigned long)(ndef_len - remaining), pn532_status_str(st));
    }
}

// -----------------------------------------------------------------------------
// Command: forum4 (NFC Forum Type 4 NDEF tag emulation)
// -----------------------------------------------------------------------------
#define FORUM4_NDEF_MAX 510
static const bp_command_opt_t forum4_opts[] = {
    { "input", 'f', BP_ARG_REQUIRED, "ndef-file", (uint32_t)(uintptr_t)"Raw NDEF message to emulate (maximum 510 bytes)" },
    { "output", 'o', BP_ARG_REQUIRED, "ndef-file", (uint32_t)(uintptr_t)"Save NDEF content written by the reader" },
    { "v1", '1', BP_ARG_NONE, NULL, (uint32_t)(uintptr_t)"Emulate NFC Forum Type 4 Tag v1.0 (default v2.0)" },
    { 0 }
};
static const char *const forum4_usage[] = {
    "forum4 [-f input.ndef] [-o written.ndef]",
    "Emulate the built-in Bus Pirate NDEF tag:%s forum4",
    "Emulate an NDEF file and save reader updates:%s forum4 -f tag.ndef -o updated.ndef",
};
static const bp_command_def_t forum4_def = {
    .name = "forum4",
    .description = (uint32_t)(uintptr_t)"Emulate an NFC Forum Type 4 NDEF tag",
    .opts = forum4_opts,
    .usage = forum4_usage,
    .usage_count = count_of(forum4_usage),
};

typedef enum { FORUM4_FILE_NONE, FORUM4_FILE_CC, FORUM4_FILE_NDEF } forum4_file_t;

static uint8_t forum4_apdu(const uint8_t *apdu, uint8_t apdu_len, forum4_file_t *selected,
                           uint8_t *ndef_file, uint16_t ndef_capacity, bool v1,
                           uint8_t *response) {
    const uint8_t ndef_aid[] = { 0xD2, 0x76, 0x00, 0x00, 0x85, 0x01, v1 ? 0x00 : 0x01 };
    const uint8_t cc_file[] = {
        0x00, 0x0F, v1 ? 0x10 : 0x20, 0x00, 0x3B, 0x00, 0x34,
        0x04, 0x06, 0xE1, 0x04, 0x02, 0x00, 0x00, 0x00,
    };
    if (apdu_len < 4) { response[0] = 0x67; response[1] = 0x00; return 2; }

    // SELECT by NFC Forum NDEF application AID.
    if (apdu[1] == 0xA4 && apdu[2] == 0x04 && apdu_len >= 12 && apdu[4] == sizeof(ndef_aid) &&
        !memcmp(&apdu[5], ndef_aid, sizeof(ndef_aid))) {
        *selected = FORUM4_FILE_NONE;
        response[0] = 0x90; response[1] = 0x00; return 2;
    }
    // SELECT CC (E103) or NDEF (E104) file.
    if (apdu[1] == 0xA4 && apdu_len >= 7 && apdu[4] == 2 && apdu[5] == 0xE1) {
        if (apdu[6] == 0x03) *selected = FORUM4_FILE_CC;
        else if (apdu[6] == 0x04) *selected = FORUM4_FILE_NDEF;
        else { response[0] = 0x6A; response[1] = 0x82; return 2; }
        response[0] = 0x90; response[1] = 0x00; return 2;
    }
    // READ BINARY. Limit one response to the PN532's normal-frame payload.
    if (apdu[1] == 0xB0 && apdu_len >= 5) {
        uint16_t offset = ((uint16_t)apdu[2] << 8) | apdu[3];
        uint8_t requested = apdu[4] ? apdu[4] : 0xFF;
        const uint8_t *source = NULL;
        uint16_t source_len = 0;
        if (*selected == FORUM4_FILE_CC) { source = cc_file; source_len = sizeof(cc_file); }
        else if (*selected == FORUM4_FILE_NDEF) { source = ndef_file; source_len = ndef_capacity; }
        else { response[0] = 0x69; response[1] = 0x86; return 2; }
        if (offset >= source_len) { response[0] = 0x6B; response[1] = 0x00; return 2; }
        uint16_t available = source_len - offset;
        uint8_t count = requested > 60 ? 60 : requested;
        if (count > available) count = (uint8_t)available;
        memcpy(response, &source[offset], count);
        response[count] = 0x90; response[count + 1] = 0x00;
        return (uint8_t)(count + 2);
    }
    // UPDATE BINARY for the selected NDEF file.
    if (apdu[1] == 0xD6 && apdu_len >= 5) {
        uint16_t offset = ((uint16_t)apdu[2] << 8) | apdu[3];
        uint8_t count = apdu[4];
        if (*selected != FORUM4_FILE_NDEF) { response[0] = 0x69; response[1] = 0x86; return 2; }
        if ((uint16_t)(5 + count) > apdu_len) { response[0] = 0x67; response[1] = 0x00; return 2; }
        if ((uint32_t)offset + count > ndef_capacity) { response[0] = 0x6A; response[1] = 0x84; return 2; }
        memcpy(&ndef_file[offset], &apdu[5], count);
        response[0] = 0x90; response[1] = 0x00; return 2;
    }
    response[0] = 0x6D; response[1] = 0x00;
    return 2;
}

static void forum4_handler(struct command_result *res) {
    if (bp_cmd_help_check(&forum4_def, res->help_flag)) return;
    if (!command_ready()) return;

    char input_name[32] = { 0 };
    char output_name[32] = { 0 };
    bool has_input = bp_cmd_get_string(&forum4_def, 'f', input_name, sizeof(input_name));
    bool has_output = bp_cmd_get_string(&forum4_def, 'o', output_name, sizeof(output_name));
    bool v1 = bp_cmd_find_flag(&forum4_def, '1');
    uint8_t ndef_file[FORUM4_NDEF_MAX + 2];
    memset(ndef_file, 0, sizeof(ndef_file));

    if (has_input) {
        FIL fil;
        if (f_open(&fil, input_name, FA_READ) != FR_OK) {
            printf("Failed to open '%s'.\r\n", input_name);
            return;
        }
        if (f_size(&fil) > FORUM4_NDEF_MAX) {
            f_close(&fil);
            printf("Input must be a raw NDEF file of at most %u bytes.\r\n", FORUM4_NDEF_MAX);
            return;
        }
        uint16_t input_len = (uint16_t)f_size(&fil);
        UINT count;
        FRESULT fr = f_read(&fil, &ndef_file[2], input_len, &count);
        f_close(&fil);
        if (fr != FR_OK || count != input_len) { printf("Failed to read '%s'.\r\n", input_name); return; }
        ndef_file[0] = (uint8_t)(input_len >> 8);
        ndef_file[1] = (uint8_t)input_len;
    } else {
        static const uint8_t default_ndef[] = {
            0xD1, 0x01, 0x10, 0x54, 0x02, 0x65, 0x6E,
            'B', 'u', 's', ' ', 'P', 'i', 'r', 'a', 't', 'e', ' ', 'N', 'F', 'C'
        };
        ndef_file[0] = 0;
        ndef_file[1] = sizeof(default_ndef);
        memcpy(&ndef_file[2], default_ndef, sizeof(default_ndef));
    }

    // Mode 0x05: passive-only and ISO14443-4 PICC-only. NFCID1 byte 0 is fixed
    // by PN532; the host supplies the remaining three bytes.
    static const uint8_t target_params[] = {
        0x05,
        0x04, 0x00, 0x12, 0x34, 0x56, 0x20,
        0x01, 0xFE, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7,
        0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xFF, 0xFF,
        0xAA, 0x99, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11,
        0x00, 0x00,
    };
    printf("Waiting for an NFC reader/phone... Press any key to stop.\r\n");
    uint8_t activation[16];
    uint8_t activation_len = sizeof(activation);
    pn532_status_t st = pn532_target_init(target_params, sizeof(target_params),
                                          activation, &activation_len, 30000);
    if (st != PN532_OK) { printf("Target activation failed: %s.\r\n", pn532_status_str(st)); return; }
    printf("Reader activated the PN532 Type 4 target.\r\n");

    forum4_file_t selected = FORUM4_FILE_NONE;
    while (true) {
        char c;
        if (rx_fifo_try_get(&c)) { printf("Emulation stopped.\r\n"); break; }
        uint8_t apdu[62];
        uint8_t apdu_len = sizeof(apdu);
        st = pn532_target_get_data(apdu, &apdu_len, 2000);
        if (st != PN532_OK) { printf("Reader disconnected.\r\n"); break; }
        printf("C-APDU: "); print_hex_line(apdu, apdu_len);
        uint8_t response[62];
        uint8_t response_len = forum4_apdu(apdu, apdu_len, &selected,
                                           ndef_file, sizeof(ndef_file), v1, response);
        printf("R-APDU: "); print_hex_line(response, response_len);
        st = pn532_target_set_data(response, response_len, 2000);
        if (st != PN532_OK) { printf("Failed to return APDU response.\r\n"); break; }
    }

    if (has_output) {
        uint16_t ndef_len = ((uint16_t)ndef_file[0] << 8) | ndef_file[1];
        if (ndef_len > FORUM4_NDEF_MAX) ndef_len = FORUM4_NDEF_MAX;
        FIL fil;
        if (f_open(&fil, output_name, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK) {
            UINT written;
            FRESULT fr = f_write(&fil, &ndef_file[2], ndef_len, &written);
            f_close(&fil);
            if (fr == FR_OK && written == ndef_len) printf("Saved %u NDEF bytes to '%s'.\r\n", ndef_len, output_name);
            else printf("Failed to save updated NDEF data.\r\n");
        } else printf("Failed to create '%s'.\r\n", output_name);
    }
}

// -----------------------------------------------------------------------------
// Command: tools (capability map for users familiar with libnfc utilities)
// -----------------------------------------------------------------------------
static const char *const tools_usage[] = {
    "tools",
    "Display libnfc workflow equivalents:%s tools",
};
static const bp_command_def_t tools_def = {
    .name = "tools",
    .description = (uint32_t)(uintptr_t)"Show libnfc utility equivalents and hardware limits",
    .usage = tools_usage,
    .usage_count = count_of(tools_usage),
};

static void tools_handler(struct command_result *res) {
    if (bp_cmd_help_check(&tools_def, res->help_flag)) return;
    printf("\r\nlibnfc-style workflow                 Bus Pirate RFID command\r\n");
    printf("nfc-list / target scan                scan [-t mask] [-v]\r\n");
    printf("nfc-scan-device                       info (one attached PN532)\r\n");
    printf("nfc-mfclassic                         dump -b / restore / write [-k key] [-B] / clone\r\n");
    printf("nfc-mfultralight                      dump -b / restore / write / ndef / pwauth\r\n");
    printf("NDEF Type 2 read/write                ndef [-u URI | -m text]\r\n");
    printf("NFC Forum Type 3 NDEF                forum3 [-f message.ndef]\r\n");
    printf("nfc-emulate-forum-tag4               forum4 [-f in.ndef] [-o out.ndef]\r\n");
    printf("nfc-jewel                             jewel -r/-w dump.jwd\r\n");
    printf("FeliCa and Type 4 APDU access         scan + transceive\r\n");
    printf("DESFire GetVersion example            transceive -p a -d 9060000000\r\n");
    printf("UID copy to Gen2 magic card           clone / write -u (Gen1A backdoor: see clone help)\r\n");
    printf("NTAG password (--pw)                  pwauth -p <4B hex> + GET_VERSION\r\n");
    printf("Bulk UID inventory (Tag Scanner)      scanner [-f inventory.csv] [-n num]\r\n");
    printf("Nested-auth nonces (mfkey32/64)       nonce -b block [-B] [-n num] [-f nonces.csv]\r\n");
    printf("\r\nNot standalone on one PN532:\r\n");
    printf("  PICC relay needs two NFC radios (reader-side target + tag-side initiator).\r\n");
    printf("  NFC Barcode needs a manual bit/parity capture path not implemented yet.\r\n");
}

// -----------------------------------------------------------------------------
// Command: read
// -----------------------------------------------------------------------------
static const bp_command_opt_t read_opts[] = {
    { "single", 's', BP_ARG_NONE, NULL, (uint32_t)(uintptr_t)"Single tag read then exit" },
    { 0 }
};
static const char *const read_usage[] = {
    "read [-s(ingle)]",
    "Continuous read mode:%s read",
    "Single tag read:%s read -s",
};
static const bp_command_def_t read_def = {
    .name = "read",
    .description = (uint32_t)(uintptr_t)"Scan and display RFID tag UID, type, and ATQA/SAK",
    .opts = read_opts,
    .usage = read_usage,
    .usage_count = count_of(read_usage),
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
                    uint8_t key_type = MIFARE_CMD_AUTH_A;
                    if (pn532_mifare_auth_auto_type(0, tag.uid, tag.uid_len, key, &key_type) == PN532_OK) {
                        uint8_t blk0[16];
                        if (pn532_mifare_read_block(0, blk0) == PN532_OK) {
                            printf("  Block 00 (Key %c): ",
                                   (key_type == MIFARE_CMD_AUTH_B) ? 'B' : 'A');
                            for (int i = 0; i < 16; i++) printf("%02X ", blk0[i]);
                            printf("\r\n");
                        }
                    } else {
                        printf("  (Dictionary Keys A+B failed to authenticate Sector 0)\r\n");
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
// Command: scanner (bulk Tag Scanner: collect many UIDs fast, Killer-style)
// -----------------------------------------------------------------------------
static const bp_command_opt_t scanner_opts[] = {
    { "file", 'f', BP_ARG_REQUIRED, "filename", (uint32_t)(uintptr_t)"Save UID inventory as CSV to TF-card" },
    { "count", 'n', BP_ARG_REQUIRED, "num", (uint32_t)(uintptr_t)"Stop after N unique tags (default: run until keypress)" },
    { 0 }
};
static const char *const scanner_usage[] = {
    "scanner [-f inventory.csv] [-n num]",
    "Collect UIDs until any key:%s scanner",
    "Save 20 unique tags to CSV:%s scanner -f tags.csv -n 20",
};
static const bp_command_def_t scanner_def = {
    .name = "scanner",
    .description = (uint32_t)(uintptr_t)"Bulk tag inventory: log every unique UID with ATQA/SAK",
    .opts = scanner_opts,
    .usage = scanner_usage,
    .usage_count = count_of(scanner_usage),
};

#define SCANNER_MAX_TAGS 64

static void scanner_handler(struct command_result *res) {
    if (bp_cmd_help_check(&scanner_def, res->help_flag)) return;
    if (!command_ready()) return;

    char file_name[32] = { 0 };
    bool save_file = bp_cmd_get_string(&scanner_def, 'f', file_name, sizeof(file_name));
    uint32_t max_unique = 0;
    (void)bp_cmd_get_uint32(&scanner_def, 'n', &max_unique);

    FIL fil;
    bool fil_open = false;
    if (save_file) {
        if (f_open(&fil, file_name, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) {
            printf("Failed to create '%s'.\r\n", file_name);
            return;
        }
        fil_open = true;
        f_printf(&fil, "index,uid,uid_len,atqa,sak,type\r\n");
    }

    printf("\r\nTag Scanner: present tags one by one. Press any key to stop.\r\n");
    uint8_t seen_uid[SCANNER_MAX_TAGS][10];
    uint8_t seen_len[SCANNER_MAX_TAGS] = { 0 };
    uint32_t unique = 0;
    uint32_t polls = 0;

    while (true) {
        char c;
        if (rx_fifo_try_get(&c)) { printf("Stopped by user.\r\n"); break; }
        if (max_unique && unique >= max_unique) break;

        pn532_tag_info_t tag;
        if (pn532_detect_tag(&tag, 120) != PN532_OK) { sleep_ms(30); polls++; continue; }
        polls++;

        bool known = false;
        for (uint32_t i = 0; i < unique && i < SCANNER_MAX_TAGS; i++) {
            if (seen_len[i] == tag.uid_len && !memcmp(seen_uid[i], tag.uid, tag.uid_len)) { known = true; break; }
        }
        if (known) { sleep_ms(30); continue; }

        uint32_t now = to_ms_since_boot(get_absolute_time());
        printf("[%lu] #%u UID: ", (unsigned long)now, (unsigned int)(unique + 1));
        for (uint8_t i = 0; i < tag.uid_len; i++) printf("%02X%s", tag.uid[i], (i + 1 == tag.uid_len) ? "" : ":");
        printf(" ATQA: %04X SAK: %02X (%s)\r\n", tag.atqa, tag.sak, tag.type_name);

        if (fil_open) {
            f_printf(&fil, "%u,", (unsigned int)(unique + 1));
            for (uint8_t i = 0; i < tag.uid_len; i++) f_printf(&fil, "%02X%s", tag.uid[i], (i + 1 == tag.uid_len) ? "" : ":");
            f_printf(&fil, ",%u,%04X,%02X,%s\r\n", tag.uid_len, tag.atqa, tag.sak, tag.type_name);
        }
        if (unique < SCANNER_MAX_TAGS) {
            memcpy(seen_uid[unique], tag.uid, tag.uid_len);
            seen_len[unique] = tag.uid_len;
        }
        unique++;
        sleep_ms(30);
    }
    if (fil_open) {
        f_close(&fil);
        printf("Saved %u unique tag(s) to '%s'.\r\n", (unsigned int)unique, file_name);
    } else {
        printf("Scanner done: %u unique tag(s) in %u poll(s).\r\n", (unsigned int)unique, (unsigned int)polls);
    }
}

// -----------------------------------------------------------------------------
// Command: dump
// -----------------------------------------------------------------------------
static const bp_command_opt_t dump_opts[] = {
    { "file", 'f', BP_ARG_REQUIRED, "filename", (uint32_t)(uintptr_t)"Save dump to TF-card file" },
    { "binary", 'b', BP_ARG_REQUIRED, "filename", (uint32_t)(uintptr_t)"Save raw binary memory image for restore" },
    { "key", 'k', BP_ARG_REQUIRED, "hex", (uint32_t)(uintptr_t)"Custom 6-byte key first (12 hex digits)" },
    { "key-b", 'B', BP_ARG_NONE, NULL, (uint32_t)(uintptr_t)"Custom/dictionary key is Key B (default Key A)" },
    { 0 }
};
static const char *const dump_usage[] = {
    "dump [-f text-file | -b binary-file] [-k key] [-B]",
    "Dump memory to terminal:%s dump",
    "Dump memory to TF-card:%s dump -f card.txt",
    "Save a restorable binary image:%s dump -b card.bin",
    "Use custom Key B first:%s dump -k A0A1A2A3A4A5 -B",
};
static const bp_command_def_t dump_def = {
    .name = "dump",
    .description = (uint32_t)(uintptr_t)"Dump entire tag memory contents (MIFARE Classic / Ultralight / NTAG)",
    .opts = dump_opts,
    .usage = dump_usage,
    .usage_count = count_of(dump_usage),
};

static void dump_handler(struct command_result *res) {
    if (bp_cmd_help_check(&dump_def, res->help_flag)) return;
    if (!command_ready()) return;

    char file_name[32] = { 0 };
    bool save_file = bp_cmd_get_string(&dump_def, 'f', file_name, sizeof(file_name));
    bool binary_file = bp_cmd_get_string(&dump_def, 'b', file_name, sizeof(file_name));
    if (save_file && binary_file) {
        printf("Choose either a text dump (-f) or binary dump (-b), not both.\r\n");
        return;
    }
    save_file |= binary_file;

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
    if (fil_open && !binary_file) {
        f_printf(&fil, "# RFID/NFC Tag Dump\r\nUID: ");
        for (uint8_t i = 0; i < tag.uid_len; i++) f_printf(&fil, "%02X ", tag.uid[i]);
        f_printf(&fil, "\r\nATQA: 0x%04X\r\nSAK: 0x%02X\r\nType: %s\r\n\r\n", tag.atqa, tag.sak, tag.type_name);
    }

    if (tag.is_mifare_classic) {
        uint8_t sectors = (tag.sak == 0x18) ? 40 : ((tag.sak == 0x09) ? 5 : 16);
        printf("\r\nReading MIFARE Classic (%u sectors)...\r\n\r\n", sectors);

        uint8_t custom_key[6] = { 0 };
        uint8_t custom_type = MIFARE_CMD_AUTH_A;
        bool has_custom = rfid_parse_custom_key(&dump_def, custom_key, &custom_type);
        if (!has_custom) {
            char key_probe[32] = { 0 };
            if (bp_cmd_get_string(&dump_def, 'k', key_probe, sizeof(key_probe))) {
                if (fil_open) f_close(&fil);
                return; // invalid key message already printed
            }
        }

        for (uint8_t s = 0; s < sectors; s++) {
            uint8_t first_block = (s < 32) ? (s * 4) : (128 + (s - 32) * 16);
            uint8_t num_blocks = (s < 32) ? 4 : 16;
            uint8_t key[6];
            uint8_t key_type = MIFARE_CMD_AUTH_A;

            if (rfid_auth_sector(first_block, tag.uid, tag.uid_len,
                                 custom_key, has_custom, custom_type, key, &key_type) != PN532_OK) {
                printf("Sector %02u: [Authentication Failed]\r\n", s);
                if (fil_open && !binary_file) f_printf(&fil, "Sector %02u: [Auth Failed]\r\n", s);
                if (fil_open && binary_file) {
                    uint8_t unread[16];
                    memset(unread, 0xFF, sizeof(unread));
                    for (uint8_t b = 0; b < num_blocks; b++) {
                        UINT written;
                        (void)f_write(&fil, unread, sizeof(unread), &written);
                    }
                }
                continue;
            }

            printf("Sector %02u (Key %c: %02X%02X%02X%02X%02X%02X):\r\n",
                   s, (key_type == MIFARE_CMD_AUTH_B) ? 'B' : 'A',
                   key[0], key[1], key[2], key[3], key[4], key[5]);
            if (fil_open && !binary_file) {
                f_printf(&fil, "Sector %02u (Key %c: %02X%02X%02X%02X%02X%02X):\r\n",
                         s, (key_type == MIFARE_CMD_AUTH_B) ? 'B' : 'A',
                         key[0], key[1], key[2], key[3], key[4], key[5]);
            }

            for (uint8_t b = 0; b < num_blocks; b++) {
                uint8_t blk = first_block + b;
                uint8_t blk_data[16];
                if (pn532_mifare_read_block(blk, blk_data) == PN532_OK) {
                    if (fil_open && binary_file) {
                        UINT written;
                        if (f_write(&fil, blk_data, sizeof(blk_data), &written) != FR_OK || written != sizeof(blk_data)) {
                            printf("Binary file write failed.\r\n");
                            f_close(&fil);
                            return;
                        }
                    }
                    printf("  Block %03u: ", blk);
                    if (fil_open && !binary_file) f_printf(&fil, "  Block %03u: ", blk);
                    for (int i = 0; i < 16; i++) {
                        printf("%02X ", blk_data[i]);
                        if (fil_open && !binary_file) f_printf(&fil, "%02X ", blk_data[i]);
                    }
                    printf(" |");
                    if (fil_open && !binary_file) f_printf(&fil, " |");
                    for (int i = 0; i < 16; i++) {
                        char ch = (blk_data[i] >= 32 && blk_data[i] <= 126) ? (char)blk_data[i] : '.';
                        printf("%c", ch);
                        if (fil_open && !binary_file) f_printf(&fil, "%c", ch);
                    }
                    printf("|\r\n");
                    if (fil_open && !binary_file) f_printf(&fil, "|\r\n");
                } else if (fil_open && binary_file) {
                    // Preserve block offsets in the binary image when a read fails.
                    memset(blk_data, 0xFF, sizeof(blk_data));
                    UINT written;
                    (void)f_write(&fil, blk_data, sizeof(blk_data), &written);
                }
            }
        }
    } else if (tag.is_ultralight_or_ntag) {
        uint16_t pages = tag.total_pages ? tag.total_pages : 16;
        printf("\r\nReading MIFARE Ultralight / NTAG (%u pages)...\r\n\r\n", pages);

        for (uint16_t p = 0; p < pages; p++) {
            uint8_t page_data[4];
            if (pn532_ultralight_read_page((uint8_t)p, page_data) == PN532_OK) {
                if (fil_open && binary_file) {
                    UINT written;
                    if (f_write(&fil, page_data, sizeof(page_data), &written) != FR_OK || written != sizeof(page_data)) {
                        printf("Binary file write failed.\r\n");
                        f_close(&fil);
                        return;
                    }
                }
                printf("Page %03u: %02X %02X %02X %02X  |", p,
                       page_data[0], page_data[1], page_data[2], page_data[3]);
                if (fil_open && !binary_file) {
                    f_printf(&fil, "Page %03u: %02X %02X %02X %02X  |", p,
                             page_data[0], page_data[1], page_data[2], page_data[3]);
                }
                for (int i = 0; i < 4; i++) {
                    char ch = (page_data[i] >= 32 && page_data[i] <= 126) ? (char)page_data[i] : '.';
                    printf("%c", ch);
                    if (fil_open && !binary_file) f_printf(&fil, "%c", ch);
                }
                printf("|\r\n");
                if (fil_open && !binary_file) f_printf(&fil, "|\r\n");
            } else {
                printf("Page %03u: [Read Error]\r\n", p);
                if (fil_open && !binary_file) f_printf(&fil, "Page %03u: [Read Error]\r\n", p);
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
// Command: restore (write a raw image produced by dump -b)
// -----------------------------------------------------------------------------
static const bp_command_opt_t restore_opts[] = {
    { "file", 'f', BP_ARG_REQUIRED, "filename", (uint32_t)(uintptr_t)"Raw binary image created by dump -b" },
    { "manufacturer", 'm', BP_ARG_NONE, NULL, (uint32_t)(uintptr_t)"Also write block/page 0 (magic tags only; dangerous)" },
    { "key", 'k', BP_ARG_REQUIRED, "hex", (uint32_t)(uintptr_t)"Custom 6-byte Classic key first (12 hex digits)" },
    { "key-b", 'B', BP_ARG_NONE, NULL, (uint32_t)(uintptr_t)"Custom/dictionary key is Key B (default Key A)" },
    { 0 }
};
static const char *const restore_usage[] = {
    "restore -f image.bin [-m] [-k key] [-B]",
    "Restore writable data while preserving UID:%s restore -f card.bin",
    "Restore block/page 0 on a compatible magic tag:%s restore -f card.bin -m",
    "Restore with custom Key B:%s restore -f card.bin -k A0A1A2A3A4A5 -B",
};
static const bp_command_def_t restore_def = {
    .name = "restore",
    .description = (uint32_t)(uintptr_t)"Restore a binary MIFARE Classic or Ultralight/NTAG image",
    .opts = restore_opts,
    .usage = restore_usage,
    .usage_count = count_of(restore_usage),
};

static void restore_handler(struct command_result *res) {
    if (bp_cmd_help_check(&restore_def, res->help_flag)) return;
    if (!command_ready()) return;

    char file_name[32] = { 0 };
    if (!bp_cmd_get_string(&restore_def, 'f', file_name, sizeof(file_name))) {
        bp_cmd_help_show(&restore_def);
        return;
    }
    bool write_manufacturer = bp_cmd_find_flag(&restore_def, 'm');

    FIL fil;
    if (f_open(&fil, file_name, FA_READ) != FR_OK) {
        printf("Failed to open '%s'.\r\n", file_name);
        return;
    }

    printf("Place destination tag on reader... Press any key to cancel.\r\n");
    pn532_tag_info_t tag;
    while (true) {
        char c;
        if (rx_fifo_try_get(&c)) { printf("Restore cancelled.\r\n"); f_close(&fil); return; }
        if (pn532_detect_tag(&tag, 150) == PN532_OK) break;
        sleep_ms(50);
    }
    print_tag_header(&tag);

    uint32_t expected_size;
    if (tag.is_mifare_classic) expected_size = (uint32_t)tag.total_pages * 16u;
    else if (tag.is_ultralight_or_ntag) expected_size = (uint32_t)tag.total_pages * 4u;
    else {
        printf("Restore is not supported for %s.\r\n", tag.type_name);
        f_close(&fil);
        return;
    }
    if (f_size(&fil) != expected_size) {
        printf("Image size is %lu bytes; this tag requires exactly %lu bytes.\r\n",
               (unsigned long)f_size(&fil), (unsigned long)expected_size);
        f_close(&fil);
        return;
    }

    printf("Writing %lu-byte image%s...\r\n", (unsigned long)expected_size,
           write_manufacturer ? " including manufacturer data" : " (preserving manufacturer data)");
    if (tag.is_mifare_classic) {
        printf("NOTE: sector trailers (keys/access bits) will be overwritten. Use a magic/clone card.\r\n");
    }

    uint8_t custom_key[6] = { 0 };
    uint8_t custom_type = MIFARE_CMD_AUTH_A;
    bool has_custom = rfid_parse_custom_key(&restore_def, custom_key, &custom_type);
    if (!has_custom) {
        char key_probe[32] = { 0 };
        if (bp_cmd_get_string(&restore_def, 'k', key_probe, sizeof(key_probe))) {
            f_close(&fil);
            return; // invalid key message already printed
        }
    }

    uint32_t written_units = 0;
    if (tag.is_mifare_classic) {
        uint16_t blocks = tag.total_pages;
        uint8_t sector_key[6];
        uint8_t sector_type = MIFARE_CMD_AUTH_A;
        for (uint16_t block = 0; block < blocks; block++) {
            uint8_t data[16];
            UINT count;
            if (f_read(&fil, data, sizeof(data), &count) != FR_OK || count != sizeof(data)) break;

            uint8_t sector = (block < 128) ? (uint8_t)(block / 4) : (uint8_t)(32 + ((block - 128) / 16));
            uint8_t first_block = (sector < 32) ? (uint8_t)(sector * 4) : (uint8_t)(128 + (sector - 32) * 16);
            if (block == first_block &&
                rfid_auth_sector((uint8_t)block, tag.uid, tag.uid_len, custom_key, has_custom,
                                 custom_type, sector_key, &sector_type) != PN532_OK) {
                printf("Authentication failed at sector %u (Key %c tried); restore stopped.\r\n",
                       sector, has_custom ? ((custom_type == MIFARE_CMD_AUTH_B) ? 'B' : 'A') : 'A');
                break;
            }
            if (block == 0 && !write_manufacturer) continue;
            if (pn532_mifare_write_block((uint8_t)block, data) != PN532_OK) {
                printf("Write failed at block %u; restore stopped.\r\n", block);
                break;
            }
            written_units++;
        }
    } else {
        for (uint16_t page = 0; page < tag.total_pages; page++) {
            uint8_t data[4];
            UINT count;
            if (f_read(&fil, data, sizeof(data), &count) != FR_OK || count != sizeof(data)) break;
            if (page < 4 && !write_manufacturer) continue;
            if (!write_manufacturer && page >= (uint16_t)(4 + tag.data_pages)) continue;
            if (pn532_ultralight_write_page((uint8_t)page, data) != PN532_OK) {
                printf("Write failed at page %u; restore stopped.\r\n", page);
                break;
            }
            written_units++;
        }
    }
    f_close(&fil);
    printf("Restore wrote %lu %s. Verify with 'dump -b verify.bin'.\r\n",
           (unsigned long)written_units, tag.is_mifare_classic ? "blocks" : "pages");
}

// -----------------------------------------------------------------------------
// Command: write
// -----------------------------------------------------------------------------
static const bp_command_opt_t write_opts[] = {
    { "block", 'b', BP_ARG_REQUIRED, "index", (uint32_t)(uintptr_t)"Block or page index to write" },
    { "data",  'd', BP_ARG_REQUIRED, "hex",   (uint32_t)(uintptr_t)"Hex data bytes (4B for Ultralight, 16B for Classic)" },
    { "uid",   'u', BP_ARG_REQUIRED, "hex",   (uint32_t)(uintptr_t)"Magic UID hex (4 bytes, e.g. 12345678)" },
    { "key",   'k', BP_ARG_REQUIRED, "hex",   (uint32_t)(uintptr_t)"Custom 6-byte Classic key (12 hex digits)" },
    { "key-b", 'B', BP_ARG_NONE, NULL,        (uint32_t)(uintptr_t)"Custom/dictionary key is Key B (default Key A)" },
    { 0 }
};
static const char *const write_usage[] = {
    "write [-b block -d hex] | [-u uid_hex]",
    "Write 16 bytes to MIFARE block 4:%s write -b 4 -d 00112233445566778899AABBCCDDEEFF",
    "Write 4 bytes to Ultralight page 4:%s write -b 4 -d 01020304",
    "Write magic UID (4 bytes):%s write -u 12345678",
    "Write with custom Key B:%s write -b 4 -d 00112233445566778899AABBCCDDEEFF -k A0A1A2A3A4A5 -B",
};
static const bp_command_def_t write_def = {
    .name = "write",
    .description = (uint32_t)(uintptr_t)"Write data block (16B) or page (4B) or magic UID (4B)",
    .opts = write_opts,
    .usage = write_usage,
    .usage_count = count_of(write_usage),
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
    if (!bp_cmd_get_uint32(&write_def, 'b', &block_idx)) {
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
        if (rfid_is_trailer((uint8_t)block_idx, tag.sak)) {
            printf("WARNING: block %u is a sector trailer (keys/access bits). Overwriting it can brick the card.\r\n",
                   (unsigned int)block_idx);
        }
        uint8_t key[6];
        uint8_t key_type = MIFARE_CMD_AUTH_A;
        uint8_t custom_key[6] = { 0 };
        uint8_t custom_type = MIFARE_CMD_AUTH_A;
        bool has_custom = rfid_parse_custom_key(&write_def, custom_key, &custom_type);
        if (!has_custom) {
            char key_probe[32] = { 0 };
            if (bp_cmd_get_string(&write_def, 'k', key_probe, sizeof(key_probe))) return;
        }
        uint8_t sector = (block_idx < 128) ? (uint8_t)(block_idx / 4) : (uint8_t)(32 + ((block_idx - 128) / 16));
        uint8_t first_block = (sector < 32) ? (uint8_t)(sector * 4) : (uint8_t)(128 + (sector - 32) * 16);
        if (rfid_auth_sector(first_block, tag.uid, tag.uid_len, custom_key, has_custom, custom_type,
                             key, &key_type) != PN532_OK) {
            printf("Error: Authentication failed for block %u (Key %c).\r\n",
                   (unsigned int)block_idx, (key_type == MIFARE_CMD_AUTH_B) ? 'B' : 'A');
            return;
        }
        printf("Authenticated sector %u with Key %c %02X%02X%02X%02X%02X%02X.\r\n", sector,
               (key_type == MIFARE_CMD_AUTH_B) ? 'B' : 'A',
               key[0], key[1], key[2], key[3], key[4], key[5]);
        pn532_status_t st = pn532_mifare_write_block((uint8_t)block_idx, payload);
        if (st == PN532_OK) {
            printf("%s[+] Successfully wrote 16 bytes to Block %u.%s\r\n",
                   ui_term_color_prompt(), (unsigned int)block_idx, ui_term_color_reset());
        } else {
            printf("[-] Failed to write Block %u (%s).\r\n", (unsigned int)block_idx, pn532_status_str(st));
        }
    } else if (data_len == 4 && tag.is_ultralight_or_ntag) {
        pn532_status_t st = pn532_ultralight_write_page((uint8_t)block_idx, payload);
        if (st == PN532_OK) {
            printf("%s[+] Successfully wrote 4 bytes to Page %u.%s\r\n",
                   ui_term_color_prompt(), (unsigned int)block_idx, ui_term_color_reset());
        } else {
            printf("[-] Failed to write Page %u (%s).\r\n", (unsigned int)block_idx, pn532_status_str(st));
        }
    } else {
        printf("Data size (%u bytes) does not match tag architecture (%s).\r\n",
               (unsigned int)data_len, tag.type_name);
    }
}

// -----------------------------------------------------------------------------
// Command: clone
// -----------------------------------------------------------------------------
static const char *const clone_usage[] = {
    "clone",
    "Interactive 2-step UID copy to Gen2 Magic tag:%s clone",
};
static const bp_command_def_t clone_def = {
    .name = "clone",
    .description = (uint32_t)(uintptr_t)"Clone UID to Magic Card (Gen1A / Gen2 UID changeable tag)",
    .usage = clone_usage,
    .usage_count = count_of(clone_usage),
};

static void clone_handler(struct command_result *res) {
    if (bp_cmd_help_check(&clone_def, res->help_flag)) return;
    if (!command_ready()) return;

    printf("\r\n=== RFID UID Cloning Tool ===\r\n");
    printf("Gen2 magic: direct Block 0 write (supported here).\r\n");
    printf("Gen1A magic: needs backdoor unlock 50 00 57 CD first (like nfc-mfsetuid);\r\n");
    printf("  not implemented -- use a Gen2/CUID card, or raw transceive -c for the backdoor.\r\n");
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
static const bp_command_opt_t erase_opts[] = {
    { "key", 'k', BP_ARG_REQUIRED, "hex", (uint32_t)(uintptr_t)"Custom 6-byte Classic key first (12 hex digits)" },
    { "key-b", 'B', BP_ARG_NONE, NULL, (uint32_t)(uintptr_t)"Custom/dictionary key is Key B (default Key A)" },
    { 0 }
};
static const char *const erase_usage[] = {
    "erase [-k key] [-B]",
    "Erase user data (trailers + lock/config preserved):%s erase",
    "Erase with custom Key B:%s erase -k A0A1A2A3A4A5 -B",
};
static const bp_command_def_t erase_def = {
    .name = "erase",
    .description = (uint32_t)(uintptr_t)"Erase tag user memory blocks to zero",
    .opts = erase_opts,
    .usage = erase_usage,
    .usage_count = count_of(erase_usage),
};

static void erase_handler(struct command_result *res) {
    if (bp_cmd_help_check(&erase_def, res->help_flag)) return;
    if (!command_ready()) return;

    printf("WARNING: This will overwrite user data blocks with 0x00.\r\n");
    printf("Sector trailers (keys/access) and NTAG lock/config are preserved.\r\n");
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

    uint8_t custom_key[6] = { 0 };
    uint8_t custom_type = MIFARE_CMD_AUTH_A;
    bool has_custom = rfid_parse_custom_key(&erase_def, custom_key, &custom_type);
    if (!has_custom) {
        char key_probe[32] = { 0 };
        if (bp_cmd_get_string(&erase_def, 'k', key_probe, sizeof(key_probe))) return;
    }
    pn532_status_t st = pn532_erase_tag_ex(&tag, custom_key, has_custom, custom_type);
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
    { "uri", 'u', BP_ARG_REQUIRED, "url",  (uint32_t)(uintptr_t)"NDEF URL / URI to write (e.g. https://buspirate.com)" },
    { "msg", 'm', BP_ARG_REQUIRED, "text", (uint32_t)(uintptr_t)"NDEF text message to write" },
    { 0 }
};
static const char *const ndef_usage[] = {
    "ndef [-u uri] | [-m message]",
    "Read existing NDEF record:%s ndef",
    "Write website URL:%s ndef -u https://buspirate.com",
    "Write text message:%s ndef -m \"Bus Pirate 5\"",
};
static const bp_command_def_t ndef_def = {
    .name = "ndef",
    .description = (uint32_t)(uintptr_t)"Read or write NDEF URI or Text record on NFC tags",
    .opts = ndef_opts,
    .usage = ndef_usage,
    .usage_count = count_of(ndef_usage),
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

// -----------------------------------------------------------------------------
// Command: pwauth (NTAG EV1 / NTAG21x password authentication, like --pw)
// -----------------------------------------------------------------------------
static const bp_command_opt_t pwauth_opts[] = {
    { "password", 'p', BP_ARG_REQUIRED, "hex", (uint32_t)(uintptr_t)"4-byte password in hex (e.g. FFFFFFFF)" },
    { 0 }
};
static const char *const pwauth_usage[] = {
    "pwauth -p password-hex",
    "Try default NTAG password:%s pwauth -p FFFFFFFF",
    "Try custom password:%s pwauth -p 12345678",
};
static const bp_command_def_t pwauth_def = {
    .name = "pwauth",
    .description = (uint32_t)(uintptr_t)"NTAG/Ultralight-EV1 PWD_AUTH and GET_VERSION",
    .opts = pwauth_opts,
    .usage = pwauth_usage,
    .usage_count = count_of(pwauth_usage),
};

static void pwauth_handler(struct command_result *res) {
    if (bp_cmd_help_check(&pwauth_def, res->help_flag)) return;
    if (!command_ready()) return;

    char pwd_str[32] = { 0 };
    if (!bp_cmd_get_string(&pwauth_def, 'p', pwd_str, sizeof(pwd_str))) {
        bp_cmd_help_show(&pwauth_def);
        return;
    }
    uint8_t pwd[4];
    if (parse_hex_bytes(pwd_str, pwd, sizeof(pwd)) != sizeof(pwd)) {
        printf("Invalid password: need exactly 4 bytes (8 hex digits, e.g. FFFFFFFF).\r\n");
        return;
    }

    printf("Place Ultralight-EV1/NTAG tag on reader... Press any key to cancel.\r\n");
    pn532_tag_info_t tag;
    while (true) {
        char c;
        if (rx_fifo_try_get(&c)) { printf("Cancelled.\r\n"); return; }
        if (pn532_detect_tag(&tag, 150) == PN532_OK) break;
        sleep_ms(50);
    }
    print_tag_header(&tag);
    if (!tag.is_ultralight_or_ntag) {
        printf("PWD_AUTH applies to Ultralight-EV1/NTAG21x (SAK 0x00), not %s.\r\n", tag.type_name);
    }

    uint8_t version[8];
    if (pn532_ntag_get_version(version) == PN532_OK) {
        printf("GET_VERSION: ");
        print_hex_line(version, sizeof(version));
        // Bytes 6..7 decode storage size per NXP NTAG datasheet.
        printf("  (vendor/product/subtype/major/minor/size/protocol -- see NTAG datasheet)\r\n");
    } else {
        printf("GET_VERSION not answered (plain Ultralight, not EV1/NTAG).\r\n");
    }

    uint8_t pack[2];
    pn532_status_t st = pn532_ntag_pwd_auth(pwd, pack);
    if (st == PN532_OK) {
        printf("%s[+] PWD_AUTH success! PACK: %02X %02X%s\r\n",
               ui_term_color_prompt(), pack[0], pack[1], ui_term_color_reset());
        printf("Tag now allows protected reads/writes until it leaves the RF field.\r\n");
    } else {
        printf("[-] PWD_AUTH failed (%s). Wrong password or tag not password-protected.\r\n",
               pn532_status_str(st));
    }
}

// -----------------------------------------------------------------------------
// Command: nonce (Mifare nested-auth nonce collector for offline mfkey tools)
// @details Real PN532 cannot sniff or crack keys on-chip: nonce collection is
//          slow and cracking needs a PC. This command performs repeated raw
//          AUTH attempts via InCommunicateThru (transparent RF exchange) and
//          logs (UID, NT, NR, AR, AT) samples. Students then run mfkey32v2 /
//          mfkey64 / hardnested on a PC, Killer-style workflow but honest about
//          PN532 limits: expect minutes, not seconds, and keep the tag still.
// -----------------------------------------------------------------------------
static const bp_command_opt_t nonce_opts[] = {
    { "block", 'b', BP_ARG_REQUIRED, "index", (uint32_t)(uintptr_t)"Target Classic block (auth applies to its sector)" },
    { "keytype", 'B', BP_ARG_NONE, NULL, (uint32_t)(uintptr_t)"Authenticate with Key B (default Key A)" },
    { "samples", 'n', BP_ARG_REQUIRED, "num", (uint32_t)(uintptr_t)"Number of nonce samples (default 8, max 64)" },
    { "file", 'f', BP_ARG_REQUIRED, "filename", (uint32_t)(uintptr_t)"Save samples as CSV for mfkey tools" },
    { 0 }
};
static const char *const nonce_usage[] = {
    "nonce -b block [-B] [-n num] [-f nonces.csv]",
    "Collect 8 nonces for block 4 Key A:%s nonce -b 4",
    "Collect 20 for Key B and save:%s nonce -b 4 -B -n 20 -f nonces.csv",
};
static const bp_command_def_t nonce_def = {
    .name = "nonce",
    .description = (uint32_t)(uintptr_t)"Collect Mifare auth nonces for offline key recovery",
    .opts = nonce_opts,
    .usage = nonce_usage,
    .usage_count = count_of(nonce_usage),
};

static void nonce_handler(struct command_result *res) {
    if (bp_cmd_help_check(&nonce_def, res->help_flag)) return;
    if (!command_ready()) return;

    uint32_t block_idx = 0;
    if (!bp_cmd_get_uint32(&nonce_def, 'b', &block_idx) || block_idx > 255) {
        printf("Missing/invalid target block (-b 0..255). See 'nonce -h'.\r\n");
        return;
    }
    uint8_t key_type = bp_cmd_find_flag(&nonce_def, 'B') ? MIFARE_CMD_AUTH_B : MIFARE_CMD_AUTH_A;
    uint32_t samples = 8;
    (void)bp_cmd_get_uint32(&nonce_def, 'n', &samples);
    if (samples < 2) samples = 2;
    if (samples > 64) samples = 64;
    char file_name[32] = { 0 };
    bool save_file = bp_cmd_get_string(&nonce_def, 'f', file_name, sizeof(file_name));

    printf("Place MIFARE Classic tag on reader (keep it still!)... Press any key to cancel.\r\n");
    pn532_tag_info_t tag;
    while (true) {
        char c;
        if (rx_fifo_try_get(&c)) { printf("Cancelled.\r\n"); return; }
        if (pn532_detect_tag(&tag, 150) == PN532_OK) break;
        sleep_ms(50);
    }
    if (!tag.is_mifare_classic) {
        printf("Nonce collection needs MIFARE Classic, not %s.\r\n", tag.type_name);
        return;
    }
    print_tag_header(&tag);
    printf("Target block %u (Key %c). PN532 is slow at this: ~2-4 samples/sec. Keep tag still.\r\n",
           (unsigned int)block_idx, (key_type == MIFARE_CMD_AUTH_B) ? 'B' : 'A');

    FIL fil;
    bool fil_open = false;
    if (save_file) {
        if (f_open(&fil, file_name, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) {
            printf("Failed to create '%s'.\r\n", file_name);
            return;
        }
        fil_open = true;
        f_printf(&fil, "# mfkey nonce log: uid,block,key_type,nt,nr,ar,at\r\n");
        f_printf(&fil, "# UID: ");
        for (uint8_t i = 0; i < tag.uid_len; i++) f_printf(&fil, "%02X", tag.uid[i]);
        f_printf(&fil, " block=%u key=%c\r\n", (unsigned int)block_idx,
                 (key_type == MIFARE_CMD_AUTH_B) ? 'B' : 'A');
        f_printf(&fil, "# PC usage: mfkey32v2 UID NT NR0 AR0 NR1 AR1 | mfkey64 UID NT NR AR AT\r\n");
    }

    uint32_t lcg = to_ms_since_boot(get_absolute_time()) ^ 0xA5A5A5A5u;
    uint32_t collected = 0;
    for (uint32_t s = 0; s < samples; s++) {
        char c;
        if (rx_fifo_try_get(&c)) { printf("Stopped by user after %u sample(s).\r\n", (unsigned int)collected); break; }

        // Re-select every sample: a failed AUTH drops the tag, like Killer sniffers do.
        if (pn532_detect_tag(&tag, 200) != PN532_OK) { sleep_ms(60); continue; }

        uint8_t auth[2] = { key_type, (uint8_t)block_idx };
        uint8_t nt[4] = { 0 };
        uint8_t nt_len = sizeof(nt);
        if (pn532_communicate_thru(auth, sizeof(auth), nt, &nt_len, 300) != PN532_OK || nt_len != 4) {
            sleep_ms(60);
            continue;
        }
        // Random reader nonce + (deliberately arbitrary) response. Even failed
        // auths leak (NT, NR, AR) equations that mfkey32v2/mfkey64 can use.
        uint8_t nr_ar[8];
        for (int i = 0; i < 8; i++) {
            lcg = lcg * 1664525u + 1013904223u;
            nr_ar[i] = (uint8_t)(lcg >> 24);
        }
        uint8_t at[8] = { 0 };
        uint8_t at_len = sizeof(at);
        pn532_status_t ast = pn532_communicate_thru(nr_ar, sizeof(nr_ar), at, &at_len, 300);
        (void)ast; // AT may be 4B success, 1B NACK, or timeout -- all get logged.

        printf("[%u] NT: %02X%02X%02X%02X NR: ", (unsigned int)(collected + 1), nt[0], nt[1], nt[2], nt[3]);
        for (int i = 0; i < 4; i++) printf("%02X", nr_ar[i]);
        printf(" AR: ");
        for (int i = 4; i < 8; i++) printf("%02X", nr_ar[i]);
        printf(" AT: ");
        if (at_len) {
            for (uint8_t i = 0; i < at_len; i++) printf("%02X", at[i]);
        } else {
            printf("(timeout)");
        }
        printf("\r\n");

        if (fil_open) {
            f_printf(&fil, "%u,%c,", (unsigned int)block_idx,
                     (key_type == MIFARE_CMD_AUTH_B) ? 'B' : 'A');
            f_printf(&fil, "%02X%02X%02X%02X,", nt[0], nt[1], nt[2], nt[3]);
            for (int i = 0; i < 4; i++) f_printf(&fil, "%02X", nr_ar[i]);
            f_printf(&fil, ",");
            for (int i = 4; i < 8; i++) f_printf(&fil, "%02X", nr_ar[i]);
            f_printf(&fil, ",");
            if (at_len) {
                for (uint8_t i = 0; i < at_len; i++) f_printf(&fil, "%02X", at[i]);
            } else {
                f_printf(&fil, "timeout");
            }
            f_printf(&fil, "\r\n");
        }
        collected++;
        sleep_ms(120);
    }
    if (fil_open) {
        f_close(&fil);
        printf("Saved %u nonce sample(s) to '%s'. Crack on PC, e.g. mfkey32v2 / mfkey64.\r\n",
               (unsigned int)collected, file_name);
    } else {
        printf("Collected %u nonce sample(s). Re-run with -f to save for mfkey tools.\r\n", (unsigned int)collected);
    }
    printf("Note: cracking runs on a PC (RP2040 is too slow); PN532 collection is minutes, not Killer-seconds.\r\n");
}

// Command table
const struct _mode_command_struct rfid_commands[] = {
    { .func = scan_handler,  .def = &scan_def,  .supress_fala_capture = true },
    { .func = transceive_handler, .def = &transceive_def, .supress_fala_capture = true },
    { .func = jewel_handler, .def = &jewel_def, .supress_fala_capture = true },
    { .func = forum3_handler, .def = &forum3_def, .supress_fala_capture = true },
    { .func = forum4_handler, .def = &forum4_def, .supress_fala_capture = true },
    { .func = read_handler,  .def = &read_def,  .supress_fala_capture = true },
    { .func = dump_handler,  .def = &dump_def,  .supress_fala_capture = true },
    { .func = restore_handler, .def = &restore_def, .supress_fala_capture = true },
    { .func = write_handler, .def = &write_def, .supress_fala_capture = true },
    { .func = clone_handler, .def = &clone_def, .supress_fala_capture = true },
    { .func = erase_handler, .def = &erase_def, .supress_fala_capture = true },
    { .func = ndef_handler,  .def = &ndef_def,  .supress_fala_capture = true },
    { .func = pwauth_handler, .def = &pwauth_def, .supress_fala_capture = true },
    { .func = scanner_handler, .def = &scanner_def, .supress_fala_capture = true },
    { .func = nonce_handler, .def = &nonce_def, .supress_fala_capture = true },
    { .func = info_handler,  .def = &info_def,  .supress_fala_capture = true },
    { .func = tools_handler, .def = &tools_def, .supress_fala_capture = true },
};
const uint32_t rfid_commands_count = count_of(rfid_commands);

// Setup UI
uint32_t rfid_setup(void) {
    const char config_file[] = "bprfid.bp";
    const mode_config_t config_t[] = {
        { "$.pin_pair", &rfid_config.pin_pair, MODE_CONFIG_FORMAT_DECIMAL },
        { "$.baudrate", &rfid_config.baudrate, MODE_CONFIG_FORMAT_DECIMAL },
    };

    uint32_t temp = 0;
    bp_cmd_status_t st = bp_cmd_flag(&rfid_setup_def, 'p', &temp);
    if (st == BP_CMD_INVALID) return 0;
    bool interactive = (st == BP_CMD_MISSING);

    if (interactive) {
        if (storage_load_mode(config_file, config_t, count_of(config_t))) {
            printf("\r\n\r\n%s%s%s\r\n", ui_term_color_info(), GET_T(T_USE_PREVIOUS_SETTINGS), ui_term_color_reset());
            rfid_settings();
            int r = bp_cmd_yes_no_exit("");
            if (r == BP_YN_EXIT) return 0;
            if (r == BP_YN_YES) return 1;
        }

        if (bp_cmd_prompt(&pin_choice, &temp) != BP_CMD_OK) return 0;
        rfid_config.pin_pair = temp;

        if (bp_cmd_prompt(&baud_choice, &temp) != BP_CMD_OK) return 0;
        rfid_config.baudrate = temp;
    } else {
        rfid_config.pin_pair = (temp == 1) ? 1 : 0;

        st = bp_cmd_flag(&rfid_setup_def, 'b', &temp);
        if (st == BP_CMD_INVALID) return 0;
        if (st == BP_CMD_OK) {
            if (!rfid_baud_supported(temp)) {
                printf("Unsupported baud %u. Use 9600, 115200, 230400, 460800, or 921600.\r\n",
                       (unsigned int)temp);
                return 0;
            }
            rfid_config.baudrate = temp;
        } else {
            rfid_config.baudrate = 115200;
        }
    }

    if (rfid_config.pin_pair == 1) {
        rfid_config.tx_pin = BIO0;
        rfid_config.rx_pin = BIO1;
    } else {
        rfid_config.tx_pin = BIO4;
        rfid_config.rx_pin = BIO5;
    }

    storage_save_mode(config_file, config_t, count_of(config_t));
    return 1;
}

uint32_t rfid_setup_exc(void) {
    if (rfid_config.pin_pair == 1) {
        rfid_config.tx_pin = BIO0;
        rfid_config.rx_pin = BIO1;
    } else {
        rfid_config.tx_pin = BIO4;
        rfid_config.rx_pin = BIO5;
    }

    system_bio_update_purpose_and_label(true, (uint8_t)rfid_config.tx_pin, BP_PIN_MODE, pin_labels[0]);
    system_bio_update_purpose_and_label(true, (uint8_t)rfid_config.rx_pin, BP_PIN_MODE, pin_labels[1]);

    pn532_init((uint8_t)rfid_config.tx_pin, (uint8_t)rfid_config.rx_pin, rfid_config.baudrate);
    rfid_config.initialized = 1;

    printf("\r\n%sRFID Mode Configured (PN532 High-Speed UART)%s\r\n", ui_term_color_notice(), ui_term_color_reset());
    printf(" Pins: TX = BIO%u (PN532 RX), RX = BIO%u (PN532 TX)\r\n",
           (unsigned int)rfid_config.tx_pin, (unsigned int)rfid_config.rx_pin);
    printf(" Baud: %u 8N1 | DIP switches: %sSEL0 = 0 (OFF), SEL1 = 0 (OFF)%s\r\n",
           (unsigned int)rfid_config.baudrate, ui_term_color_prompt(), ui_term_color_reset());
    if (rfid_config.baudrate != 115200 && rfid_config.baudrate != 9600) {
        printf(" High baud: handshake starts at 115200, then SetSerialBaudrate switches.\r\n");
        printf(" Keep wires short (<10cm) at 460800+.\r\n");
    }
    printf("\r\n%sNOTE:%s VREG power is OFF by default.\r\n", ui_term_color_warning(), ui_term_color_reset());
    printf(" Type %sW 3.3%s to power the PN532 module.\r\n\r\n", ui_term_color_prompt(), ui_term_color_reset());

    return 1;
}

void rfid_cleanup(void) {
    if (rfid_config.initialized) {
        pn532_deinit();
    }
    rfid_config.initialized = 0;
    system_bio_update_purpose_and_label(false, (uint8_t)rfid_config.tx_pin, BP_PIN_MODE, 0);
    system_bio_update_purpose_and_label(false, (uint8_t)rfid_config.rx_pin, BP_PIN_MODE, 0);
}

void rfid_settings(void) {
    printf("PN532 TX: BIO%u (Bus Pirate TX -> PN532 RX)\r\n", (unsigned int)rfid_config.tx_pin);
    printf("PN532 RX: BIO%u (Bus Pirate RX <- PN532 TX)\r\n", (unsigned int)rfid_config.rx_pin);
    printf("Baud Rate: %u bps (8N1)\r\n", (unsigned int)rfid_config.baudrate);
    printf("Status:   %s\r\n", pn532_is_connected() ? "Connected & Ready" : "Awaiting power / Not connected");
}

void rfid_help(void) {
    printf("NXP PN532 RFID / NFC Reader/Writer Mode (High-Speed UART).\r\n\r\n");
    printf("Hardware Setup:\r\n");
    printf("  1. Configure PN532 DIP switches: SEL0 = 0 (OFF), SEL1 = 0 (OFF) for HSU.\r\n");
    printf("  2. Connect VCC -> VOUT, GND -> GND.\r\n");
    printf("  3. Connect PN532 RX -> BIO%u (TX), PN532 TX -> BIO%u (RX).\r\n",
           (unsigned int)rfid_config.tx_pin, (unsigned int)rfid_config.rx_pin);
    printf("  4. Turn on power using 'W 3.3' command.\r\n\r\n");
    ui_help_mode_commands(rfid_commands, rfid_commands_count);
}

bool rfid_preflight_sanity_check(void) {
    return ui_help_sanity_check(true, 0x00);
}
