/**
 * @file cell.h
 * @brief CELL AT modem commands for UART mode.
 * @details Port of the ESP32-Bit-Pirate "CELL" feature. Communicates with
 *          SIMCom-style 2G/3G/4G cellular AT modems connected to the hardware
 *          UART pins. Uses only standard 3GPP TS 27.007 AT commands, so any
 *          modem with a standard AT interface should work.
 *
 *          Requires: UART mode active (m uart), baud matching the modem
 *          (factory default is usually 115200), TX/RX wired to the modem,
 *          valid SIM and antenna attached.
 */

#ifndef BP_CMD_CELL_H
#define BP_CMD_CELL_H

#include <stdint.h>
#include <stdbool.h>
#include "command_struct.h"

struct bp_command_def;

/* AT engine (usable outside the command handlers) */

/**
 * @brief Send an AT command and collect the response until a final result
 *        code (OK / ERROR / +CME ERROR / +CMS ERROR) or timeout.
 * @param cmd  AT command text (CR is appended automatically)
 * @param timeout_ms  Read timeout in milliseconds
 * @return Pointer to a static response buffer, or NULL when the modem
 *         returned nothing. The buffer is overwritten on the next call.
 */
const char *cell_cmd(const char *cmd, uint32_t timeout_ms);

/**
 * @brief Send an AT command and check it completed with a final OK.
 * @details "OK" is only accepted when it is the trailing line of the
 *          response, so a stray "OK" inside an echo or URC does not
 *          count as success.
 * @return true if the response ended with OK and printed no error code.
 */
bool cell_cmd_ok(const char *cmd, uint32_t timeout_ms);

/**
 * @brief Probe the modem with "AT" until it answers (3 attempts).
 * @return true when a modem responded.
 */
bool cell_modem_detect(void);

/** @return true when the SIM reports READY (AT+CPIN?). */
bool cell_sim_ready(void);

/** @return true when the SIM is blocked and requires a PUK. */
bool cell_sim_puk_required(void);

/**
 * @brief Extract one field from an AT response.
 * @details Finds @p prefix (e.g. "+CPIN:") and copies the text up to the
 *          end of that line into @p buf (trailing spaces trimmed).
 * @return buf on success, NULL when the prefix was not found.
 */
const char *cell_field(const char *resp, const char *prefix, char *buf, size_t maxlen);

/**
 * @brief Copy the first printable line of an AT response into a buffer.
 * @details Used for value-style responses (AT+CGMI -> "SIMCOM_Ltd",
 *          AT+CCID -> "8986...") where there is no field prefix.
 * @return buf on success, NULL when the response was empty.
 */
const char *cell_first_line(const char *resp, char *buf, size_t maxlen);

/**
 * @brief Print a modem response with printable ASCII and CRLF line ends.
 * @details Replaces bare LF with CRLF and swallows non-printable bytes
 *          so modem output is terminal-safe.
 */
void cell_print_modem_text(const char *resp);

/** @brief "OK" / "ERROR" / "no answer" summary of a response. */
const char *cell_status_text(const char *resp);

/** @return true when the response contains an ERROR result code. */
bool cell_cmd_error(const char *resp);

/* mode command handlers and definitions (UART mode only) */
extern const struct bp_command_def cell_modem_def;
extern const struct bp_command_def cell_network_def;
extern const struct bp_command_def cell_operator_def;
extern const struct bp_command_def cell_sim_def;
extern const struct bp_command_def cell_unlock_def;
extern const struct bp_command_def cell_phonebook_def;
extern const struct bp_command_def cell_sms_def;
extern const struct bp_command_def cell_call_def;
extern const struct bp_command_def cell_ussd_def;
extern const struct bp_command_def cell_setmode_def;
extern const struct bp_command_def cell_at_def;

void cell_modem_handler(struct command_result *res);
void cell_network_handler(struct command_result *res);
void cell_operator_handler(struct command_result *res);
void cell_sim_handler(struct command_result *res);
void cell_unlock_handler(struct command_result *res);
void cell_phonebook_handler(struct command_result *res);
void cell_sms_handler(struct command_result *res);
void cell_call_handler(struct command_result *res);
void cell_ussd_handler(struct command_result *res);
void cell_setmode_handler(struct command_result *res);
void cell_at_handler(struct command_result *res);

#endif
