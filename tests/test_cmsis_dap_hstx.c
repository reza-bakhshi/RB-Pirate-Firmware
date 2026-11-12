#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>

#include "pirate.h"
#include "system_config.h"
#include "binmode/cmsis_dap.h"
#include "pirate/hstx_dvi.h"
#include "pirate/font8x16.h"

// Stubs for host test
struct _system_config system_config;
const uint8_t bio2bufiopin[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };

void system_bio_update_purpose_and_label(bool enable, uint8_t bio_pin, enum bp_pin_func func, const char* label) {
    (void)enable; (void)bio_pin; (void)func; (void)label;
}

void bio_init(void) {}

// Test 1: CMSIS-DAP v2 Info Queries
static void test_cmsis_dap_info(void) {
    printf("[1] Testing CMSIS-DAP v2 Info Queries... ");
    cmsis_dap_init();
    assert(!cmsis_dap_is_connected());

    uint8_t req[16];
    uint8_t resp[64];
    uint32_t resp_len;

    // 1. FW Version query
    req[0] = DAP_ID_INFO;
    req[1] = DAP_INFO_FW_VER;
    resp_len = cmsis_dap_process_command(req, 2, resp, sizeof(resp));
    assert(resp_len >= 7);
    assert(resp[0] == DAP_ID_INFO);
    assert(resp[1] == 6); // Length of "2.1.1" + null
    assert(strcmp((char*)&resp[2], "2.1.1") == 0);

    // 2. Vendor query
    req[1] = DAP_INFO_VENDOR;
    resp_len = cmsis_dap_process_command(req, 2, resp, sizeof(resp));
    assert(resp_len > 2);
    assert(resp[0] == DAP_ID_INFO);
    assert(strcmp((char*)&resp[2], "Bus Pirate") == 0);

    // 3. Product query
    req[1] = DAP_INFO_PRODUCT;
    resp_len = cmsis_dap_process_command(req, 2, resp, sizeof(resp));
    assert(resp_len > 2);
    assert(resp[0] == DAP_ID_INFO);
    assert(strstr((char*)&resp[2], "CMSIS-DAP") != NULL);

    // 4. Capabilities (SWD support)
    req[1] = DAP_INFO_CAPABILITIES;
    resp_len = cmsis_dap_process_command(req, 2, resp, sizeof(resp));
    assert(resp_len == 3);
    assert(resp[0] == DAP_ID_INFO);
    assert(resp[1] == 1);
    assert((resp[2] & 0x01) == 0x01); // Bit 0: SWD supported

    // 5. Packet size query (0xFF)
    req[1] = DAP_INFO_PACKET_SIZE;
    resp_len = cmsis_dap_process_command(req, 2, resp, sizeof(resp));
    assert(resp_len == 4);
    assert(resp[0] == DAP_ID_INFO);
    assert(resp[1] == 2);
    uint16_t pkt_sz = resp[2] | ((uint16_t)resp[3] << 8);
    assert(pkt_sz == CMSIS_DAP_PACKET_SIZE);

    printf("PASS\n");
}

