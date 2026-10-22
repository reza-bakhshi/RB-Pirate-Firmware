/**
 * @file cell.c
 * @brief CELL AT modem commands for UART mode.
 * @details Port of the ESP32-Bit-Pirate "CELL" feature (CellController and
 *          CellService): modem info, network status, operator scan, SIM
 *          status, PIN/PUK unlock, phonebook dump, SMS, calls, USSD, modem
 *          functional mode and raw AT passthrough.
 *
 *          Requirements: UART mode must be active and wired to the modem
 *          (modem TX -> BP RX pin, modem RX -> BP TX pin). Use the standard
 *          modem baud with `m uart -b 115200`. See the wiki section "CELL".
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "pirate.h"
#include "system_config.h"
#include "command_struct.h"
#include "bytecode.h"
#include "mode/hwuart.h"
#include "pirate/bio.h"
#include "ui/ui_term.h"
#include "ui/ui_help.h"
#include "ui/ui_prompt.h"
#include "ui/ui_parse.h"
#include "lib/bp_args/bp_cmd.h"
#include "lib/bp_linenoise/ln_cmdreader.h"
#include "commands/cell/cell.h"

// ── usage lines ──────────────────────────────────────────────────────────────

static const char* const modem_usage[] = { "cellmodem\t[-h(elp)]",
                                           "Show modem info:%s cellmodem" };
static const char* const network_usage[] = { "cellnet\t[-h(elp)]",
                                             "Show network status:%s cellnet" };
static const char* const ops_usage[] = { "cellops\t[-h(elp)] [-a(uto)]",
                                         "Scan operators (up to 60s):%s cellops",
                                         "Reset operator to auto:%s cellops -a" };
static const char* const sim_usage[] = { "cellsim\t[-h(elp)]", "Show SIM status:%s cellsim" };
static const char* const unlock_usage[] = {
    "cellpin\t[-h(elp)] [-p PIN] [-k PUK] [-n newpin] [-y]",
    "Unlock with PIN:%s cellpin -p 1234",
    "Interactive:%s cellpin",
    "Unlock with PUK:%s cellpin -k 12345678 -n 5678"
};
static const char* const book_usage[] = { "cellbook\t[-h(elp)] [-s start] [-e end]",
                                          "Dump entries 1-50:%s cellbook",
                                          "Dump entries 1-250:%s cellbook -e 250" };
static const char* const sms_usage[] = {
    "cellsms\t[-h(elp)] [-l(ist)] [-r index] [-d index] [-t number]",
    "List messages:%s cellsms -l",
    "Read message 3:%s cellsms -r 3",
    "Send SMS (interactive text prompt):%s cellsms -t +123456789",
    "Delete message 3:%s cellsms -d 3"
};
static const char* const call_usage[] = { "cellcall\t[-h(elp)] [-d number] [-a(nswer)] [-u(p)] [-l(ist)]",
                                          "Dial:%s cellcall -d +123456789",
                                          "Hang up:%s cellcall -u",
                                          "List calls:%s cellcall -l" };
static const char* const ussd_usage[] = { "cellussd <code>\t[-h(elp)] [-e 15|72]",
                                          "Check balance:%s cellussd *123#",
                                          "UCS2 encoding:%s cellussd *123# -e 72" };
static const char* const mode_usage[] = { "cellmode <1|4>\t[-h(elp)]",
                                          "Full functionality:%s cellmode 1",
                                          "Airplane mode (RF off):%s cellmode 4" };
static const char* const at_usage[] = { "cellat <command>\t[-h(elp)]",
                                        "Query signal quality:%s cellat AT+CSQ",
                                        "Turn echo off:%s cellat ATE0" };

// ── option tables ────────────────────────────────────────────────────────────

static const bp_command_opt_t ops_opts[] = {
    { "auto", 'a', BP_ARG_NONE, NULL, 0 }, { 0 }
};

static const bp_command_opt_t unlock_opts[] = {
    { "pin", 'p', BP_ARG_REQUIRED, "PIN", 0 },
    { "puk", 'k', BP_ARG_REQUIRED, "PUK", 0 },
    { "newpin", 'n', BP_ARG_REQUIRED, "newpin", 0 },
    { "yes", 'y', BP_ARG_NONE, NULL, 0 },
    { 0 }
};

static const bp_command_opt_t book_opts[] = {
    { "start", 's', BP_ARG_REQUIRED, "1-250", 0 },
    { "end", 'e', BP_ARG_REQUIRED, "1-250", 0 },
    { 0 }
};

static const bp_command_opt_t sms_opts[] = {
    { "list", 'l', BP_ARG_NONE, NULL, 0 },
    { "to", 't', BP_ARG_REQUIRED, "number", 0 },
    { "read", 'r', BP_ARG_REQUIRED, "index", 0 },
    { "delete", 'd', BP_ARG_REQUIRED, "index", 0 },
    { 0 }
};

static const bp_command_opt_t call_opts[] = {
    { "dial", 'd', BP_ARG_REQUIRED, "number", 0 },
    { "answer", 'a', BP_ARG_NONE, NULL, 0 },
    { "hangup", 'u', BP_ARG_NONE, NULL, 0 },
    { "list", 'l', BP_ARG_NONE, NULL, 0 },
    { 0 }
};

static const bp_command_opt_t ussd_opts[] = {
    { "dcs", 'e', BP_ARG_REQUIRED, "15|72", 0 },
    { 0 }
};

// ── positional tables (defined before the defs that reference them) ─────────

static const bp_command_positional_t ussd_positionals[] = {
    { "code", "e.g. *123#", 0, true }, { 0 }
};
static const bp_command_positional_t mode_positionals[] = {
    { "mode", "1=full, 4=airplane", 0, true }, { 0 }
};
static const bp_command_positional_t at_positionals[] = {
    { "command", "complete AT command", 0, true }, { 0 }
};

//!< largest SMS body accepted (single-part GSM-7bit text SMS: 160 chars)
#define cell_sms_body_max 161

// ── command definitions (the .name is the terminal command word) ────────────

const bp_command_def_t cell_ussd_def = { .name = "cellussd",
                                         .description = T_HELP_CELL_USSD,
                                         .opts = ussd_opts,
                                         .positionals = ussd_positionals,
                                         .positional_count = 1,
                                         .usage = ussd_usage,
                                         .usage_count = count_of(ussd_usage) };
const bp_command_def_t cell_setmode_def = { .name = "cellmode",
                                            .description = T_HELP_CELL_SETMODE,
                                            .positionals = mode_positionals,
                                            .positional_count = 1,
                                            .usage = mode_usage,
                                            .usage_count = count_of(mode_usage) };
const bp_command_def_t cell_at_def = { .name = "cellat",
                                       .description = T_HELP_CELL_AT,
                                       .positionals = at_positionals,
                                       .positional_count = 1,
                                       .usage = at_usage,
                                       .usage_count = count_of(at_usage) };

const bp_command_def_t cell_modem_def = { .name = "cellmodem",
                                          .description = T_HELP_CELL_MODEM,
                                          .usage = modem_usage,
                                          .usage_count = count_of(modem_usage) };
const bp_command_def_t cell_network_def = { .name = "cellnet",
                                            .description = T_HELP_CELL_NETWORK,
                                            .usage = network_usage,
                                            .usage_count = count_of(network_usage) };
const bp_command_def_t cell_operator_def = { .name = "cellops",
                                             .description = T_HELP_CELL_OPERATOR,
                                             .opts = ops_opts,
                                             .usage = ops_usage,
                                             .usage_count = count_of(ops_usage) };
const bp_command_def_t cell_sim_def = { .name = "cellsim",
                                        .description = T_HELP_CELL_SIM,
                                        .usage = sim_usage,
                                        .usage_count = count_of(sim_usage) };
const bp_command_def_t cell_unlock_def = { .name = "cellpin",
                                           .description = T_HELP_CELL_UNLOCK,
                                           .opts = unlock_opts,
                                           .usage = unlock_usage,
                                           .usage_count = count_of(unlock_usage) };
const bp_command_def_t cell_phonebook_def = { .name = "cellbook",
                                              .description = T_HELP_CELL_PHONEBOOK,
                                              .opts = book_opts,
                                              .usage = book_usage,
                                              .usage_count = count_of(book_usage) };
const bp_command_def_t cell_sms_def = { .name = "cellsms",
                                        .description = T_HELP_CELL_SMS,
                                        .opts = sms_opts,
                                        .usage = sms_usage,
                                        .usage_count = count_of(sms_usage) };
const bp_command_def_t cell_call_def = { .name = "cellcall",
                                         .description = T_HELP_CELL_CALL,
                                         .opts = call_opts,
                                         .usage = call_usage,
                                         .usage_count = count_of(call_usage) };

// ── helpers ──────────────────────────────────────────────────────────────────

static bool modem_present(void) {
    if (cell_modem_detect()) return true;
    printf("%sNo modem answered. Check wiring (modem TX->BIO5/RX, modem RX->BIO4/TX), baud "
           "rate and power.%s\r\n",
           ui_term_color_error(),
           ui_term_color_reset());
    return false;
}

static bool sim_ok(void) {
    if (cell_sim_ready()) return true;
    printf("%sSIM not ready. Use 'cellpin' to enter the PIN first.%s\r\n",
           ui_term_color_error(),
           ui_term_color_reset());
    return false;
}

/**
 * @brief Interactive line prompt with full editing (spaces allowed).
 * @return true and fills buf (maxlen) when the user entered a line.
 */
