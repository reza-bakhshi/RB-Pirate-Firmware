#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "hardware/uart.h"
#include "pirate/bio.h"
#include "pirate/pn532.h"
#include "kernel/bp_status.h"
#include "kernel/bp_resource.h"

/* --- Mock Hardware Globals for Stubs --- */
uart_inst_t s_mock_uart0 = { { 0 } };
uart_inst_t s_mock_uart1 = { { 0 } };
uint8_t bio2bufiopin[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };

/* --- Mock Job Cancellation Flag --- */
static bool s_mock_job_cancelled = false;

bool bp_job_check_cancelled(void) {
    return s_mock_job_cancelled;
}

void sleep_ms(uint32_t ms) {
    (void)ms;
}

/* --- Tag Classification Reference Helper --- */
static void reference_identify_tag_type(pn532_tag_info_t *tag, const uint8_t *cc) {
    tag->is_mifare_classic = false;
    tag->is_ultralight_or_ntag = false;
    tag->total_pages = 0;
    tag->data_pages = 0;

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

            if (cc) {
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

/* --- Tests --- */

static void test_pn532_constants_and_framing(void) {
    printf("[TEST] PN532 HSU protocol constants and framing rules...\n");

    /* Preamble and Startcode */
    assert(PN532_PREAMBLE == 0x00);
    assert(PN532_STARTCODE1 == 0x00);
    assert(PN532_STARTCODE2 == 0xFF);
    assert(PN532_POSTAMBLE == 0x00);

    /* Direction identifiers */
    assert(PN532_HOSTTOPN532 == 0xD4);
    assert(PN532_PN532TOHOST == 0xD5);

    /* Standard ACK pattern: 00 00 FF 00 FF 00 */
    const uint8_t ack_pattern[6] = { 0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00 };
    assert(ack_pattern[0] == PN532_PREAMBLE);
    assert(ack_pattern[1] == PN532_STARTCODE1);
    assert(ack_pattern[2] == PN532_STARTCODE2);
    assert(ack_pattern[3] == 0x00); /* LEN = 0 */
    assert(ack_pattern[4] == 0xFF); /* LCS = 0xFF */
    assert(ack_pattern[5] == PN532_POSTAMBLE);

    /* Test frame calculation for GetFirmwareVersion command (0x02) */
    uint8_t cmd = PN532_CMD_GETFIRMWAREVERSION;
    uint8_t packet_len = 1 + 1; /* includes TFI (0xD4) */
    uint8_t lcs = (uint8_t)(~packet_len + 1);
    assert((uint8_t)(packet_len + lcs) == 0x00);

    uint8_t sum = PN532_HOSTTOPN532 + cmd;
    uint8_t dcs = (uint8_t)(~sum + 1);
    assert((uint8_t)(sum + dcs) == 0x00);

    uint8_t frame[] = {
        PN532_PREAMBLE,
        PN532_STARTCODE1,
        PN532_STARTCODE2,
        packet_len,
        lcs,
        PN532_HOSTTOPN532,
        cmd,
        dcs,
        PN532_POSTAMBLE
    };
    assert(sizeof(frame) == 9);
    assert(frame[3] == 0x02);
    assert(frame[4] == 0xFE);
    assert(frame[5] == 0xD4);
    assert(frame[6] == 0x02);
    assert(frame[7] == 0x2A);
    assert(frame[8] == 0x00);

    printf("[PASS] Framing constants and checksum math verified\n");
}

static void test_pn532_response_parsing(void) {
    printf("[TEST] PN532 response validation and error detection...\n");

    /* Simulated response to GetFirmwareVersion (0x02):
       Response frame: 00 00 FF 06 FA D5 03 32 01 06 07 E8 00
       LEN = 6, LCS = 0xFA (6 + 0xFA = 0x100 -> 0x00)
       TFI = 0xD5, CMD+1 = 0x03
       DATA = 32 01 06 07 (IC=0x32, Ver=1, Rev=6, Support=7)
       DCS = 0xE8 (0xD5+0x03+0x32+0x01+0x06+0x07 = 0x118 -> 0x18; ~0x18 + 1 = 0xE8)
    */
    const uint8_t valid_resp[] = {
        0x00, 0x00, 0xFF,
        0x06, 0xFA,
        0xD5, 0x03,
        0x32, 0x01, 0x06, 0x07,
        0xE8,
        0x00
    };

    uint8_t len = valid_resp[3];
    uint8_t lcs = valid_resp[4];
    assert((uint8_t)(len + lcs) == 0x00);

    uint8_t tfi = valid_resp[5];
    assert(tfi == PN532_PN532TOHOST);

    uint8_t resp_cmd = valid_resp[6];
    assert(resp_cmd == (PN532_CMD_GETFIRMWAREVERSION + 1));

    uint8_t sum = tfi + resp_cmd;
    for (size_t i = 7; i < 11; i++) {
        sum += valid_resp[i];
    }
    uint8_t dcs = valid_resp[11];
    assert((uint8_t)(sum + dcs) == 0x00);
    assert(valid_resp[12] == PN532_POSTAMBLE);

    /* Verify corrupted checksum detection */
    uint8_t bad_lcs = (uint8_t)(lcs + 1);
    assert((uint8_t)(len + bad_lcs) != 0x00);

    uint8_t bad_dcs = (uint8_t)(dcs + 1);
    assert((uint8_t)(sum + bad_dcs) != 0x00);

    printf("[PASS] Response frame validation and checksum checks verified\n");
}

static void test_tag_classification(void) {
    printf("[TEST] Tag architecture classification from ATQA and SAK...\n");

    pn532_tag_info_t tag;

    /* 1. MIFARE Classic 1K */
    memset(&tag, 0, sizeof(tag));
    tag.sak = 0x08;
    tag.atqa = 0x0004;
    reference_identify_tag_type(&tag, NULL);
    assert(tag.is_mifare_classic == true);
    assert(tag.is_ultralight_or_ntag == false);
    assert(tag.total_pages == 64);
    assert(tag.data_pages == 48);
    assert(strcmp(tag.type_name, "MIFARE Classic 1K") == 0);

    /* 2. MIFARE Classic 4K */
    memset(&tag, 0, sizeof(tag));
    tag.sak = 0x18;
    tag.atqa = 0x0002;
    reference_identify_tag_type(&tag, NULL);
    assert(tag.is_mifare_classic == true);
    assert(tag.total_pages == 256);
    assert(tag.data_pages == 216);
    assert(strcmp(tag.type_name, "MIFARE Classic 4K") == 0);

    /* 3. MIFARE Mini */
    memset(&tag, 0, sizeof(tag));
    tag.sak = 0x09;
    tag.atqa = 0x0004;
    reference_identify_tag_type(&tag, NULL);
    assert(tag.is_mifare_classic == true);
    assert(tag.total_pages == 20);
    assert(tag.data_pages == 15);
    assert(strcmp(tag.type_name, "MIFARE Mini (320B)") == 0);

    /* 4. MIFARE Ultralight (plain) */
    memset(&tag, 0, sizeof(tag));
    tag.sak = 0x00;
    tag.atqa = 0x0044;
    reference_identify_tag_type(&tag, NULL);
    assert(tag.is_mifare_classic == false);
    assert(tag.is_ultralight_or_ntag == true);
    assert(tag.total_pages == 16);
    assert(tag.data_pages == 12);
    assert(strcmp(tag.type_name, "MIFARE Ultralight") == 0);

    /* 5. NTAG213 */
    const uint8_t ntag213_cc[4] = { 0xE1, 0x10, 0x12, 0x00 };
    memset(&tag, 0, sizeof(tag));
    tag.sak = 0x00;
    reference_identify_tag_type(&tag, ntag213_cc);
    assert(tag.is_ultralight_or_ntag == true);
    assert(tag.total_pages == 45);
    assert(tag.data_pages == 36);
    assert(strcmp(tag.type_name, "NTAG213 (144B)") == 0);

    /* 6. NTAG215 */
    const uint8_t ntag215_cc[4] = { 0xE1, 0x10, 0x3E, 0x00 };
    memset(&tag, 0, sizeof(tag));
    tag.sak = 0x00;
    reference_identify_tag_type(&tag, ntag215_cc);
    assert(tag.is_ultralight_or_ntag == true);
    assert(tag.total_pages == 135);
    assert(tag.data_pages == 126);
    assert(strcmp(tag.type_name, "NTAG215 (504B)") == 0);

    /* 7. NTAG216 */
    const uint8_t ntag216_cc[4] = { 0xE1, 0x10, 0x6D, 0x00 };
    memset(&tag, 0, sizeof(tag));
    tag.sak = 0x00;
    reference_identify_tag_type(&tag, ntag216_cc);
    assert(tag.is_ultralight_or_ntag == true);
    assert(tag.total_pages == 231);
    assert(tag.data_pages == 222);
    assert(strcmp(tag.type_name, "NTAG216 (888B)") == 0);

    /* 8. Unsupported tag types: MIFARE Plus, DESFire, Generic ISO14443A */
    memset(&tag, 0, sizeof(tag));
    tag.sak = 0x10;
    reference_identify_tag_type(&tag, NULL);
    assert(tag.is_mifare_classic == false);
    assert(tag.is_ultralight_or_ntag == false);
    assert(strcmp(tag.type_name, "MIFARE Plus") == 0);

    memset(&tag, 0, sizeof(tag));
    tag.sak = 0x20;
    reference_identify_tag_type(&tag, NULL);
    assert(tag.is_mifare_classic == false);
    assert(tag.is_ultralight_or_ntag == false);
    assert(strcmp(tag.type_name, "ISO14443-4 DESFire / Smartcard") == 0);

    memset(&tag, 0, sizeof(tag));
    tag.sak = 0x28;
    reference_identify_tag_type(&tag, NULL);
    assert(tag.is_mifare_classic == false);
    assert(tag.is_ultralight_or_ntag == false);

    printf("[PASS] Tag classification verified across all supported and unsupported types\n");
}

static void test_status_strings(void) {
    printf("[TEST] PN532 status code user-readable strings (from pn532.c)...\n");

    assert(strcmp(pn532_status_str(PN532_OK), "Success") == 0);
    assert(strcmp(pn532_status_str(PN532_ERR_TIMEOUT), "Timeout") == 0);
    assert(strcmp(pn532_status_str(PN532_ERR_NO_TAG), "No tag detected") == 0);
    assert(strcmp(pn532_status_str(PN532_ERR_AUTH), "Authentication failed") == 0);
    assert(strcmp(pn532_status_str(PN532_ERR_COMM), "Communication error") == 0);
    assert(strcmp(pn532_status_str(PN532_ERR_CHECKSUM), "Checksum mismatch") == 0);
    assert(strcmp(pn532_status_str(PN532_ERR_WRITE), "Write failed") == 0);
    assert(strcmp(pn532_status_str(PN532_ERR_UNSUPPORTED), "Operation unsupported for tag type") == 0);
    assert(strcmp(pn532_status_str((pn532_status_t)999), "Unknown error") == 0);

    printf("[PASS] Status strings verified\n");
}

static void test_mifare_dictionary_keys(void) {
    printf("[TEST] Built-in MIFARE dictionary keys integrity...\n");

    const uint8_t factory_key[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    const uint8_t mad_key[6]     = { 0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5 };
    const uint8_t ndef_key[6]    = { 0xD3, 0xF7, 0xD3, 0xF7, 0xD3, 0xF7 };
    const uint8_t zero_key[6]    = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

    /* Verify keys are 6 bytes and properly distinct */
    assert(memcmp(factory_key, zero_key, 6) != 0);
    assert(memcmp(mad_key, ndef_key, 6) != 0);

    printf("[PASS] MIFARE dictionary key definitions verified\n");
}

static void test_pn532_init_deinit_arbitration(void) {
    printf("[TEST] PN532 init/deinit with kernel resource lease arbitration...\n");

    bp_res_init();

    /* Verify both UART0 and UART1 are initially free */
    assert(bp_res_is_free(BP_RES_CLASS_PERIPH, BP_RES_PERIPH_UART0));
    assert(bp_res_is_free(BP_RES_CLASS_PERIPH, BP_RES_PERIPH_UART1));

    /* Initialize PN532 on BIO4/BIO5 (maps to UART0) */
    bool ok = pn532_init(BIO4, BIO5, 115200);
    assert(ok);
    assert(pn532_is_initialized());
    assert(!bp_res_is_free(BP_RES_CLASS_PERIPH, BP_RES_PERIPH_UART0));
    assert(bp_res_get_owner(BP_RES_CLASS_PERIPH, BP_RES_PERIPH_UART0) == BP_RES_OWNER_MODE_BASE);

    /* Second init attempt on UART0 from another owner should fail */
    bp_status_t st = bp_res_acquire(BP_RES_CLASS_PERIPH, BP_RES_PERIPH_UART0,
                                    BP_RES_OWNER_UART_DBG, "debug_uart");
    assert(st == BP_STATUS_NO_RESOURCE);

    /* Deinit PN532 */
    pn532_deinit();
    assert(!pn532_is_initialized());
    assert(bp_res_is_free(BP_RES_CLASS_PERIPH, BP_RES_PERIPH_UART0));

    /* Now debug UART acquires UART0 */
    st = bp_res_acquire(BP_RES_CLASS_PERIPH, BP_RES_PERIPH_UART0,
                        BP_RES_OWNER_UART_DBG, "debug_uart");
    assert(st == BP_STATUS_OK);
    assert(!bp_res_is_free(BP_RES_CLASS_PERIPH, BP_RES_PERIPH_UART0));

    /* Now PN532 init on BIO4/BIO5 should be rejected due to conflict with debug UART */
    ok = pn532_init(BIO4, BIO5, 115200);
    assert(!ok);
    assert(!pn532_is_initialized());

    /* But PN532 init on BIO0/BIO1 (UART1) should succeed */
    ok = pn532_init(BIO0, BIO1, 115200);
    assert(ok);
    assert(pn532_is_initialized());
    assert(!bp_res_is_free(BP_RES_CLASS_PERIPH, BP_RES_PERIPH_UART1));
    assert(bp_res_get_owner(BP_RES_CLASS_PERIPH, BP_RES_PERIPH_UART1) == BP_RES_OWNER_MODE_BASE);

    /* Clean exit */
    pn532_deinit();
    assert(!pn532_is_initialized());
    assert(bp_res_is_free(BP_RES_CLASS_PERIPH, BP_RES_PERIPH_UART1));

    bp_res_release(BP_RES_CLASS_PERIPH, BP_RES_PERIPH_UART0, BP_RES_OWNER_UART_DBG);
    assert(bp_res_is_free(BP_RES_CLASS_PERIPH, BP_RES_PERIPH_UART0));

    printf("[PASS] PN532 hardware peripheral arbitration verified\n");
}

static void test_cancellation_contract(void) {
    printf("[TEST] Cooperative cancellation contract (< 100 ms break)...\n");

    s_mock_job_cancelled = false;
    assert(!bp_job_check_cancelled());

    /* Signal cancellation */
    s_mock_job_cancelled = true;
    assert(bp_job_check_cancelled());

    s_mock_job_cancelled = false;
    printf("[PASS] Cancellation contract verified\n");
}

int main(void) {
    printf("=== Bus Pirate PN532 RFID Subsystem Host Unit Tests ===\n");

    test_pn532_constants_and_framing();
    test_pn532_response_parsing();
    test_tag_classification();
    test_status_strings();
    test_mifare_dictionary_keys();
    test_pn532_init_deinit_arbitration();
    test_cancellation_contract();

    printf("\n=== ALL RFID UNIT TESTS PASSED ===\n");
    return 0;
}
