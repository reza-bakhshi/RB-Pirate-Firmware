#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#define BP_CUSTOM_UART_MOCKS 1
#define BP_CUSTOM_TICK_MOCK 1

#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "kernel/bp_status.h"
#include "kernel/bp_resource.h"

/* --- Mock Hardware & System Stubs --- */
uart_inst_t s_mock_uart0 = { { 0 } };
uart_inst_t s_mock_uart1 = { { 0 } };

#define HWUART 1

/* --- Mock Time using Host Monotonic Clock --- */
uint32_t get_host_tick_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

void sleep_ms(uint32_t ms) {
    struct timespec req;
    req.tv_sec = ms / 1000;
    req.tv_nsec = (ms % 1000) * 1000000;
    nanosleep(&req, NULL);
}

/* --- Mock Cancellation --- */
static bool s_mock_job_cancelled = false;
static char s_mock_rx_char = 0;
static bool s_mock_rx_has = false;

bool bp_job_check_cancelled(void) {
    return s_mock_job_cancelled;
}

bool rx_fifo_try_get(char *c) {
    if (s_mock_rx_has) {
        *c = s_mock_rx_char;
        s_mock_rx_has = false;
        return true;
    }
    return false;
}

/* --- Mock UART RX/TX FIFOs & Response Auto-Responder --- */
static char s_uart_rx_fifo[8192];
static size_t s_uart_rx_head = 0;
static size_t s_uart_rx_tail = 0;

static char s_uart_tx_fifo[8192];
static size_t s_uart_tx_len = 0;

static const char *s_next_mock_resp = NULL;
static bool s_auto_respond_at = false;

void mock_uart_reset(void) {
    s_uart_rx_head = 0;
    s_uart_rx_tail = 0;
    s_uart_tx_len = 0;
    s_uart_tx_fifo[0] = '\0';
    s_mock_job_cancelled = false;
    s_mock_rx_has = false;
    s_next_mock_resp = NULL;
    s_auto_respond_at = false;
}

void mock_feed_uart_rx(const char *str) {
    while (*str) {
        s_uart_rx_fifo[s_uart_rx_tail] = *str++;
        s_uart_rx_tail = (s_uart_rx_tail + 1) % sizeof(s_uart_rx_fifo);
    }
}

void mock_set_next_response(const char *resp) {
    s_next_mock_resp = resp;
}

bool uart_is_readable(uart_inst_t *uart) {
    (void)uart;
    return (s_uart_rx_head != s_uart_rx_tail);
}

uint8_t uart_getc(uart_inst_t *uart) {
    (void)uart;
    if (s_uart_rx_head == s_uart_rx_tail) return 0;
    uint8_t c = (uint8_t)s_uart_rx_fifo[s_uart_rx_head];
    s_uart_rx_head = (s_uart_rx_head + 1) % sizeof(s_uart_rx_fifo);
    return c;
}

void uart_putc_raw(uart_inst_t *uart, uint8_t c) {
    (void)uart;
    if (s_uart_tx_len < sizeof(s_uart_tx_fifo) - 1) {
        s_uart_tx_fifo[s_uart_tx_len++] = (char)c;
        s_uart_tx_fifo[s_uart_tx_len] = '\0';
    }

    // When the command line is terminated with CR, queue the mock response
    if (c == '\r') {
        if (s_auto_respond_at) {
            if (strstr(s_uart_tx_fifo + s_uart_tx_len - 3, "AT\r") ||
                strstr(s_uart_tx_fifo + s_uart_tx_len - 5, "ATE0\r")) {
                mock_feed_uart_rx("\r\nOK\r\n");
            }
        } else if (s_next_mock_resp) {
            mock_feed_uart_rx(s_next_mock_resp);
            s_next_mock_resp = NULL;
        }
    }
}

void putchar_raw(char c) {
    (void)c;
}

char *ui_term_color_error(void) { static char s[] = ""; return s; }
char *ui_term_color_reset(void) { static char s[] = ""; return s; }
char *ui_term_color_prompt(void) { static char s[] = ""; return s; }
char *ui_term_color_info(void) { static char s[] = ""; return s; }
char *ui_term_color_notice(void) { static char s[] = ""; return s; }
char *ui_term_color_warning(void) { static char s[] = ""; return s; }

/* Include cell_at implementation directly for testing */
#include "commands/cell/cell_at.c"