static bool prompt_line(const char *prompt, char *buf, size_t maxlen) {
    printf("%s%s%s", ui_term_color_prompt(), prompt, ui_term_color_reset());
    if (!ui_prompt_user_input()) return false;
    const char *line = ln_cmdln_current();
    size_t len = ln_cmdln_remaining();
    if (len >= maxlen) len = maxlen - 1;
    memcpy(buf, line, len);
    buf[len] = '\0';
    // strip trailing CR/LF just in case
    while (len && (buf[len - 1] == '\r' || buf[len - 1] == '\n')) buf[--len] = '\0';
    return len > 0;
}

/**
 * @brief Send message text and wait for the submission result.
 * @details The modem responds to AT+CMGS with "> ", takes the text and
 *          submits on CTRL+Z, then returns +CMGS/<ref> and a final code.
 * @return true when the final result was OK.
 */
static bool sms_send_text(const char *number, const char *body) {
    char cmd[40];
    snprintf(cmd, sizeof(cmd), "AT+CMGS=\"%s\"", number);
    const char *resp = cell_cmd(cmd, 8000);
    if (!resp || !strchr(resp, '>')) {
        printf("Modem refused to send (%s).\r\n", cell_status_text(resp));
        return false;
    }
    printf("%s\r\n", body); // local echo of the submitted text
    for (const char *p = body; *p; p++) {
        uart_putc_raw(M_UART_PORT, (uint8_t)*p);
    }
    uart_putc_raw(M_UART_PORT, 0x1a); // CTRL+Z: submit the message

    // stream the asynchronous submission URCs until quiet (+CMGS ... final code)
    uint32_t start = to_ms_since_boot(get_absolute_time());
    uint32_t last_byte = start;
    while ((to_ms_since_boot(get_absolute_time()) - start) < 30000) {
        if (uart_is_readable(M_UART_PORT)) {
            putchar_raw(uart_getc(M_UART_PORT));
            last_byte = to_ms_since_boot(get_absolute_time());
        } else if ((to_ms_since_boot(get_absolute_time()) - last_byte) > 1000) {
            break;
        }
    }
    printf("\r\n");
    return true;
}

