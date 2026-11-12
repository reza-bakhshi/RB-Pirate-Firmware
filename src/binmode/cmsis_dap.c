/**
 * @file cmsis_dap.c
 * @brief Implementation of ARM CMSIS-DAP v2 Serial Wire Debug (SWD) probe.
 */

#include "binmode/cmsis_dap.h"
#include "pico/stdlib.h"
#include <string.h>
/* Modified by Codex: enforce response capacity before target side effects. */
#include <stdio.h>
#include "pirate.h"
#include "pirate/bio.h"
#include "system_config.h"
#include "ui/ui_term.h"

#if __has_include("tusb.h") && !defined(BP_HOST_TEST)
#include "tusb.h"
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#endif

#define SWD_PIN_CLK     BIO0
#define SWD_PIN_DIO     BIO1
#define SWD_PIN_RESET   BIO2

typedef struct {
    bool        connected;
    uint8_t     port;
    uint8_t     idle_cycles;
    uint16_t    retry_count;
    uint16_t    match_retry;
    uint32_t    clock_hz;
    uint32_t    clock_delay_cycles;
    uint8_t     turnaround_cycles;
    bool        data_phase;
    uint32_t    match_mask;
} dap_state_t;

static dap_state_t s_dap = {
    .connected          = false,
    .port               = DAP_PORT_DEFAULT,
    .idle_cycles        = 0,
    .retry_count        = 100,
    .match_retry        = 0,
    .clock_hz           = 1000000, // 1 MHz default
    .clock_delay_cycles = 10,
    .turnaround_cycles  = 1,
    .data_phase         = false,
    .match_mask         = 0xFFFFFFFF
};

// Static 1024-byte packet buffers (Zero runtime heap allocation)
static uint8_t s_dap_req[CMSIS_DAP_MAX_PACKET_SIZE];
static uint8_t s_dap_resp[CMSIS_DAP_MAX_PACKET_SIZE];

#if defined(BP_HOST_TEST)
// Mock ARM Cortex target state for host test suite
static struct {
    uint32_t dp_idcode;
    uint32_t dp_ctrl_stat;
    uint32_t dp_select;
    uint32_t ap_csw;
    uint32_t ap_tar;
    uint32_t ap_idr;
    uint32_t sram[256];
} s_mock_target = {
    .dp_idcode    = 0x0BF11477, // ARM Cortex-M0+ DP IDCODE
    .dp_ctrl_stat = 0xF0000000, // Powered up
    .dp_select    = 0x00000000,
    .ap_csw       = 0x23000012,
    .ap_tar       = 0x20000000,
    .ap_idr       = 0x04770031, // Standard Cortex-M AP IDR
    .sram         = { 0 }
};
#endif

static inline void swd_delay(void) {
#if !defined(BP_HOST_TEST)
    if (s_dap.clock_delay_cycles > 0) {
        busy_wait_at_least_cycles(s_dap.clock_delay_cycles);
    }
#endif
}

static inline void swd_clock_low(void) {
    bio_put(SWD_PIN_CLK, false);
}

static inline void swd_clock_high(void) {
    bio_put(SWD_PIN_CLK, true);
}

static void swd_write_bit(bool bit) {
    bio_put(SWD_PIN_DIO, bit);
    swd_delay();
    swd_clock_low();
    swd_delay();
    swd_clock_high();
}

static bool swd_read_bit(void) {
    swd_delay();
    swd_clock_low();
    swd_delay();
    bool bit = bio_get(SWD_PIN_DIO);
    swd_clock_high();
    return bit;
}

static void swd_turnaround(bool to_read) {
    if (to_read) {
        bio_input(SWD_PIN_DIO);
    } else {
        bio_output(SWD_PIN_DIO);
    }
    for (uint8_t i = 0; i < s_dap.turnaround_cycles; i++) {
        swd_delay();
        swd_clock_low();
        swd_delay();
        swd_clock_high();
    }
}

static void swd_write_byte(uint8_t val) {
    for (uint8_t i = 0; i < 8; i++) {
        swd_write_bit((val >> i) & 1);
    }
}

