/**
 * @file cell_at.c
 * @brief Cellular modem AT command engine.
 * @details Port of the ESP32-Bit-Pirate CellService/CellAtProfile: a generic
 *          send/collect AT engine with final result code detection
 *          (OK / ERROR / +CME ERROR / +CMS ERROR / NO CARRIER / BUSY / NO ANSWER),
 *          plus helpers to extract response fields. All traffic goes through the
 *          hardware UART configured by UART mode (m uart).
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "pirate.h"
#include "ui/ui_term.h"
#include "usb_rx.h"
#include "kernel/bp_job.h"
#include "commands/cell/cell.h"

#define CELL_RESPONSE_BUFFER 1600u
//!< idle gap (ms) after the last received byte that ends a response read
#define CELL_IDLE_GAP 750u
//!< attempts of the "AT" probe during modem detection
#define CELL_PROBE_TRIES 3u
//!< give up waiting when not a single byte arrived after this (ms)
#define CELL_FIRST_BYTE_TIMEOUT 5000u

static char response[CELL_RESPONSE_BUFFER];

bool cell_check_cancel(void) {
    char c;
    if (rx_fifo_try_get(&c)) {
        return true;
    }
    return bp_job_check_cancelled();
}

/**
 * @brief Test the last line of a response for a final result code.
 * @details A final result code is a line containing exactly "OK", "ERROR",
 *          starting with "+CME ERROR"/"+CMS ERROR", or call termination codes
 *          ("NO CARRIER", "BUSY", "NO ANSWER", "NO DIALTONE").
 * @param buf    Response buffer
 * @param len    Response length
 * @param is_ok  Set true when the last line is "OK"
 * @return true if the last line is a final result code
 */
bool cell_last_line_is_final(const char *buf, size_t len, bool *is_ok) {
    if (!buf || !len) return false;
    // trim trailing CR/LF and spaces
    while (len && (buf[len - 1] == '\r' || buf[len - 1] == '\n' || buf[len - 1] == ' ')) len--;
    if (!len) return false;
    // find the start of the last line
    size_t start = 0;
    for (size_t i = 0; i < len; i++) {
        if (buf[i] == '\r' || buf[i] == '\n') start = i + 1;
    }
    size_t ll = len - start;
    const char *line = buf + start;
    // trim leading spaces
    while (ll && (*line == ' ')) { line++; ll--; }

    *is_ok = (ll == 2 && memcmp(line, "OK", 2) == 0);
    if (*is_ok) return true;
    if (ll >= 5 && memcmp(line, "ERROR", 5) == 0) return true;
    if (ll >= 10 && memcmp(line, "+CME ERROR", 10) == 0) return true;
    if (ll >= 10 && memcmp(line, "+CMS ERROR", 10) == 0) return true;
    if (ll >= 10 && memcmp(line, "NO CARRIER", 10) == 0) return true;
    if (ll >= 4 && memcmp(line, "BUSY", 4) == 0) return true;
    if (ll >= 9 && memcmp(line, "NO ANSWER", 9) == 0) return true;
    if (ll >= 11 && memcmp(line, "NO DIALTONE", 11) == 0) return true;
    return false;
}

static uint32_t now_ms(void) {
    return to_ms_since_boot(get_absolute_time());
}

const char *cell_cmd(const char *cmd, uint32_t timeout_ms) {
    size_t len = 0;
    bool got_byte = false;

    response[0] = '\0';

    // clear any stale bytes
    while (uart_is_readable(M_UART_PORT)) {
        (void)uart_getc(M_UART_PORT);
    }

    if (cmd && cmd[0]) {
        for (const char *p = cmd; *p; p++) {
            uart_putc_raw(M_UART_PORT, (uint8_t)*p);
        }
        uart_putc_raw(M_UART_PORT, '\r');
    }

    uint32_t start = now_ms();
    uint32_t last_byte = start;
    char line_buf[64];
    size_t line_len = 0;

    while ((now_ms() - start) < timeout_ms) {
        if (cell_check_cancel()) {
            break;
        }
        if (uart_is_readable(M_UART_PORT)) {
            char c = (char)uart_getc(M_UART_PORT);
            got_byte = true;
            last_byte = now_ms();
            if (len < (CELL_RESPONSE_BUFFER - 1)) {
                response[len++] = c;
            }

            if (c == '\r' || c == '\n') {
                if (line_len > 0) {
                    bool is_ok;
                    line_buf[line_len] = '\0';
                    if (cell_last_line_is_final(line_buf, line_len, &is_ok)) {
                        // If buffer was truncated, append final line at the end so callers see it
                        if (len >= (CELL_RESPONSE_BUFFER - 1) && line_len < 32) {
                            size_t copy_pos = (CELL_RESPONSE_BUFFER - 1) - line_len - 2;
                            response[copy_pos++] = '\r';
                            response[copy_pos++] = '\n';
                            memcpy(response + copy_pos, line_buf, line_len);
                            len = copy_pos + line_len;
                        }
                        break;
                    }
                    line_len = 0;
                }
            } else {
                if (line_len < sizeof(line_buf) - 1) {
                    line_buf[line_len++] = c;
                }
            }

            // detect SMS prompt "> " immediately
            if (len >= 2 && response[len - 2] == '>' && response[len - 1] == ' ') {
                break;
            }
            if (line_len >= 2 && line_buf[line_len - 2] == '>' && line_buf[line_len - 1] == ' ') {
                break;
            }
        } else if (got_byte) {
            // keep reading until the stream goes quiet with no final code
            if ((now_ms() - last_byte) > CELL_IDLE_GAP) break;
        } else if ((now_ms() - start) > CELL_FIRST_BYTE_TIMEOUT) {
            break; // modem silent
        }
    }

    response[len] = '\0';
    return len ? response : NULL;
}