// ── handlers ─────────────────────────────────────────────────────────────────

void cell_modem_handler(struct command_result *res) {
    if (bp_cmd_help_check(&cell_modem_def, res->help_flag)) return;
    if (!ui_help_check_vout_vref()) return;
    if (!modem_present()) return;

    char field[48];
    printf("%s[MODEM]%s\r\n", ui_term_color_info(), ui_term_color_reset());
    if (cell_first_line(cell_cmd("AT+CGMI", 500), field, sizeof(field)))
        printf("  Manufacturer: %s\r\n", field);
    if (cell_first_line(cell_cmd("AT+CGMM", 500), field, sizeof(field)))
        printf("  Model: %s\r\n", field);
    if (cell_first_line(cell_cmd("AT+CGMR", 500), field, sizeof(field)))
        printf("  Revision: %s\r\n", field);
    if (cell_first_line(cell_cmd("AT+CGSN", 500), field, sizeof(field)))
        printf("  IMEI: %s\r\n", field);
    const char *fun = cell_cmd("AT+CFUN?", 500);
    if (cell_field(fun, "+CFUN: ", field, sizeof(field)))
        printf("  Functionality: %s (1=full, 4=RF off, 0=minimum)\r\n", field);
    const char *clk = cell_cmd("AT+CCLK?", 3000);
    if (cell_field(clk, "+CCLK: ", field, sizeof(field))) printf("  Clock: %s\r\n", field);
}