#if defined(BP_HOST_TEST)
#define DAP_UNUSED __attribute__((unused))
#else
#define DAP_UNUSED
#endif

DAP_UNUSED static uint8_t swd_read_byte(void) {
    uint8_t val = 0;
    for (uint8_t i = 0; i < 8; i++) {
        if (swd_read_bit()) {
            val |= (1 << i);
        }
    }
    return val;
}

static uint8_t swd_calc_parity(uint32_t val) {
    uint32_t p = val ^ (val >> 16);
    p ^= (p >> 8);
    p ^= (p >> 4);
    p ^= (p >> 2);
    p ^= (p >> 1);
    return (uint8_t)(p & 1);
}

DAP_UNUSED static uint8_t swd_transfer_hardware(uint8_t req, uint32_t *data) {
    // 1. Send 8-bit request header
    uint8_t ap_ndp = (req >> 0) & 1;
    uint8_t r_nw   = (req >> 1) & 1;
    uint8_t a2     = (req >> 2) & 1;
    uint8_t a3     = (req >> 3) & 1;
    uint8_t parity = ap_ndp ^ r_nw ^ a2 ^ a3;

    uint8_t header = (1 << 0) | (ap_ndp << 1) | (r_nw << 2) |
                     (a2 << 3) | (a3 << 4) | (parity << 5) |
                     (0 << 6) | (1 << 7);

    bio_output(SWD_PIN_DIO);
    swd_write_byte(header);

    // 2. Turnaround to input
    swd_turnaround(true);

    // 3. Read 3-bit ACK
    uint8_t ack = 0;
    for (uint8_t i = 0; i < 3; i++) {
        if (swd_read_bit()) {
            ack |= (1 << i);
        }
    }

    if (ack != DAP_TRANSFER_OK) {
        // Target returned WAIT (2), FAULT (4), or no response
        if (s_dap.data_phase) {
            for (uint8_t i = 0; i < 33; i++) {
                swd_read_bit();
            }
        }
        swd_turnaround(false);
        bio_put(SWD_PIN_DIO, true);
        return ack;
    }

    // 4. Data phase
    if (r_nw) {
        // Read 32 bits data + 1 parity bit
        uint32_t val = 0;
        for (uint8_t i = 0; i < 32; i++) {
            if (swd_read_bit()) {
                val |= (1UL << i);
            }
        }
        bool read_parity = swd_read_bit();
        swd_turnaround(false);

        if (swd_calc_parity(val) != read_parity) {
            ack = DAP_TRANSFER_ERROR;
        } else {
            if (data) *data = val;
        }
    } else {
        // Write 32 bits data + 1 parity bit
        swd_turnaround(false);
        uint32_t val = data ? *data : 0;
        for (uint8_t i = 0; i < 32; i++) {
            swd_write_bit((val >> i) & 1);
        }
        swd_write_bit(swd_calc_parity(val));
    }

    // 5. Idle cycles
    bio_output(SWD_PIN_DIO);
    bio_put(SWD_PIN_DIO, false);
    for (uint8_t i = 0; i < s_dap.idle_cycles; i++) {
        swd_delay();
        swd_clock_low();
        swd_delay();
        swd_clock_high();
    }
    bio_put(SWD_PIN_DIO, true);

    return ack;
}

#if defined(BP_HOST_TEST)
static uint8_t s_test_ack;
static uint32_t s_test_ack_remaining;
void cmsis_dap_test_ack(uint8_t ack, uint32_t count) {
    s_test_ack = ack;
    s_test_ack_remaining = count;
}
#endif