// Test 2: CMSIS-DAP Connect, Pin and Clock Configuration
static void test_cmsis_dap_connect_and_config(void) {
    printf("[2] Testing CMSIS-DAP Connect & Configuration... ");
    uint8_t req[16];
    uint8_t resp[64];
    uint32_t resp_len;

    // Connect to SWD port (port 1)
    req[0] = DAP_ID_CONNECT;
    req[1] = DAP_PORT_SWD;
    resp_len = cmsis_dap_process_command(req, 2, resp, sizeof(resp));
    assert(resp_len == 2);
    assert(resp[0] == DAP_ID_CONNECT);
    assert(resp[1] == DAP_PORT_SWD);
    assert(cmsis_dap_is_connected());

    // Host status
    req[0] = DAP_ID_HOST_STATUS;
    req[1] = 0; // Type: connect LED
    req[2] = 1; // Status: on
    resp_len = cmsis_dap_process_command(req, 3, resp, sizeof(resp));
    assert(resp_len == 2);
    assert(resp[0] == DAP_ID_HOST_STATUS);
    assert(resp[1] == DAP_OK);

    // Set SWJ Clock (e.g. 5 MHz = 5000000 Hz = 0x004C4B40)
    req[0] = DAP_ID_SWJ_CLOCK;
    req[1] = 0x40;
    req[2] = 0x4B;
    req[3] = 0x4C;
    req[4] = 0x00;
    resp_len = cmsis_dap_process_command(req, 5, resp, sizeof(resp));
    assert(resp_len == 2);
    assert(resp[0] == DAP_ID_SWJ_CLOCK);
    assert(resp[1] == DAP_OK);

    // Transfer Configure (idle cycles = 1, retries = 50)
    req[0] = DAP_ID_TRANSFER_CONFIGURE;
    req[1] = 1;    // Idle cycles
    req[2] = 50;   // Retries low
    req[3] = 0;    // Retries high
    req[4] = 0;    // Match retry low
    req[5] = 0;    // Match retry high
    resp_len = cmsis_dap_process_command(req, 6, resp, sizeof(resp));
    assert(resp_len == 2);
    assert(resp[0] == DAP_ID_TRANSFER_CONFIGURE);
    assert(resp[1] == DAP_OK);

    // SWD Configure (turnaround = 1)
    req[0] = DAP_ID_SWD_CONFIGURE;
    req[1] = 0; // Turnaround 1
    resp_len = cmsis_dap_process_command(req, 2, resp, sizeof(resp));
    assert(resp_len == 2);
    assert(resp[0] == DAP_ID_SWD_CONFIGURE);
    assert(resp[1] == DAP_OK);

    // SWJ Pins
    req[0] = DAP_ID_SWJ_PINS;
    req[1] = 0x03; // Output values
    req[2] = 0x03; // Pin select mask
    req[3] = 0x00; // Wait time (us) low
    req[4] = 0x00;
    req[5] = 0x00;
    req[6] = 0x00;
    resp_len = cmsis_dap_process_command(req, 7, resp, sizeof(resp));
    assert(resp_len == 2);
    assert(resp[0] == DAP_ID_SWJ_PINS);

    printf("PASS\n");
}

// Test 3: SWD Transfer Request - Reading DP IDCODE and AP IDR
static void test_cmsis_dap_transfer_dp_ap(void) {
    printf("[3] Testing SWD Transfer Request (DP IDCODE & AP IDR)... ");
    uint8_t req[32];
    uint8_t resp[64];
    uint32_t resp_len;

    // 1. Read DP IDCODE (reg 0x00, DP, Read: APnDP=0, RnW=1 -> req_hdr = 0x02)
    req[0] = DAP_ID_TRANSFER;
    req[1] = 0; // DAP Index
    req[2] = 1; // Transfer count
    req[3] = 0x02; // DP Read reg 0x00 (IDCODE)
    resp_len = cmsis_dap_process_command(req, 4, resp, sizeof(resp));
    assert(resp_len == 7);
    assert(resp[0] == DAP_ID_TRANSFER);
    assert(resp[1] == 1); // 1 transfer executed
    assert(resp[2] == DAP_TRANSFER_OK);
    uint32_t idcode = resp[3] | ((uint32_t)resp[4] << 8) |
                     ((uint32_t)resp[5] << 16) | ((uint32_t)resp[6] << 24);
    assert(idcode == 0x0BF11477); // ARM Cortex-M0+ DP IDCODE

    // 2. Write DP CTRL/STAT (reg 0x04, DP, Write: APnDP=0, RnW=0, reg=0x04 -> req_hdr = 0x04)
    req[0] = DAP_ID_TRANSFER;
    req[1] = 0;
    req[2] = 1;
    req[3] = 0x04;
    uint32_t pwr_req = 0x50000000;
    memcpy(&req[4], &pwr_req, 4);
    resp_len = cmsis_dap_process_command(req, 8, resp, sizeof(resp));
    assert(resp_len == 3);
    assert(resp[0] == DAP_ID_TRANSFER);
    assert(resp[1] == 1);
    assert(resp[2] == DAP_TRANSFER_OK);

    // 3. Read back DP CTRL/STAT (reg 0x04, DP, Read: req_hdr = 0x06)
    req[0] = DAP_ID_TRANSFER;
    req[1] = 0;
    req[2] = 1;
    req[3] = 0x06;
    resp_len = cmsis_dap_process_command(req, 4, resp, sizeof(resp));
    assert(resp_len == 7);
    assert(resp[1] == 1);
    assert(resp[2] == DAP_TRANSFER_OK);
    uint32_t ctrl_stat = resp[3] | ((uint32_t)resp[4] << 8) |
                         ((uint32_t)resp[5] << 16) | ((uint32_t)resp[6] << 24);
    assert(ctrl_stat == 0x50000000);

    // 4. Read AP IDR (reg 0x0C, AP, Read: APnDP=1, RnW=1, reg=0x0C -> req_hdr = 0x0F)
    req[0] = DAP_ID_TRANSFER;
    req[1] = 0;
    req[2] = 1;
    req[3] = 0x0F;
    resp_len = cmsis_dap_process_command(req, 4, resp, sizeof(resp));
    assert(resp_len == 7);
    assert(resp[1] == 1);
    assert(resp[2] == DAP_TRANSFER_OK);
    uint32_t ap_idr = resp[3] | ((uint32_t)resp[4] << 8) |
                      ((uint32_t)resp[5] << 16) | ((uint32_t)resp[6] << 24);
    assert(ap_idr == 0x04770031); // Standard Cortex-M AP IDR

    printf("PASS\n");
}