void cell_network_handler(struct command_result *res) {
    if (bp_cmd_help_check(&cell_network_def, res->help_flag)) return;
    if (!ui_help_check_vout_vref()) return;
    if (!modem_present()) return;

    char field[48];
    printf("%s[NETWORK]%s\r\n", ui_term_color_info(), ui_term_color_reset());
    if (cell_field(cell_cmd("AT+CSQ", 500), "+CSQ: ", field, sizeof(field)))
        printf("  Signal quality: %s  (RSSI,BER; 31=-51dBm or better, 99=unknown)\r\n", field);
    if (cell_field(cell_cmd("AT+COPS?", 3000), "+COPS: ", field, sizeof(field)))
        printf("  Operator: %s\r\n", field);
    if (cell_field(cell_cmd("AT+CREG?", 500), "+CREG: ", field, sizeof(field)))
        printf("  CS registration: %s (stat 1 or 5 = registered)\r\n", field);
    if (cell_field(cell_cmd("AT+CGREG?", 500), "+CGREG: ", field, sizeof(field)))
        printf("  PS registration: %s (stat 1 or 5 = registered)\r\n", field);
    if (cell_field(cell_cmd("AT+CGATT?", 500), "+CGATT: ", field, sizeof(field)))
        printf("  Packet attach: %s\r\n", field);
}

void cell_operator_handler(struct command_result *res) {
    if (bp_cmd_help_check(&cell_operator_def, res->help_flag)) return;
    if (!ui_help_check_vout_vref()) return;
    if (!modem_present()) return;

    if (bp_cmd_find_flag(&cell_operator_def, 'a')) {
        printf("%sSelecting operator automatically...%s\r\n",
               ui_term_color_notice(),
               ui_term_color_reset());
        printf("%s\r\n", cell_cmd_ok("AT+COPS=0", 8000) ? "Done." : "Failed.");
        return;
    }

    printf("%sScanning operators (can take up to 1 minute)...%s\r\n",
           ui_term_color_notice(),
           ui_term_color_reset());
    const char *scan = cell_cmd("AT+COPS=?", 65000);
    cell_print_modem_text(scan);

    const char *ops = cell_cmd("AT+COPS?", 3000);
    char field[48];

    if (ops && cell_field(ops, "+COPS: ", field, sizeof(field))) {
        printf("  Current: %s\r\n", field);
    } else if (ops) {
        cell_print_modem_text(ops);
    }

    if (bp_cmd_confirm(&cell_operator_def, "Reset operator to auto mode?")) {
        printf("%s\r\n", cell_cmd_ok("AT+COPS=0", 8000) ? "Done." : "Failed.");
    }
}

void cell_sim_handler(struct command_result *res) {
    if (bp_cmd_help_check(&cell_sim_def, res->help_flag)) return;
    if (!ui_help_check_vout_vref()) return;
    if (!modem_present()) return;

    char field[64];
    printf("%s[SIM]%s\r\n", ui_term_color_info(), ui_term_color_reset());
    const char *pin = cell_cmd("AT+CPIN?", 500);
    if (pin && cell_field(pin, "+CPIN: ", field, sizeof(field))) {
        printf("  SIM state: %s\r\n", field);
    } else {
        printf("  SIM state: %s\r\n", cell_status_text(pin));
    }
    if (cell_field(cell_cmd("AT+CSPN?", 500), "+CSPN: ", field, sizeof(field)))
        printf("  Provider: %s\r\n", field);
    if (cell_first_line(cell_cmd("AT+CCID", 500), field, sizeof(field)))
        printf("  ICCID: %s\r\n", field);
    if (cell_first_line(cell_cmd("AT+CIMI", 500), field, sizeof(field)))
        printf("  IMSI: %s\r\n", field);
    if (cell_field(cell_cmd("AT+CNUM", 500), "+CNUM: \"", field, sizeof(field)))
        printf("  Own number: %s\r\n", field);
    if (cell_field(cell_cmd("AT+CLCK=\"SC\",2", 500), "+CLCK: ", field, sizeof(field)))
        printf("  PIN lock: %s (0=disabled, 1=enabled)\r\n", field);
    if (cell_field(cell_cmd("AT+CPBS?", 500), "+CPBS: ", field, sizeof(field)))
        printf("  Phonebook storage: %s\r\n", field);
    if (cell_field(cell_cmd("AT+CPMS?", 500), "+CPMS: ", field, sizeof(field)))
        printf("  SMS storage: %s\r\n", field);
    if (!cell_sim_ready()) {
        printf("\r\n  SIM is locked. Use 'cellpin -p <PIN>' to unlock.\r\n");
    }
}