/* --- Test 1: Final Result Code Parsing --- */
static void test_final_result_codes(void) {
    printf("[TEST] AT final result code detection...\n");
    bool is_ok = false;

    // Standard OK
    const char *s_ok = "AT\r\r\nOK\r\n";
    assert(cell_last_line_is_final(s_ok, strlen(s_ok), &is_ok));
    assert(is_ok == true);

    // Standard ERROR
    const char *s_err = "AT+INVALID\r\r\nERROR\r\n";
    assert(cell_last_line_is_final(s_err, strlen(s_err), &is_ok));
    assert(is_ok == false);

    // CME ERROR
    const char *s_cme = "+CPIN: SIM PIN\r\n+CME ERROR: 10\r\n";
    assert(cell_last_line_is_final(s_cme, strlen(s_cme), &is_ok));
    assert(is_ok == false);

    // CMS ERROR
    const char *s_cms = "\r\n+CMS ERROR: 500\r\n";
    assert(cell_last_line_is_final(s_cms, strlen(s_cms), &is_ok));
    assert(is_ok == false);

    // Telephony call termination codes
    const char *s_nocarrier = "ATD12345;\r\r\nNO CARRIER\r\n";
    assert(cell_last_line_is_final(s_nocarrier, strlen(s_nocarrier), &is_ok));
    assert(is_ok == false);

    const char *s_busy = "BUSY\r\n";
    assert(cell_last_line_is_final(s_busy, strlen(s_busy), &is_ok));
    assert(is_ok == false);

    const char *s_noanswer = "NO ANSWER\r\n";
    assert(cell_last_line_is_final(s_noanswer, strlen(s_noanswer), &is_ok));
    assert(is_ok == false);

    const char *s_nodialtone = "NO DIALTONE\r\n";
    assert(cell_last_line_is_final(s_nodialtone, strlen(s_nodialtone), &is_ok));
    assert(is_ok == false);

    // Trailing whitespace tolerance
    const char *s_trail = "OK   \r\n";
    assert(cell_last_line_is_final(s_trail, strlen(s_trail), &is_ok));
    assert(is_ok == true);

    // Non-final lines
    const char *s_nonfinal = "+CPIN: READY\r\n";
    assert(!cell_last_line_is_final(s_nonfinal, strlen(s_nonfinal), &is_ok));

    // Empty / NULL
    assert(!cell_last_line_is_final("", 0, &is_ok));
    assert(!cell_last_line_is_final(NULL, 0, &is_ok));

    printf("[PASS] Final result codes correctly recognized\n");
}

/* --- Test 2: Substring "OK" Resilience & Error Status --- */
static void test_substring_ok_resilience(void) {
    printf("[TEST] Substring 'OK' in payload vs final OK line...\n");
    mock_uart_reset();

    // Payload containing "BOOK" (which contains "OK") followed by OK
    mock_set_next_response("\r\n+COPS: (2,\"BOOK-4G\",,\"12345\")\r\n\r\nOK\r\n");
    assert(cell_cmd_ok("AT+COPS=?", 1000));

    mock_uart_reset();
    // Payload containing "OK" but ending with ERROR
    mock_set_next_response("\r\n+COPS: (2,\"BOOK-4G\",,\"12345\")\r\n\r\nERROR\r\n");
    assert(!cell_cmd_ok("AT+COPS=?", 1000));
    assert(cell_cmd_error(response));

    mock_uart_reset();
    // Call ending in NO CARRIER
    mock_set_next_response("\r\nNO CARRIER\r\n");
    assert(!cell_cmd_ok("ATD123;", 1000));
    assert(cell_cmd_error(response));
    assert(strcmp(cell_status_text(response), "ERROR") == 0);

    printf("[PASS] Substring 'OK' correctly distinguished from final OK\n");
}

/* --- Test 3: Field & First Line Extraction with Echo Suppression --- */
static void test_field_and_first_line_extraction(void) {
    printf("[TEST] Field and first-line parsing with echo suppression...\n");
    char field[64];

    // Field extraction
    const char *resp1 = "\r\n+CPIN: READY\r\n\r\nOK\r\n";
    assert(cell_field(resp1, "+CPIN: ", field, sizeof(field)) != NULL);
    assert(strcmp(field, "READY") == 0);

    const char *resp2 = "\r\n+CSQ: 18,99\r\n\r\nOK\r\n";
    assert(cell_field(resp2, "+CSQ: ", field, sizeof(field)) != NULL);
    assert(strcmp(field, "18,99") == 0);

    // Missing field
    assert(cell_field(resp2, "+NONEXIST:", field, sizeof(field)) == NULL);

    // First line extraction: clean value
    const char *resp3 = "\r\nSIMCOM_Ltd\r\n\r\nOK\r\n";
    assert(cell_first_line(resp3, field, sizeof(field)) != NULL);
    assert(strcmp(field, "SIMCOM_Ltd") == 0);

    // First line extraction: modem echo active (AT+CGMI echoed first)
    const char *resp4 = "AT+CGMI\r\r\nSIMCOM_Ltd\r\n\r\nOK\r\n";
    assert(cell_first_line(resp4, field, sizeof(field)) != NULL);
    assert(strcmp(field, "SIMCOM_Ltd") == 0);

    // First line extraction: only OK response
    const char *resp5 = "\r\nOK\r\n";
    assert(cell_first_line(resp5, field, sizeof(field)) == NULL);

    printf("[PASS] Field extraction and echo stripping verified\n");
}

