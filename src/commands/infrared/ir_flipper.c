/**
 * @file ir_flipper.c
 * @brief Flipper Zero .ir signal file read/write support for INFRARED mode.
 *
 * Based on the INFRARED feature set of the ESP32-Bit-Pirate project
 * (https://github.com/geo-tp/ESP32-Bit-Pirate), adapted for the Bus Pirate 5.
 *
 * Modified for Bus Pirate 5 by community contributors (c) 2026
 * SPDX-License-Identifier: MIT
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "pirate/ir_protocols.h"
#include "commands/infrared/ir_flipper.h"

// engine index -> Flipper protocol name
static const char *flipper_name(int8_t proto, uint32_t sub) {
    switch (proto) {
        case 0: return "NEC";
        case 1: return "Samsung32";
        case 2: return "LG";
        case 3: // SONY
            if (sub == 15) return "SIRC15";
            if (sub == 20) return "SIRC20";
            return "SIRC";
        case 4: return "JVC";
        case 5: return "Kaseikyo";
        case 6: return "RC5";
        case 7: return "RC6";
        case 8: return "DISH";
        case 9: return "Sharp";
        case 10: return "Denon";
        case 11: return "Mitsubishi";
        case 12: return "Kaseikyo_Sharp";
        default: return "UNKNOWN";
    }
}

// Flipper protocol name -> engine index
static int8_t flipper_to_engine(const char *name, uint32_t *sub) {
    *sub = 0;
    if (!strcasecmp(name, "SIRC15")) {
        *sub = 15;
        return 3;
    }
    if (!strcasecmp(name, "SIRC20")) {
        *sub = 20;
        return 3;
    }
    if (!strcasecmp(name, "NEC") || !strcasecmp(name, "NECext") || !strcasecmp(name, "NEC42")) return 0;
    if (!strcasecmp(name, "SIRC")) return 3;
    if (!strcasecmp(name, "RC5") || !strcasecmp(name, "RC5X")) return 6;
    if (!strcasecmp(name, "RC6")) return 7;
    if (!strcasecmp(name, "Samsung32")) return 1;
    if (!strcasecmp(name, "LG")) return 2;
    if (!strcasecmp(name, "JVC")) return 4;
    if (!strcasecmp(name, "Kaseikyo") || !strcasecmp(name, "Panasonic")) return 5;
    if (!strcasecmp(name, "Sharp")) return 9;
    if (!strcasecmp(name, "DISH")) return 8;
    if (!strcasecmp(name, "Denon") || !strcasecmp(name, "Kaseikyo_Denon")) return 10;
    if (!strcasecmp(name, "Mitsubishi") || !strcasecmp(name, "Kaseikyo_Mitsubishi")) return 11;
    if (!strcasecmp(name, "Kaseikyo_Sharp")) return 12;
    // fall back to our own table names
    return ir_protocols_find(name);
}

static bool write_hex64(FIL *fil, uint32_t value) {
    return (f_printf(fil, "%02lx %02lx %02lx %02lx 00 00 00 00",
                     (unsigned long)(value & 0xff),
                     (unsigned long)((value >> 8) & 0xff),
                     (unsigned long)((value >> 16) & 0xff),
                     (unsigned long)((value >> 24) & 0xff)) > 0);
}

bool ir_flipper_write_header_if_needed(FIL *fil) {
    if (f_size(fil) > 0) {
        return true;
    }
    return (f_printf(fil, "Filetype: IR signals file\nVersion: 1\n#\n") > 0);
}

bool ir_flipper_write_signal(FIL *fil, const ir_signal_t *signal) {
    if (f_printf(fil, "name: %s\n", signal->name) <= 0) {
        return false;
    }
    if (signal->parsed) {
        if (f_printf(fil, "type: parsed\nprotocol: %s\naddress: ", flipper_name(signal->proto, signal->sub)) <= 0 ||
            !write_hex64(fil, signal->dev) ||
            f_printf(fil, "\ncommand: ") <= 0 || !write_hex64(fil, signal->cmd) ||
            f_printf(fil, "\n#\n") <= 0) {
            return false;
        }
    } else {
        if (f_printf(fil, "type: raw\nfrequency: %lu\nduty_cycle: 0.330000\ndata: ", (unsigned long)(signal->freq_khz * 1000)) <= 0) {
            return false;
        }
        for (uint16_t i = 0; i < signal->pairs; i++) {
            uint32_t mark = signal->data[i] >> 16;
            uint32_t space = signal->data[i] & 0xffff;
            if (f_printf(fil, "%lu ", (unsigned long)mark) <= 0) {
                return false;
            }
            if (i < (signal->pairs - 1)) { // no trailing space after the final mark
                if (f_printf(fil, "%lu ", (unsigned long)space) <= 0) {
                    return false;
                }
            }
        }
        if (f_printf(fil, "\n#\n") <= 0) {
            return false;
        }
    }
    return true;
}

// get the value after "key:" on the current line, true if found
static bool line_value(const char *line, const char *key, char *out, uint16_t out_len) {
    uint16_t klen = strlen(key);
    if (strncasecmp(line, key, klen) != 0) {
        return false;
    }
    line += klen;
    while (*line == ' ') {
        line++;
    }
    uint16_t n = 0;
    while (*line && *line != '\r' && *line != '\n' && n < (out_len - 1)) {
        out[n++] = *line++;
    }
    out[n] = 0;
    return true;
}

// parse a "hex byte byte byte..." list into a little endian value
static uint32_t parse_hex_bytes(const char *s) {
    uint32_t value = 0;
    uint8_t shift = 0;
    while (*s && shift < 32) {
        char *end;
        long b = strtol(s, &end, 16);
        if (end == s) {
            break;
        }
        value |= ((uint32_t)(b & 0xff)) << shift;
        shift += 8;
        s = end;
    }
    return value;
}

uint16_t ir_flipper_scan(FIL *fil, uint32_t *offsets, char names[][33], uint16_t max) {
    char line[256];
    uint16_t count = 0;
    f_lseek(fil, 0);
    while (count < max && f_gets(line, sizeof(line), fil)) {
        char value[64];
        if (line_value(line, "name:", value, sizeof(value))) {
            offsets[count] = f_tell(fil) - strlen(line); // start of the name: line
            strncpy(names[count], value, 32);
            names[count][32] = 0;
            count++;
        }
    }
    return count;
}

bool ir_flipper_read_signal(FIL *fil, uint32_t offset, ir_signal_t *signal) {
    char line[512];
    char value[64];
    memset(signal, 0, sizeof(ir_signal_t));
    signal->proto = -1;
    signal->freq_khz = 38;
    f_lseek(fil, offset);
    bool have_type = false;
    bool have_raw = false;
    while (f_gets(line, sizeof(line), fil)) {
        if (line_value(line, "name:", value, sizeof(value))) {
            strncpy(signal->name, value, 32);
        } else if (line_value(line, "type:", value, sizeof(value))) {
            signal->parsed = (strcasecmp(value, "parsed") == 0);
            have_type = true;
        } else if (line_value(line, "protocol:", value, sizeof(value))) {
            signal->proto = flipper_to_engine(value, &signal->sub);
        } else if (line_value(line, "address:", value, sizeof(value))) {
            signal->dev = parse_hex_bytes(value);
        } else if (line_value(line, "command:", value, sizeof(value))) {
            signal->cmd = parse_hex_bytes(value);
        } else if (line_value(line, "frequency:", value, sizeof(value))) {
            signal->freq_khz = (uint32_t)strtoul(value, NULL, 10) / 1000;
        } else if (strncasecmp(line, "data:", 5) == 0) {
            have_raw = true;
            // FatFS can split a long data line across several f_gets calls.
            // Keep the current number and mark across those chunks.
            char *s = line + 5;
            bool want_mark = true;
            uint32_t mark = 0;
            uint32_t number = 0;
            bool in_number = false;
            bool too_long = false;
            while (true) {
                while (*s) {
                    if (*s >= '0' && *s <= '9') {
                        in_number = true;
                        number = number * 10 + (*s - '0');
                    } else if (in_number) {
                        if (number > 0xffff) return false;
                        if (want_mark) {
                            mark = number;
                        } else if (signal->pairs < IR_CMD_MAX_PAIRS) {
                            signal->data[signal->pairs++] = (mark << 16) | number;
                        } else {
                            too_long = true;
                        }
                        want_mark = !want_mark;
                        number = 0;
                        in_number = false;
                    }
                    s++;
                }
                if (strchr(line, '\n') == NULL) {
                    if (!f_gets(line, sizeof(line), fil)) break;
                    s = line;
                    continue;
                }
                if (!f_gets(line, sizeof(line), fil)) break;
                s = line;
                while (*s == ' ' || *s == '\t') s++;
                if (*s < '0' || *s > '9') break;
            }
            if (in_number) {
                if (number > 0xffff) return false;
                if (want_mark) mark = number;
                else if (signal->pairs < IR_CMD_MAX_PAIRS) signal->data[signal->pairs++] = (mark << 16) | number;
                else too_long = true;
                want_mark = !want_mark;
            }
            if (!want_mark && signal->pairs < IR_CMD_MAX_PAIRS) {
                signal->data[signal->pairs++] = mark << 16;
            } else if (!want_mark) {
                too_long = true;
            }
            if (too_long || signal->pairs == 0) return false;
            break; // data is the last field of a raw block
        } else if (line[0] == '#') {
            break; // end of block
        }
    }
    if (!have_type) {
        return false;
    }
    if (signal->parsed && signal->proto < 0) {
        return true; // parsed but unsupported protocol: caller decides
    }
    if (!signal->parsed && !have_raw) {
        return false;
    }
    return true;
}