void cell_unlock_handler(struct command_result *res) {
    if (bp_cmd_help_check(&cell_unlock_def, res->help_flag)) return;
    if (!ui_help_check_vout_vref()) return;
    if (!modem_present()) return;

    char pin[16] = { 0 };
    char puk[16] = { 0 };
    char newpin[16] = { 0 };
    char cmd[48];

    // read all command line arguments before any interactive prompt:
    // a prompt re-points the command line reader to the prompt input
    bool has_puk = bp_cmd_get_string(&cell_unlock_def, 'k', puk, sizeof(puk));
    bool has_pin = bp_cmd_get_string(&cell_unlock_def, 'p', pin, sizeof(pin));
    bool has_new = bp_cmd_get_string(&cell_unlock_def, 'n', newpin, sizeof(newpin));
    bool yes = bp_cmd_find_flag(&cell_unlock_def, 'y');

    // PUK path: PUK plus a new PIN
    if (has_puk || cell_sim_puk_required()) {
        if (!has_puk) {
            printf("%sThe SIM is blocked and requires its PUK.%s\r\n",
                   ui_term_color_warning(),
                   ui_term_color_reset());
            printf("A valid PUK (8 digits) unblocks it; you must also set a new PIN.\r\n");
            if (yes || bp_cmd_confirm(&cell_unlock_def, "Continue with PUK unlock?")) {
                if (!prompt_line("Enter PUK: ", puk, sizeof(puk))) return;
            } else {
                return;
            }
        }
        if (!has_new) {
            if (!prompt_line("Enter new PIN (4-8 digits): ", newpin, sizeof(newpin))) return;
        }
        if (strlen(puk) != 8 || strlen(newpin) < 4 || strlen(newpin) > 8) {
            printf("Invalid lengths: PUK 8 digits, new PIN 4-8 digits.\r\n");
            return;
        }
        if (!yes && !bp_cmd_confirm(&cell_unlock_def, "Unlock with this PUK and new PIN?")) return;
        snprintf(cmd, sizeof(cmd), "AT+CPIN=\"%.8s\",\"%.8s\"", puk, newpin);
        printf("%s\r\n", cell_cmd_ok(cmd, 10000) ? "PUK accepted." : "PUK unlock failed.");
        const char *state = cell_cmd("AT+CPIN?", 500);
        if (state) cell_print_modem_text(state);
        return;
    }

    if (!has_pin) {
        if (!prompt_line("Enter SIM PIN (4-8 digits): ", pin, sizeof(pin))) return;
    }
    if (strlen(pin) < 4 || strlen(pin) > 8) {
        printf("Invalid PIN length (4-8 digits).\r\n");
        return;
    }
    if (!yes && !bp_cmd_confirm(&cell_unlock_def, "Unlock with this PIN?")) return;
    snprintf(cmd, sizeof(cmd), "AT+CPIN=\"%.8s\"", pin);
    printf("%s\r\n", cell_cmd_ok(cmd, 6000) ? "PIN accepted." : "PIN unlock failed.");
    const char *state = cell_cmd("AT+CPIN?", 500);
    if (state) cell_print_modem_text(state);
}

void cell_phonebook_handler(struct command_result *res) {
    if (bp_cmd_help_check(&cell_phonebook_def, res->help_flag)) return;
    if (!ui_help_check_vout_vref()) return;
    if (!modem_present()) return;
    if (!sim_ok()) return;

    uint32_t start = 1, end = 50;
    uint32_t value;
    if (bp_cmd_get_uint32(&cell_phonebook_def, 's', &value)) start = value;
    if (bp_cmd_get_uint32(&cell_phonebook_def, 'e', &value)) end = value;
    if (!start || !end || end < start || end > 250) {
        printf("Limits: start 1-250, end 1-250, end >= start.\r\n");
        return;
    }

    char cmd[32];
    printf("%s[PHONEBOOK %lu-%lu]%s\r\n",
           ui_term_color_info(),
           (unsigned long)start,
           (unsigned long)end,
           ui_term_color_reset());
    const char *caps = cell_cmd("AT+CPBR=?", 3000);
    if (caps) {
        char field[24];
        if (cell_field(caps, "+CPBR: ", field, sizeof(field)))
            printf("  Storage capacity: %s entries\r\n", field);
    }
    snprintf(cmd, sizeof(cmd), "AT+CPBR=%lu,%lu", (unsigned long)start, (unsigned long)end);
    cell_print_modem_text(cell_cmd(cmd, 10000));
}