static uint8_t swd_transfer_once(uint8_t req, uint32_t *data) {
#if defined(BP_HOST_TEST)
    if (s_test_ack_remaining) { s_test_ack_remaining--; return s_test_ack; }
    // Host test mock engine
    uint8_t ap_ndp = (req >> 0) & 1;
    uint8_t r_nw   = (req >> 1) & 1;
    uint8_t reg    = ((req >> 2) & 0x3) << 2;

    if (ap_ndp == 0) { // DP access
        if (r_nw) { // Read
            if (reg == 0x00) *data = s_mock_target.dp_idcode;
            else if (reg == 0x04) *data = s_mock_target.dp_ctrl_stat;
            else if (reg == 0x0C) *data = 0; // RDBUFF
            else *data = 0;
        } else { // Write
            if (reg == 0x04) s_mock_target.dp_ctrl_stat = *data;
            else if (reg == 0x08) s_mock_target.dp_select = *data;
        }
    } else { // AP access
        if (r_nw) { // Read
            if (reg == 0x00) *data = s_mock_target.ap_csw;
            else if (reg == 0x04) *data = s_mock_target.ap_tar;
            else if (reg == 0x0C) *data = s_mock_target.ap_idr;
            else if (reg == 0x08) { // DRW read
                uint32_t idx = (s_mock_target.ap_tar - 0x20000000) >> 2;
                if (idx < 256) *data = s_mock_target.sram[idx];
                else *data = 0xAA55AA55;
                s_mock_target.ap_tar += 4;
            } else *data = 0;
        } else { // Write
            if (reg == 0x00) s_mock_target.ap_csw = *data;
            else if (reg == 0x04) s_mock_target.ap_tar = *data;
            else if (reg == 0x08) { // DRW write
                uint32_t idx = (s_mock_target.ap_tar - 0x20000000) >> 2;
                if (idx < 256) s_mock_target.sram[idx] = *data;
                s_mock_target.ap_tar += 4;
            }
        }
    }
    return DAP_TRANSFER_OK;
#else
    return swd_transfer_hardware(req, data);
#endif
}

/* WAIT is retryable; FAULT/parity/protocol errors must be reported to the host.
 * Limit both retry count and elapsed time so a dead target cannot monopolize USB. */
static uint8_t swd_transfer(uint8_t req, uint32_t *data) {
    uint32_t start = time_us_32();
    for (uint32_t attempt = 0; ; ++attempt) {
        uint8_t ack = swd_transfer_once(req, data);
        if (ack != DAP_TRANSFER_WAIT || attempt >= s_dap.retry_count ||
            (uint32_t)(time_us_32() - start) >= 100000) return ack;
    }
}

void cmsis_dap_init(void) {
    s_dap.connected = false;
    s_dap.port = DAP_PORT_DEFAULT;
    s_dap.idle_cycles = 0;
    s_dap.retry_count = 100;
    s_dap.match_retry = 0;
    s_dap.clock_hz = 1000000;
    s_dap.clock_delay_cycles = 10;
    s_dap.turnaround_cycles = 1;
    s_dap.data_phase = false;
    s_dap.match_mask = 0xFFFFFFFF;
}

void cmsis_dap_setup(void) {
    cmsis_dap_init();
    system_bio_update_purpose_and_label(true, SWD_PIN_CLK, BP_PIN_MODE, "SWCLK");
    system_bio_update_purpose_and_label(true, SWD_PIN_DIO, BP_PIN_MODE, "SWDIO");
    system_bio_update_purpose_and_label(true, SWD_PIN_RESET, BP_PIN_MODE, "nRESET");

    bio_output(SWD_PIN_CLK);
    bio_put(SWD_PIN_CLK, true);

    bio_output(SWD_PIN_DIO);
    bio_put(SWD_PIN_DIO, true);

    bio_output(SWD_PIN_RESET);
    bio_put(SWD_PIN_RESET, true);

    s_dap.connected = true;
    s_dap.port = DAP_PORT_SWD;
}

void cmsis_dap_cleanup(void) {
    bio_input(SWD_PIN_CLK);
    bio_input(SWD_PIN_DIO);
    bio_input(SWD_PIN_RESET);

    system_bio_update_purpose_and_label(false, SWD_PIN_CLK, BP_PIN_MODE, 0);
    system_bio_update_purpose_and_label(false, SWD_PIN_DIO, BP_PIN_MODE, 0);
    system_bio_update_purpose_and_label(false, SWD_PIN_RESET, BP_PIN_MODE, 0);

    bio_init();
    s_dap.connected = false;
}

bool cmsis_dap_is_connected(void) {
    return s_dap.connected;
}