// Test 4: SWD Block Transfer - SRAM Memory Write and Readback
static void test_cmsis_dap_block_transfer(void) {
    printf("[4] Testing SWD Block Transfer (SRAM Memory R/W)... ");
    uint8_t req[64];
    uint8_t resp[64];
    uint32_t resp_len;

    // 1. Set AP TAR to 0x20000000 (APnDP=1, RnW=0, reg=0x04 -> req_hdr = 0x05)
    req[0] = DAP_ID_TRANSFER;
    req[1] = 0;
    req[2] = 1;
    req[3] = 0x05;
    uint32_t sram_base = 0x20000000;
    memcpy(&req[4], &sram_base, 4);
    resp_len = cmsis_dap_process_command(req, 8, resp, sizeof(resp));
    assert(resp[1] == 1);
    assert(resp[2] == DAP_TRANSFER_OK);

    // 2. Block write 4 words to SRAM via AP DRW (APnDP=1, RnW=0, reg=0x08 -> req_hdr = 0x09)
    req[0] = DAP_ID_TRANSFER_BLOCK;
    req[1] = 0; // DAP Index
    req[2] = 4; // Count low
    req[3] = 0; // Count high
    req[4] = 0x09; // AP DRW Write
    uint32_t test_data[4] = { 0xDEADBEEF, 0xCAFEBABE, 0x12345678, 0x87654321 };
    memcpy(&req[5], test_data, sizeof(test_data));
    resp_len = cmsis_dap_process_command(req, 5 + 16, resp, sizeof(resp));
    assert(resp_len == 4);
    assert(resp[0] == DAP_ID_TRANSFER_BLOCK);
    uint16_t written = resp[1] | ((uint16_t)resp[2] << 8);
    assert(written == 4);
    assert(resp[3] == DAP_TRANSFER_OK);

    // 3. Reset AP TAR to 0x20000000
    req[0] = DAP_ID_TRANSFER;
    req[1] = 0;
    req[2] = 1;
    req[3] = 0x05;
    memcpy(&req[4], &sram_base, 4);
    resp_len = cmsis_dap_process_command(req, 8, resp, sizeof(resp));
    assert(resp[1] == 1);
    assert(resp[2] == DAP_TRANSFER_OK);

    // 4. Block read 4 words back via AP DRW (APnDP=1, RnW=1, reg=0x08 -> req_hdr = 0x0B)
    req[0] = DAP_ID_TRANSFER_BLOCK;
    req[1] = 0;
    req[2] = 4;
    req[3] = 0;
    req[4] = 0x0B; // AP DRW Read
    resp_len = cmsis_dap_process_command(req, 5, resp, sizeof(resp));
    assert(resp_len == 4 + 16);
    assert(resp[0] == DAP_ID_TRANSFER_BLOCK);
    uint16_t read_cnt = resp[1] | ((uint16_t)resp[2] << 8);
    assert(read_cnt == 4);
    assert(resp[3] == DAP_TRANSFER_OK);

    uint32_t rcv_data[4];
    memcpy(rcv_data, &resp[4], sizeof(rcv_data));
    for (int i = 0; i < 4; i++) {
        assert(rcv_data[i] == test_data[i]);
    }

    printf("PASS\n");
}

// Test 5: CMSIS-DAP Disconnect & Pin Restoration
static void test_cmsis_dap_disconnect(void) {
    printf("[5] Testing CMSIS-DAP Disconnect... ");
    uint8_t req[8];
    uint8_t resp[16];
    uint32_t resp_len;

    req[0] = DAP_ID_DISCONNECT;
    resp_len = cmsis_dap_process_command(req, 1, resp, sizeof(resp));
    assert(resp_len == 2);
    assert(resp[0] == DAP_ID_DISCONNECT);
    assert(resp[1] == DAP_OK);
    assert(!cmsis_dap_is_connected());

    printf("PASS\n");
}