void cell_sms_handler(struct command_result *res) {
    if (bp_cmd_help_check(&cell_sms_def, res->help_flag)) return;
    if (!ui_help_check_vout_vref()) return;
    if (!modem_present()) return;
    if (!sim_ok()) return;

    char to[24] = { 0 };
    char body[cell_sms_body_max] = { 0 };
    char cmd[44];

    uint32_t index = 0;
    int taken = 0;
    enum { ACT_NONE, ACT_LIST, ACT_READ, ACT_DELETE, ACT_SEND } action = ACT_NONE;

    if (bp_cmd_find_flag(&cell_sms_def, 'l')) { action = ACT_LIST; taken++; }
    if (bp_cmd_get_uint32(&cell_sms_def, 'r', &index)) { action = ACT_READ; taken++; }
    if (bp_cmd_get_uint32(&cell_sms_def, 'd', &index)) { action = ACT_DELETE; taken++; }
    bool to_has = bp_cmd_get_string(&cell_sms_def, 't', to, sizeof(to));
    if (to_has) { action = ACT_SEND; taken++; }

    if (action == ACT_NONE || taken > 1) {
        bp_cmd_help_show(&cell_sms_def);
        return;
    }

    // switch the modem to SMS text mode
    if (!cell_cmd_ok("AT+CMGF=1", 3000)) {
        printf("Modem refused SMS text mode (AT+CMGF=1).\r\n");
        return;
    }

    if (action == ACT_LIST) {
        cell_print_modem_text(cell_cmd("AT+CMGL=\"ALL\"", 10000));
        return;
    }
    if (action == ACT_READ) {
        if (!index) {
            printf("Invalid index (1-255).\r\n");
            return;
        }
        snprintf(cmd, sizeof(cmd), "AT+CMGR=%lu", (unsigned long)index);
        cell_print_modem_text(cell_cmd(cmd, 10000));
        return;
    }
    if (action == ACT_DELETE) {
        if (!index) {
            printf("Invalid index (1-255).\r\n");
            return;
        }
        snprintf(cmd, sizeof(cmd), "AT+CMGD=%lu", (unsigned long)index);
        printf("%s\r\n", cell_cmd_ok(cmd, 8000) ? "Deleted." : "Delete failed.");
        return;
    }

    // send
    if (!to_has) {
        if (!prompt_line("Recipient number: ", to, sizeof(to))) return;
    }
    if (!prompt_line("Message text: ", body, sizeof(body))) return;
    printf("Sending SMS to %s...\r\n", to);
    if (sms_send_text(to, body)) {
        printf("SMS submitted.\r\n");
    }
}

void cell_call_handler(struct command_result *res) {
    if (bp_cmd_help_check(&cell_call_def, res->help_flag)) return;
    if (!ui_help_check_vout_vref()) return;
    if (!modem_present()) return;
    if (!sim_ok()) return;

    char number[24] = { 0 };
    char cmd[40];

    bool dial_has = bp_cmd_get_string(&cell_call_def, 'd', number, sizeof(number));
    bool answer = bp_cmd_find_flag(&cell_call_def, 'a');
    bool hangup = bp_cmd_find_flag(&cell_call_def, 'u');

    int taken = (dial_has ? 1 : 0) + (answer ? 1 : 0) + (hangup ? 1 : 0);
    if (taken == 0) {
        if (bp_cmd_find_flag(&cell_call_def, 'l')) {
            cell_print_modem_text(cell_cmd("AT+CLCC", 5000));
            return;
        }
        bp_cmd_help_show(&cell_call_def);
        return;
    }
    if (taken > 1) {
        printf("Choose one: -d dial, -a answer or -u hang up.\r\n");
        return;
    }

    if (dial_has) {
        snprintf(cmd, sizeof(cmd), "ATD%s;", number);
        printf("%s\r\n",
               cell_cmd_ok(cmd, 6000) ? "Dialing..." : "Dial failed.");
        return;
    }
    if (answer) {
        printf("%s\r\n", cell_cmd_ok("ATA", 6000) ? "Call answered." : "Answer failed.");
        return;
    }
    if (hangup) {
        // AT+CHUP (3GPP) hangs up; ATH is widely supported too
        if (cell_cmd_ok("AT+CHUP", 6000)) {
            printf("Call ended.\r\n");
        } else if (cell_cmd_ok("ATH", 3000)) {
            printf("Call ended.\r\n");
        } else {
            printf("Hang up failed.\r\n");
        }
    }
}