const char cmsis_dap_name[] = "CMSIS-DAP v2 SWD Probe";

const char* cmsis_dap_mode_name(void) {
    return cmsis_dap_name;
}

uint32_t cmsis_dap_process_command(const uint8_t *req, uint32_t req_len,
                                   uint8_t *resp, uint32_t max_resp_len) {
    if (!req || req_len == 0 || !resp || max_resp_len < 4) {
        return 0;
    }

    uint8_t cmd = req[0];
    uint32_t resp_len = 0;
    resp[0] = cmd; // Echo command byte

    switch (cmd) {
        case DAP_ID_INFO: {
            if (req_len < 2) {
                resp[1] = 0;
                resp_len = 2;
                break;
            }
            uint8_t info_id = req[1];
            switch (info_id) {
                case DAP_INFO_VENDOR: {
                    const char vendor[] = "Bus Pirate";
                    uint8_t len = sizeof(vendor); // includes null terminator
                    if ((uint32_t)len + 2 > max_resp_len) return 0;
                    resp[1] = len;
                    memcpy(&resp[2], vendor, len);
                    resp_len = 2 + len;
                    break;
                }
                case DAP_INFO_PRODUCT: {
                    const char product[] = "RB-Pirate CMSIS-DAP";
                    uint8_t len = sizeof(product);
                    if ((uint32_t)len + 2 > max_resp_len) return 0;
                    resp[1] = len;
                    memcpy(&resp[2], product, len);
                    resp_len = 2 + len;
                    break;
                }
                case DAP_INFO_SERNUM: {
                    const char ser[] = "BP6-CMSIS-DAP";
                    uint8_t len = sizeof(ser);
                    if ((uint32_t)len + 2 > max_resp_len) return 0;
                    resp[1] = len;
                    memcpy(&resp[2], ser, len);
                    resp_len = 2 + len;
                    break;
                }
                case DAP_INFO_FW_VER: {
                    const char fw[] = "2.1.1";
                    uint8_t len = sizeof(fw);
                    if ((uint32_t)len + 2 > max_resp_len) return 0;
                    resp[1] = len;
                    memcpy(&resp[2], fw, len);
                    resp_len = 2 + len;
                    break;
                }
                case DAP_INFO_CAPABILITIES: {
                    resp[1] = 1;
                    resp[2] = 0x01; // Bit 0: SWD supported
                    resp_len = 3;
                    break;
                }
                case DAP_INFO_PACKET_COUNT: {
                    resp[1] = 1;
                    resp[2] = 1; // 1 packet
                    resp_len = 3;
                    break;
                }
                case DAP_INFO_PACKET_SIZE: {
                    resp[1] = 2;
                    uint16_t sz = CMSIS_DAP_PACKET_SIZE;
                    memcpy(&resp[2], &sz, 2);
                    resp_len = 4;
                    break;
                }
                case DAP_INFO_UART_BUF_SIZE: {
                    if (max_resp_len < 6) return 0;
                    resp[1] = 4;
                    memset(&resp[2], 0, 4);
                    resp_len = 6;
                    break;
                }
                case 0xFB: // Test Domain Timer
                case 0xFC: // UART streaming
                default: {
                    resp[1] = 0; // Unsupported info
                    resp_len = 2;
                    break;
                }
            }
            break;
        }

        case DAP_ID_HOST_STATUS: {
            resp[1] = DAP_OK;
            resp_len = 2;
            break;
        }

        case DAP_ID_CONNECT: {
            uint8_t port = (req_len > 1) ? req[1] : DAP_PORT_DEFAULT;
            if (port == DAP_PORT_DEFAULT || port == DAP_PORT_SWD) {
                cmsis_dap_setup();
                resp[1] = DAP_PORT_SWD;
            } else {
                resp[1] = 0; // Failed
            }
            resp_len = 2;
            break;
        }

        case DAP_ID_DISCONNECT: {
            cmsis_dap_cleanup();
            resp[1] = DAP_OK;
            resp_len = 2;
            break;
        }

        case DAP_ID_TRANSFER_CONFIGURE: {
            if (req_len >= 6) {
                s_dap.idle_cycles = req[1];
                s_dap.retry_count = req[2] | ((uint16_t)req[3] << 8);
                s_dap.match_retry = req[4] | ((uint16_t)req[5] << 8);
                resp[1] = DAP_OK;
            } else {
                resp[1] = DAP_ERROR;
            }
            resp_len = 2;
            break;
        }

        case DAP_ID_SWJ_CLOCK: {
            if (req_len >= 5) {
                uint32_t clk = req[1] | ((uint32_t)req[2] << 8) |
                               ((uint32_t)req[3] << 16) | ((uint32_t)req[4] << 24);
                if (clk > 0) {
                    s_dap.clock_hz = clk;
#if !defined(BP_HOST_TEST)
                    uint32_t sys_hz = clock_get_hz(clk_sys);
                    s_dap.clock_delay_cycles = sys_hz / (clk * 2);
#else
                    s_dap.clock_delay_cycles = 10;
#endif
                }
                resp[1] = DAP_OK;
            } else {
                resp[1] = DAP_ERROR;
            }
            resp_len = 2;
            break;
        }

        case DAP_ID_SWJ_PINS: {
            if (req_len >= 7) {
                uint8_t val = req[1];
                uint8_t sel = req[2];
                if (sel & 0x01) bio_put(SWD_PIN_CLK, (val & 0x01) != 0);
                if (sel & 0x02) bio_put(SWD_PIN_DIO, (val & 0x02) != 0);
                if (sel & 0x80) bio_put(SWD_PIN_RESET, (val & 0x80) != 0);

                uint8_t in = 0;
                if (bio_get(SWD_PIN_CLK)) in |= 0x01;
                if (bio_get(SWD_PIN_DIO)) in |= 0x02;
                if (bio_get(SWD_PIN_RESET)) in |= 0x80;

                resp[1] = in;
                resp_len = 2;
            } else {
                resp[1] = 0;
                resp_len = 2;
            }
            break;
        }

        case DAP_ID_SWJ_SEQUENCE: {
            if (req_len >= 2) {
                uint16_t bit_cnt = req[1] ? req[1] : 256;
                uint8_t byte_cnt = (bit_cnt + 7) / 8;
                if (req_len >= (uint32_t)(2 + byte_cnt)) {
                    bio_output(SWD_PIN_DIO);
                    for (uint16_t i = 0; i < bit_cnt; i++) {
                        bool b = (req[2 + (i / 8)] >> (i % 8)) & 1;
                        swd_write_bit(b);
                    }
                    bio_put(SWD_PIN_DIO, true);
                    resp[1] = DAP_OK;
                } else {
                    resp[1] = DAP_ERROR;
                }
            } else {
                resp[1] = DAP_ERROR;
            }
            resp_len = 2;
            break;
        }

        case DAP_ID_SWD_CONFIGURE: {
            if (req_len >= 2) {
                s_dap.turnaround_cycles = (req[1] & 0x03) + 1;
                s_dap.data_phase = (req[1] & 0x04) != 0;
                resp[1] = DAP_OK;
            } else {
                resp[1] = DAP_ERROR;
            }
            resp_len = 2;
            break;
        }

        case DAP_ID_RESET_TARGET: {
            bio_output(SWD_PIN_RESET);
            bio_put(SWD_PIN_RESET, false);
#if !defined(BP_HOST_TEST)
            sleep_ms(10);
#endif
            bio_put(SWD_PIN_RESET, true);
            resp[1] = DAP_OK;
            resp[2] = 0; // No execute reset
            resp_len = 3;
            break;
        }

        case DAP_ID_TRANSFER: {
            if (req_len < 3) {
                resp[1] = 0;
                resp[2] = DAP_TRANSFER_ERROR;
                resp_len = 3;
                break;
            }

            uint8_t transfer_cnt = req[2];
            uint32_t req_idx = 3;
            uint32_t resp_idx = 3;
            uint8_t executed = 0;
            uint8_t last_ack = DAP_TRANSFER_OK;

            for (uint8_t i = 0; i < transfer_cnt; i++) {
                if (req_idx >= req_len) break;
                uint8_t req_hdr = req[req_idx++];
                uint8_t r_nw = (req_hdr >> 1) & 1;

                if (r_nw) { // Read transfer
                    if (max_resp_len - resp_idx < 4) {
                        last_ack = DAP_TRANSFER_ERROR;
                        break;
                    }
                    uint32_t data = 0;
                    last_ack = swd_transfer(req_hdr, &data);
                    if (last_ack != DAP_TRANSFER_OK) break;
                    if (resp_idx + 4 <= max_resp_len) {
                        memcpy(&resp[resp_idx], &data, 4);
                        resp_idx += 4;
                    }
                } else { // Write transfer
                    if (req_idx + 4 > req_len) break;
                    uint32_t data;
                    memcpy(&data, &req[req_idx], 4);
                    req_idx += 4;
                    last_ack = swd_transfer(req_hdr, &data);
                    if (last_ack != DAP_TRANSFER_OK) break;
                }
                executed++;
            }

            resp[1] = executed;
            resp[2] = last_ack;
            resp_len = resp_idx;
            break;
        }

        case DAP_ID_TRANSFER_BLOCK: {
            if (req_len < 5) {
                resp[1] = 0;
                resp[2] = 0;
                resp[3] = DAP_TRANSFER_ERROR;
                resp_len = 4;
                break;
            }

            uint16_t block_cnt = req[2] | ((uint16_t)req[3] << 8);
            uint8_t req_hdr = req[4];
            uint8_t r_nw = (req_hdr >> 1) & 1;
            uint32_t req_idx = 5;
            uint32_t resp_idx = 4;
            uint16_t executed = 0;
            uint8_t last_ack = DAP_TRANSFER_OK;

            for (uint16_t i = 0; i < block_cnt; i++) {
                if (r_nw) { // Block Read
                    if (max_resp_len - resp_idx < 4) {
                        last_ack = DAP_TRANSFER_ERROR;
                        break;
                    }
                    uint32_t data = 0;
                    last_ack = swd_transfer(req_hdr, &data);
                    if (last_ack != DAP_TRANSFER_OK) break;
                    if (resp_idx + 4 <= max_resp_len) {
                        memcpy(&resp[resp_idx], &data, 4);
                        resp_idx += 4;
                    }
                } else { // Block Write
                    if (req_idx + 4 > req_len) break;
                    uint32_t data;
                    memcpy(&data, &req[req_idx], 4);
                    req_idx += 4;
                    last_ack = swd_transfer(req_hdr, &data);
                    if (last_ack != DAP_TRANSFER_OK) break;
                }
                executed++;
            }

            resp[1] = (uint8_t)(executed & 0xFF);
            resp[2] = (uint8_t)(executed >> 8);
            resp[3] = last_ack;
            resp_len = resp_idx;
            break;
        }

        case DAP_ID_TRANSFER_ABORT:
        case DAP_ID_WRITE_ABORT: {
            resp[1] = DAP_OK;
            resp_len = 2;
            break;
        }

        case DAP_ID_DELAY: {
            if (req_len >= 3) {
                uint16_t us = req[1] | ((uint16_t)req[2] << 8);
#if !defined(BP_HOST_TEST)
                sleep_us(us);
#else
                (void)us;
#endif
                resp[1] = DAP_OK;
            } else {
                resp[1] = DAP_ERROR;
            }
            resp_len = 2;
            break;
        }

        default: {
            resp[0] = 0xFF; // Unknown command
            resp_len = 1;
            break;
        }
    }

    return resp_len;
}

void cmsis_dap_service(void) {
#if __has_include("tusb.h") && !defined(BP_HOST_TEST)
    if (!tud_vendor_available()) {
        return;
    }

    uint32_t count = tud_vendor_read(s_dap_req, sizeof(s_dap_req));
    if (count == 0) {
        return;
    }

    uint32_t resp_len = cmsis_dap_process_command(s_dap_req, count,
                                                  s_dap_resp, sizeof(s_dap_resp));
    if (resp_len > 0) {
        tud_vendor_write(s_dap_resp, resp_len);
        tud_vendor_flush();
    }
#endif
}