// Test 6: HSTX DVI Video Subsystem Init & Terminal Operations
static void test_hstx_dvi_init_and_text(void) {
    printf("[6] Testing HSTX DVI Init, Color & Terminal Operations... ");
    assert(hstx_dvi_init());
    assert(hstx_dvi_is_supported());
    assert(!hstx_dvi_is_enabled());

    hstx_dvi_enable(true);
    assert(hstx_dvi_is_enabled());

    // Check clear
    hstx_dvi_clear();
    const char *text_buf = hstx_dvi_get_text_buffer();
    for (int i = 0; i < HSTX_DVI_TEXT_ROWS * HSTX_DVI_TEXT_COLS; i++) {
        assert(text_buf[i] == ' ');
    }

    // Cursor positioning and text output
    hstx_dvi_set_cursor(5, 2);
    hstx_dvi_set_color(HSTX_COLOR_BRIGHT_GREEN, HSTX_COLOR_BLACK);
    hstx_dvi_write_string("Hello DVI");

    // Check that row 2, col 5 contains "Hello DVI"
    const char *row2 = text_buf + (2 * HSTX_DVI_TEXT_COLS);
    assert(memcmp(&row2[5], "Hello DVI", 9) == 0);

    // Escape character handling: \r, \n, \t, \b
    hstx_dvi_set_cursor(0, 5);
    hstx_dvi_write_string("AB\bC"); // Backspace replaces 'B' with 'C'
    const char *row5 = text_buf + (5 * HSTX_DVI_TEXT_COLS);
    assert(row5[0] == 'A');
    assert(row5[1] == 'C');

    // Tab handling
    hstx_dvi_set_cursor(0, 6);
    hstx_dvi_write_string("X\tY");
    const char *row6 = text_buf + (6 * HSTX_DVI_TEXT_COLS);
    assert(row6[0] == 'X');
    assert(row6[8] == 'Y'); // Tab stops at column 8

    printf("PASS\n");
}

// Test 7: HSTX DVI Terminal Scrolling
static void test_hstx_dvi_scrolling(void) {
    printf("[7] Testing HSTX DVI Terminal Scrolling... ");
    hstx_dvi_clear();

    // Write 34 newline-terminated lines: Line 00 through Line 33
    for (int i = 0; i < 34; i++) {
        char line[32];
        snprintf(line, sizeof(line), "Line %02d\r\n", i);
        hstx_dvi_write_string(line);
    }
    // Write line 34 without trailing newline so it stays on row 29
    hstx_dvi_write_string("Line 34");

    const char *text_buf = hstx_dvi_get_text_buffer();
    // After 35 lines, rows 0-4 should have scrolled off
    // Row 0 should be "Line 05"
    const char *row0 = text_buf;
    assert(memcmp(row0, "Line 05", 7) == 0);

    // Row 29 (bottom row) should be "Line 34"
    const char *row29 = text_buf + (29 * HSTX_DVI_TEXT_COLS);
    assert(memcmp(row29, "Line 34", 7) == 0);

    printf("PASS\n");
}

// Test 8: HSTX DVI Scanline Pixel Rendering and Font Expansion
static void test_hstx_dvi_scanline_rendering(void) {
    printf("[8] Testing HSTX DVI Scanline Pixel Rendering... ");
    hstx_dvi_clear();
    hstx_dvi_set_cursor(0, 0);
    hstx_dvi_set_color(HSTX_COLOR_BRIGHT_RED, HSTX_COLOR_BLACK);
    hstx_dvi_write_char('E');

    uint8_t line_buf[HSTX_DVI_WIDTH];
    const uint8_t *glyph_E = font8x16_get_glyph('E');

    // Render scanlines 0 through 15 (row 0 glyph)
    for (uint16_t y = 0; y < 16; y++) {
        memset(line_buf, 0xAA, sizeof(line_buf));
        hstx_dvi_render_scanline(y, line_buf);

        uint8_t expected_row_bits = glyph_E[y];
        for (uint8_t b = 0; b < 8; b++) {
            bool bit_set = (expected_row_bits & (0x80 >> b)) != 0;
            uint8_t expected_pixel = bit_set ? HSTX_COLOR_BRIGHT_RED : HSTX_COLOR_BLACK;
            assert(line_buf[b] == expected_pixel);
        }
        // Columns 1..79 are space characters (' '), so their pixels should be HSTX_COLOR_BLACK
        for (uint16_t px = 8; px < HSTX_DVI_WIDTH; px++) {
            assert(line_buf[px] == HSTX_COLOR_BLACK);
        }
    }

    printf("PASS\n");
}