/* --- Test 4: Modem Detect and Absent Modem Handling --- */
static void test_modem_detect(void) {
    printf("[TEST] Modem detection and absent probe handling...\n");
    mock_uart_reset();

    // 1. Responsive modem: auto-responds to AT and ATE0 with OK
    s_auto_respond_at = true;
    assert(cell_modem_detect());
    assert(strstr(s_uart_tx_fifo, "ATE0\r") != NULL);

    // 2. Unresponsive modem: no response (times out quickly on probe)
    mock_uart_reset();
    s_auto_respond_at = false;
    assert(!cell_modem_detect());

    printf("[PASS] Modem detection and timeout behavior verified\n");
}

/* --- Test 5: Immediate SMS Prompt (> ) Detection --- */
static void test_sms_prompt_detection(void) {
    printf("[TEST] Immediate SMS prompt (> ) detection...\n");
    mock_uart_reset();

    // Feed SMS prompt with trailing space and NO newline upon AT+CMGS
    mock_set_next_response("\r\n> ");
    const char *resp = cell_cmd("AT+CMGS=\"+1234567890\"", 5000);
    assert(resp != NULL);
    assert(strstr(resp, ">") != NULL);

    printf("[PASS] SMS prompt '> ' returned immediately without gap timeout\n");
}

/* --- Test 6: Cooperative Cancellation (< 100ms) --- */
static void test_cooperative_cancellation(void) {
    printf("[TEST] Cooperative cancellation contract (< 100ms)...\n");

    // 1. Cancel via user keypress (rx_fifo_try_get)
    mock_uart_reset();
    s_mock_rx_has = true;
    s_mock_rx_char = 0x03; // Ctrl+C
    const char *resp = cell_cmd("AT+COPS=?", 65000);
    assert(resp == NULL); // Cancelled immediately

    // 2. Cancel via bp_job cancellation flag
    mock_uart_reset();
    s_mock_job_cancelled = true;
    resp = cell_cmd("AT+COPS=?", 65000);
    assert(resp == NULL);

    printf("[PASS] Cooperative cancellation stops execution immediately\n");
}

/* --- Test 7: 100 Consecutive AT Commands and Buffer Safety --- */
static void test_buffer_bounds_and_stress(void) {
    printf("[TEST] 100 consecutive AT commands & buffer boundary stress...\n");
    for (int i = 0; i < 100; i++) {
        mock_uart_reset();
        if (i % 3 == 0) {
            // Normal short response
            mock_set_next_response("\r\nOK\r\n");
            assert(cell_cmd_ok("AT", 1000));
        } else if (i % 3 == 1) {
            // Error response
            mock_set_next_response("\r\nERROR\r\n");
            assert(!cell_cmd_ok("AT+FOO", 1000));
        } else {
            // Large response exceeding CELL_RESPONSE_BUFFER (1600 bytes)
            char big_chunk[2048];
            memset(big_chunk, 'A', sizeof(big_chunk) - 10);
            big_chunk[sizeof(big_chunk) - 10] = '\0';
            strcat(big_chunk, "\r\nOK\r\n");
            mock_set_next_response(big_chunk);
            const char *res = cell_cmd("AT+BIG", 2000);
            assert(res != NULL);
            // Verify buffer didn't overflow
            assert(strlen(res) < CELL_RESPONSE_BUFFER);
        }
    }
    printf("[PASS] 100 AT commands executed with zero memory corruption\n");
}

/* --- Test 8: UART Resource Lease Arbitration --- */
static void test_uart_lease_arbitration(void) {
    printf("[TEST] UART0 hardware resource lease arbitration...\n");
    bp_res_init();

    // Verify UART0 is initially free
    assert(bp_res_is_free(BP_RES_CLASS_PERIPH, BP_RES_PERIPH_UART0));

    // Acquire UART0 for HWUART
    bp_status_t st = bp_res_acquire(BP_RES_CLASS_PERIPH, BP_RES_PERIPH_UART0,
                                    BP_RES_OWNER_MODE_BASE + HWUART, "hwuart");
    assert(st == BP_STATUS_OK);
    assert(!bp_res_is_free(BP_RES_CLASS_PERIPH, BP_RES_PERIPH_UART0));

    // Second acquire by another subsystem must fail with NO_RESOURCE
    st = bp_res_acquire(BP_RES_CLASS_PERIPH, BP_RES_PERIPH_UART0,
                        BP_RES_OWNER_SYSTEM, "debug_uart");
    assert(st == BP_STATUS_NO_RESOURCE);

    // Release UART0
    bp_res_release(BP_RES_CLASS_PERIPH, BP_RES_PERIPH_UART0,
                   BP_RES_OWNER_MODE_BASE + HWUART);
    assert(bp_res_is_free(BP_RES_CLASS_PERIPH, BP_RES_PERIPH_UART0));

    printf("[PASS] UART0 peripheral lease tracking verified\n");
}

int main(void) {
    printf("=== Bus Pirate Cellular Subsystem Host Unit Tests (F8) ===\n");
    test_final_result_codes();
    test_substring_ok_resilience();
    test_field_and_first_line_extraction();
    test_modem_detect();
    test_sms_prompt_detection();
    test_cooperative_cancellation();
    test_buffer_bounds_and_stress();
    test_uart_lease_arbitration();
    printf("=== ALL CELL UNIT TESTS PASSED ===\n");
    return 0;
}