bool cell_cmd_ok(const char *cmd, uint32_t timeout_ms) {
    const char *resp = cell_cmd(cmd, timeout_ms);
    if (!resp) return false;
    bool is_ok = false;
    if (cell_last_line_is_final(resp, strlen(resp), &is_ok)) {
        return is_ok;
    }
    return false;
}

bool cell_cmd_error(const char *resp) {
    if (!resp) return false;
    bool is_ok = false;
    if (cell_last_line_is_final(resp, strlen(resp), &is_ok)) {
        return !is_ok;
    }
    return (strstr(resp, "+CME ERROR") != NULL || strstr(resp, "+CMS ERROR") != NULL ||
            strstr(resp, "ERROR") != NULL || strstr(resp, "NO CARRIER") != NULL ||
            strstr(resp, "BUSY") != NULL || strstr(resp, "NO ANSWER") != NULL ||
            strstr(resp, "NO DIALTONE") != NULL);
}

bool cell_modem_detect(void) {
    for (uint32_t attempt = 0; attempt < CELL_PROBE_TRIES; attempt++) {
        if (cell_cmd_ok("AT", 800)) {
            // Turn off local echo so responses are clean
            (void)cell_cmd_ok("ATE0", 800);
            return true;
        }
        sleep_ms(50);
    }
    return false;
}

bool cell_sim_ready(void) {
    const char *resp = cell_cmd("AT+CPIN?", 800);
    return resp && strstr(resp, "READY");
}

bool cell_sim_puk_required(void) {
    const char *resp = cell_cmd("AT+CPIN?", 800);
    return resp && strstr(resp, "PUK");
}

/**
 * @brief Copy the text following an AT response prefix into a buffer.
 * @details Handles "+CPIN: READY" style fields; trims spaces, ignores CRLF.
 * @return buf when found, NULL otherwise.
 */
const char *cell_field(const char *resp, const char *prefix, char *buf, size_t maxlen) {
    buf[0] = '\0';
    const char *found = resp ? strstr(resp, prefix) : NULL;
    if (!found) return NULL;
    found += strlen(prefix);
    size_t i = 0;
    while (found[i] && found[i] != '\r' && found[i] != '\n' && i < (maxlen - 1)) {
        buf[i] = found[i];
        i++;
    }
    while (i && buf[i - 1] == ' ') i--;
    buf[i] = '\0';
    return buf;
}

/**
 * @brief Copy the first printable line of an AT response into a buffer.
 * @details Used for value-style responses (AT+CGMI -> "SIMCOM_Ltd",
 *          AT+CCID -> "8986...") where there is no field prefix.
 *          Skips echoed AT command lines.
 * @return buf on success, NULL when the response was empty.
 */
const char *cell_first_line(const char *resp, char *buf, size_t maxlen) {
    buf[0] = '\0';
    if (!resp || !resp[0]) return NULL;
    const char *p = resp;
    while (*p) {
        // skip leading empty lines and whitespace
        while (*p == '\r' || *p == '\n' || *p == ' ') p++;
        if (!*p) break;

        size_t line_len = 0;
        while (p[line_len] && p[line_len] != '\r' && p[line_len] != '\n') {
            line_len++;
        }

        // skip echoed AT command lines and final OK/ERROR codes
        if ((line_len >= 2 && (p[0] == 'A' || p[0] == 'a') && (p[1] == 'T' || p[1] == 't')) ||
            (line_len == 2 && memcmp(p, "OK", 2) == 0)) {
            p += line_len;
            continue;
        }

        size_t copy_len = (line_len < (maxlen - 1)) ? line_len : (maxlen - 1);
        memcpy(buf, p, copy_len);
        buf[copy_len] = '\0';
        while (copy_len && buf[copy_len - 1] == ' ') buf[--copy_len] = '\0';
        return copy_len ? buf : NULL;
    }
    return NULL;
}

void cell_print_modem_text(const char *resp) {
    if (!resp || !resp[0]) {
        printf("(no response)\r\n");
        return;
    }
    for (const char *p = resp; *p; p++) {
        if (*p == '\r') continue;
        if (*p == '\n') {
            printf("\r\n");
            continue;
        }
        if ((unsigned char)*p >= ' ' && (unsigned char)*p <= '~') {
            putchar_raw(*p);
        }
    }
    if (resp[strlen(resp) - 1] != '\n') printf("\r\n");
}

const char *cell_status_text(const char *resp) {
    if (!resp || !resp[0]) return "no answer";
    bool is_ok;
    if (cell_last_line_is_final(resp, strlen(resp), &is_ok)) return is_ok ? "OK" : "ERROR";
    if (cell_cmd_error(resp)) return "ERROR";
    return "OK";
}