// Test 9: HSTX DVI Background Service & Test Pattern
static void test_hstx_dvi_service_and_pattern(void) {
    printf("[9] Testing HSTX DVI DMA Service & Test Pattern... ");
    hstx_dvi_enable(true);
    hstx_dvi_test_pattern();

    const char *text_buf = hstx_dvi_get_text_buffer();
    assert(strstr(text_buf, "RP2350 HSTX DVI") != NULL);
    assert(strstr(text_buf, "Color Palette") != NULL);

    // Call service across full screen frame (480 lines) to verify scanline rollover
    for (int frame = 0; frame < 500; frame++) {
        hstx_dvi_service();
    }

    // Disable video
    hstx_dvi_enable(false);
    assert(!hstx_dvi_is_enabled());

    printf("PASS\n");
}

static void test_wait_retry_budget(void) {
    uint8_t response[32];
    uint8_t config[] = { DAP_ID_TRANSFER_CONFIGURE, 0, 2, 0, 0, 0 };
    assert(cmsis_dap_process_command(config, sizeof(config), response, sizeof(response)) == 2);
    uint8_t read[] = { DAP_ID_TRANSFER, 0, 1, 2 };
    cmsis_dap_test_ack(DAP_TRANSFER_WAIT, 2);
    assert(cmsis_dap_process_command(read, sizeof(read), response, sizeof(response)) == 7);
    assert(response[1] == 1 && response[2] == DAP_TRANSFER_OK);
    cmsis_dap_test_ack(DAP_TRANSFER_WAIT, 3);
    assert(cmsis_dap_process_command(read, sizeof(read), response, sizeof(response)) == 3);
    assert(response[1] == 0 && response[2] == DAP_TRANSFER_WAIT);
    cmsis_dap_test_ack(DAP_TRANSFER_FAULT, 1);
    assert(cmsis_dap_process_command(read, sizeof(read), response, sizeof(response)) == 3);
    assert(response[1] == 0 && response[2] == DAP_TRANSFER_FAULT);
}

static void test_response_capacity(void) {
    uint8_t response[64];
    const uint8_t ids[] = { DAP_INFO_VENDOR, DAP_INFO_PRODUCT, DAP_INFO_SERNUM,
                           DAP_INFO_FW_VER, DAP_INFO_UART_BUF_SIZE };
    for (unsigned i = 0; i < sizeof(ids); ++i) {
        uint8_t req[] = { DAP_ID_INFO, ids[i] };
        uint32_t required = cmsis_dap_process_command(req, sizeof(req), response, sizeof(response));
        assert(required > 4);
        for (uint32_t capacity = 4; capacity < required; ++capacity) {
            memset(response, 0xA5, sizeof(response));
            assert(cmsis_dap_process_command(req, sizeof(req), response, capacity) == 0);
            for (unsigned j = capacity; j < sizeof(response); ++j) assert(response[j] == 0xA5);
        }
    }
    uint8_t req[] = { DAP_ID_TRANSFER_BLOCK, 0, 0xFF, 0xFF, 0x02 };
    assert(cmsis_dap_process_command(req, sizeof(req), response, 4) == 4);
    assert(response[1] == 0 && response[2] == 0 && response[3] == DAP_TRANSFER_ERROR);
    uint8_t sequence[34] = { DAP_ID_SWJ_SEQUENCE, 0 };
    assert(cmsis_dap_process_command(sequence, 10, response, sizeof(response)) == 2);
    assert(response[1] == DAP_ERROR);
    assert(cmsis_dap_process_command(sequence, sizeof(sequence), response, sizeof(response)) == 2);
    assert(response[1] == DAP_OK);
}

int main(void) {
    test_wait_retry_budget();
    test_response_capacity();
    printf("==============================================================================\n");
    printf(" RB-Pirate CMSIS-DAP v2 & HSTX DVI Video Unified Conformance Test Suite\n");
    printf("==============================================================================\n");

    test_cmsis_dap_info();
    test_cmsis_dap_connect_and_config();
    test_cmsis_dap_transfer_dp_ap();
    test_cmsis_dap_block_transfer();
    test_cmsis_dap_disconnect();
    test_hstx_dvi_init_and_text();
    test_hstx_dvi_scrolling();
    test_hstx_dvi_scanline_rendering();
    test_hstx_dvi_service_and_pattern();

    printf("==============================================================================\n");
    printf(" ALL CMSIS-DAP & HSTX DVI TESTS PASSED!\n");
    printf("==============================================================================\n");
    return 0;
}