void cell_ussd_handler(struct command_result *res) {
    if (bp_cmd_help_check(&cell_ussd_def, res->help_flag)) return;
    if (!ui_help_check_vout_vref()) return;
    if (!modem_present()) return;
    if (!sim_ok()) return;

    char code[24] = { 0 };
    // read flags before any interactive prompt:
    // a prompt re-points the command line reader to the prompt input
    uint32_t dcs = 15;
    uint32_t value;
    if (bp_cmd_get_uint32(&cell_ussd_def, 'e', &value)) dcs = value;
    if (!bp_cmd_get_positional_string(&cell_ussd_def, 1, code, sizeof(code))) {
        if (!prompt_line("USSD code (e.g. *123#): ", code, sizeof(code))) return;
    }
    for (const char *p = code; *p; p++) {
        if (!((*p >= '0' && *p <= '9') || *p == '*' || *p == '#' || *p == '+' || *p == 'A' ||
              *p == 'B' || *p == 'C' || *p == 'D')) {
            printf("Invalid USSD code. Allowed: digits, *, #, +, A, B, C.\r\n");
            return;
        }
    }
    if (dcs != 15 && dcs != 72) {
        printf("DCS must be 15 (GSM 7-bit) or 72 (UCS2).\r\n");
        return;
    }

    char cmd[56];
    snprintf(cmd, sizeof(cmd), "AT+CUSD=1,\"%s\",%lu", code, (unsigned long)dcs);
    const char *resp = cell_cmd(cmd, 15000);
    cell_print_modem_text(resp);
    if (resp && !cell_cmd_error(resp) && !strstr(resp, "+CUSD")) {
        printf("(USSD response may arrive asynchronously)\r\n");
    }
}

void cell_setmode_handler(struct command_result *res) {
    if (bp_cmd_help_check(&cell_setmode_def, res->help_flag)) return;
    if (!ui_help_check_vout_vref()) return;
    if (!modem_present()) return;

    uint32_t mode;
    if (!bp_cmd_get_positional_uint32(&cell_setmode_def, 1, &mode)) {
        bp_cmd_help_show(&cell_setmode_def);
        return;
    }
    if (mode != 1 && mode != 4) {
        printf("Mode must be 1 (full functionality) or 4 (RF disabled).\r\n");
        return;
    }

    char cmd[16];
    snprintf(cmd, sizeof(cmd), "AT+CFUN=%lu", (unsigned long)mode);
    const char *resp = cell_cmd(cmd, 10000);
    printf("Setmode: %s\r\n", cell_status_text(resp));
    // report registration state after the change
    const char *creg = cell_cmd("AT+CREG?", 500);
    char field[24];
    if (cell_field(creg, "+CREG: ", field, sizeof(field)))
        printf("  CS registration: %s\r\n", field);
}

void cell_at_handler(struct command_result *res) {
    if (bp_cmd_help_check(&cell_at_def, res->help_flag)) return;
    if (!ui_help_check_vout_vref()) return;
    if (!modem_present()) return;

    char cmd[128] = { 0 };
    const char *remainder;
    size_t len;
    if (!bp_cmd_get_remainder(&cell_at_def, &remainder, &len)) {
        bp_cmd_help_show(&cell_at_def);
        return;
    }
    if (len >= sizeof(cmd)) len = sizeof(cmd) - 1;
    memcpy(cmd, remainder, len);
    cmd[len] = '\0';
    // strip trailing spaces
    while (len && cmd[len - 1] == ' ') cmd[--len] = '\0';
    if (!len) {
        bp_cmd_help_show(&cell_at_def);
        return;
    }

    printf("%s> %s%s\r\n", ui_term_color_prompt(), cmd, ui_term_color_reset());
    const char *resp = cell_cmd(cmd, 8000);
    cell_print_modem_text(resp);
    if (resp) {
        printf("%s[%s]%s\r\n",
               ui_term_color_info(),
               cell_status_text(resp),
               ui_term_color_reset());
    }
}
