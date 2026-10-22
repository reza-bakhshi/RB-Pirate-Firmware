/*

########################################################################################################################
#                                                                                                                      #
#                                                                                                                      #
#                          This file was adapted from https://github.com/probonopd/MakeHex/.                           #
#                         It takes an IRP string and return the raw IR sequence. By John Fine                          #
#                                                                                                                      #
#                                                                                                                      #
########################################################################################################################

*/

// Copyright 2005 John S. Fine

// You may use, copy, modify and/or distribute this program for private or commercial use provided that:
// 1) You do not hold me responsible for any damage or negative consequences resulting from those activities.
// 2) You include this copyright notice and disclaimer in any copy of any part or all of this program.
// I do not provide any warranty of the correctness, safety, or suitibility of this program for any purpose.
// If you do not agree to these conditions, you have no permission to use, copy, modify or distribute this program.

// Ported to the Bus Pirate 5 firmware from the ESP32-Bit-Pirate project
// (https://github.com/geo-tp/ESP32-Bit-Pirate, src/Vendors/MakeHex.cpp).
// Modified for Bus Pirate 5: C-callable wrapper, fixed the Device= line
// written when no sub-device is specified, bounded output buffer.
// Modified by Bus Pirate community contributors (c) 2026

#include <vector>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <strings.h>

#include "MakeHex.h"
#include "irp_definitions.h"

class IRP {
public:
    IRP();
    ~IRP();

    bool readIrpString(char* str);
    void generate(int* s, int* r, float* raw);

private:
    struct Value {
        double m_val;
        int m_bits;
    };

    unsigned int reverse(unsigned int Number);
    bool match(const char* master);
    void setDigit(int d);
    char* copy();
    void getPair(int* result);
    void parseVal(Value& result, char*& in, int prec = 0);
    int genHex(const char* Pattern);
    void genHex(float number);
    char* m_digits[16];
    int m_frequency;
    int m_timeBase;
    int m_messageTime;
    int m_device[2];
    int m_functions[4];
    bool m_msb;
    const char* m_form;
    const char* m_prefix;
    const char* m_suffix;
    const char* m_rPrefix;
    const char* m_rSuffix;
    const char* m_def[26];
    char m_bufr[1024];
    const char* m_next;
    int m_bitGroup;
    int m_pendingBits;
    double m_cumulative;
    unsigned int m_mask[33];
    std::vector<float> m_hex;

public:
    int m_value[26];
};

bool makehex_generate(const char *protocol, int32_t dev, int32_t sub, int32_t func,
                      uint32_t *pairs, uint16_t *count, uint16_t max_pairs, uint32_t *freq_hz) {
    *count = 0;
    if (protocol == NULL) {
        return false;
    }

    // Build the IRP string: device/sub-device/function values + protocol definition
    char irp[512] = "";
    if (sub >= 0) {
        snprintf(irp, sizeof(irp), "Device=%ld.%ld\nFunction=%ld\n", (long)dev, (long)sub, (long)func);
    } else {
        snprintf(irp, sizeof(irp), "Device=%ld\nFunction=%ld\n", (long)dev, (long)func);
    }

    // Look up the protocol definition (special cases first)
    const char *def = NULL;
    const char *name = protocol;
    char upper[64];
    strncpy(upper, protocol, sizeof(upper) - 1);
    upper[sizeof(upper) - 1] = '\0';
    for (char *u = upper; *u; u++) {
        *u = (char)toupper((unsigned char)*u);
    }

    int m = 0;
    int l = 0;
    if (sscanf(upper, "RC6-%d-%d", &m, &l) == 2) {
        char tmp2[64];
        snprintf(tmp2, sizeof(tmp2), "Define M=%d\nDefine L=%d\n", m, l);
        strncat(irp, tmp2, sizeof(irp) - strlen(irp) - 1);
        name = "rc6-M-L";
    } else if (strcmp(upper, "NEC") == 0) {
        name = "nec2";
    } else if (strcmp(upper, "NECX") == 0) {
        name = "NECx2";
    }

    for (uint8_t i = 0; i < makehex_protocol_count; i++) {
        if (strcasecmp(name, makehex_protocols[i].name) == 0) {
            def = makehex_protocols[i].def;
            break;
        }
    }
    if (def == NULL) {
        return false; // unknown protocol
    }

    strncat(irp, def, sizeof(irp) - strlen(irp) - 1);

    // Carrier frequency from the definition
    uint32_t frequency = 38000;
    const char *freq_str = strstr(def, "Frequency=");
    if (freq_str != NULL) {
        int f = 0;
        if (sscanf(freq_str, "Frequency=%d", &f) == 1 && f > 0) {
            frequency = (uint32_t)f;
        }
    }

    // Validate and parse the IRP string
    IRP irp_parser;
    if (!irp_parser.readIrpString(irp)) {
        return false;
    }

    // Set the command values
    irp_parser.m_value['D' - 'A'] = dev;
    irp_parser.m_value['S' - 'A'] = sub;
    irp_parser.m_value['F' - 'A'] = func;
    irp_parser.m_value['N' - 'A'] = -1;

    // Generate the sequence (bounded)
    int s = 0;
    int r = 0;
    float seq[512];
    irp_parser.generate(&s, &r, seq);
    int words = 2 * (s + r);
    if (words > 512) {
        words = 512;
    }
    if (words < 2) {
        return false;
    }

    // Convert to mark/space pairs, clamped to the 16 bit engine format
    uint16_t n_pairs = 0;
    for (int i = 0; (i + 1) < words && n_pairs < max_pairs; i += 2) {
        float mark = seq[i];
        float space = seq[i + 1];
        if (mark < 1) {
            mark = 1;
        }
        if (space < 1) {
            space = 1;
        }
        if (mark > 0xffff) {
            mark = 0xffff;
        }
        if (space > 0xffff) {
            space = 0xffff;
        }
        pairs[n_pairs++] = ((uint32_t)mark << 16) | (uint32_t)space;
    }
    if (n_pairs == 0) {
        return false;
    }

    *count = n_pairs;
    *freq_hz = frequency;
    return true;
}

unsigned int IRP::reverse(unsigned int Number) {
    Number = ((Number & 0x55555555) << 1) + ((Number >> 1) & 0x55555555);
    Number = ((Number & 0x33333333) << 2) + ((Number >> 2) & 0x33333333);
    Number = ((Number & 0xF0F0F0F)  << 4) + ((Number >> 4) & 0xF0F0F0F);
    Number = ((Number & 0xFF00FF)   << 8) + ((Number >> 8) & 0xFF00FF);
    return (Number >> 16) + (Number << 16);
}

IRP::IRP() :
    m_frequency(38400),
    m_timeBase(1),
    m_messageTime(0),
    m_msb(false),
    m_form(0),
    m_prefix(0),
    m_suffix(0),
    m_rPrefix(0),
    m_rSuffix(0),
    m_bitGroup(2)
{
    memset(m_value, 0, sizeof(m_value));
    memset(m_def, 0, sizeof(m_def));
    memset(m_device, -1, sizeof(m_device));
    memset(m_functions, -1, sizeof(m_functions));
    memset(m_digits, 0, sizeof(m_digits));
    m_mask[0] = 0;
    for (int ndx = 1; ndx < 33; ++ndx)
        m_mask[ndx] = 2 * m_mask[ndx - 1] + 1;
}

IRP::~IRP() {
    for (int i = 0; i < 26; ++i) {
        if (m_def[i]) {
            free((void *)m_def[i]);
        }
    }
    for (int i = 0; i < 16; ++i) {
        if (m_digits[i]) {
            free((void *)m_digits[i]);
        }
    }
    free((void *)m_prefix);
    free((void *)m_suffix);
    free((void *)m_rPrefix);
    free((void *)m_rSuffix);
    free((void *)m_form);
}

bool IRP::match(const char* master) {
    int len = strlen(master);
    m_next = m_bufr + len;
    return 0 == memcmp(m_bufr, master, len);
}
void IRP::setDigit(int d) {
    m_digits[d] = copy();
    while (d >= m_bitGroup) {
        m_bitGroup <<= 1;
    }
}

bool IRP::readIrpString(char* str) {
    char* line = strtok(str, "\n");
    while (line != NULL && line[0] != 0) {
        strcpy(m_bufr, line);
        line = m_bufr;

        Value val;

        // Convert to upper case, compress out blanks and discard comments
        for (char* l2 = line; *l2 && *l2 != '\'' && *l2 != '\n'; l2++) {
            if (*l2 != ' ' && *l2 != '\t')
                *(line++) = toupper(*l2);
        }
        *line = 0;

        // Recognise some Keywords
        if (match("FREQUENCY=")) {
            parseVal(val, const_cast<char*&>(m_next));
            m_frequency = val.m_val;
        } else if (match("TIMEBASE=")) {
            parseVal(val, const_cast<char*&>(m_next));
            m_timeBase = val.m_val;
        } else if (match("MESSAGETIME=")) {
            parseVal(val, const_cast<char*&>(m_next));
            if (val.m_bits == 0)
                val.m_val *= m_timeBase;
            m_messageTime = val.m_val;
        } else if (m_bufr[0] >= '0' && m_bufr[0] <= '9' && m_bufr[1] == '=') {
            m_next = m_bufr + 2;
            setDigit(m_bufr[0] - '0');
        } else if (m_bufr[0] == '1' && m_bufr[1] >= '0' && m_bufr[1] <= '5' && m_bufr[2] == '=') {
            m_next = m_bufr + 3;
            setDigit(m_bufr[1] + (10 - '0'));
        } else if (match("ZERO=")) {
            setDigit(0);
        } else if (match("ONE=")) {
            setDigit(1);
        } else if (match("TWO=")) {
            setDigit(2);
        } else if (match("THREE=")) {
            setDigit(3);
        } else if (match("PREFIX=")) {
            m_prefix = copy();
        } else if (match("SUFFIX=")) {
            m_suffix = copy();
        } else if (match("R-PREFIX=")) {
            m_rPrefix = copy();
        } else if (match("R-SUFFIX=")) {
            m_rSuffix = copy();
        } else if (match("FIRSTBIT=MSB")) {
            m_msb = true;
        } else if (match("FORM=")) {
            m_form = copy();
        } else if (match("DEFINE") || match("DEFAULT")) {
            if (m_next[1] == '=') {
                m_next += 2;
                m_def[m_next[-2] - 'A'] = copy();
            } else if (m_next[0] == '=' && m_next[2] == 'A' && m_next[3] == 'S') {
                m_next += 4;
                m_def[m_next[-3] - 'A'] = copy();
            } else if (m_next[1] == 'A' && m_next[2] == 'S') {
                m_next += 3;
                m_def[m_next[-3] - 'A'] = copy();
            }
        } else if (match("DEVICE=")) {
            getPair(m_device);
        } else if (match("FUNCTION=")) {
            getPair(m_functions);
            if (m_next[0] == '.' && m_next[1] == '.') {
                m_next += 2;
                getPair(m_functions + 2);
            }
        }

        line = strtok(NULL, "\n");
    }

    if (m_device[1] >= 0)
        m_def['S' - 'A'] = 0;
    if (m_functions[1] >= 0)
        m_def['N' - 'A'] = 0;

    return true;
}

void IRP::getPair(int* result) {
    for (int nIndex = 0; nIndex < 2; nIndex++) {
        int num = 0;
        for (; *m_next; m_next++) {
            if (*m_next >= '0' && *m_next <= '9')
                num = num * 10 + *m_next - '0';
            else
                break;
        }
        result[nIndex] = num;
        if (m_next[0] != '.' || m_next[1] < '0' || m_next[1] > '9')
            break;
        m_next++;
    }
}

char* IRP::copy() {
    return strdup(m_next);
}

void IRP::parseVal(Value& result, char*& in, int prec) {
    if (*in >= 'A' && *in <= 'Z') {
        int ndx = *(in++) - 'A';
        const char* in2 = m_def[ndx];
        if (in2) {
            parseVal(result, const_cast<char*&>(in2), 0);
            if (*in2) {
                // error
            }
        } else {
            result.m_val = m_value[ndx];
            result.m_bits = 0;
        }
    } else if (*in >= '0' && *in <= '9') {
        result.m_bits = 0;
        result.m_val = 0.0;
        do {
            result.m_val = result.m_val * 10 + *(in++) - '0';
        } while (*in >= '0' && *in <= '9');
    } else switch (*in) {
        case '-':
            ++in;
            parseVal(result, in, 1);
            result.m_val = -result.m_val;
            if (result.m_bits > 0)
                result.m_bits = 0;
            break;
        case '~':
            ++in;
            parseVal(result, in, 1);
            result.m_val = -(result.m_val + 1);
            if (result.m_bits > 0)
                result.m_val = (double)(((int)result.m_val) & m_mask[result.m_bits]);
            break;
        case '(':
            ++in;
            parseVal(result, in, 0);
            if (*in == ')') {
                ++in;
            } else {
                // error
            }
            break;
        default:
            // error
            break;
    }

    if (*in == 'M') {
        result.m_val *= 1000;
        result.m_bits = -1;
        ++in;
    } else if (*in == 'U') {
        result.m_bits = -1;
        ++in;
    }

    for (;;) {
        Value v2;
        if (prec < 2) {
            if (*in == '*') {
                ++in;
                parseVal(v2, in, 2);
                result.m_val *= v2.m_val;
                if (result.m_bits > 0)
                    result.m_bits = 0;
                continue;
            }
        }
        if (prec < 1) {
            switch (*in) {
                case '+':
                    ++in;
                    parseVal(v2, in, 1);
                    result.m_val += v2.m_val;
                    if (result.m_bits > 0)
                        result.m_bits = 0;
                    continue;
                case '-':
                    ++in;
                    parseVal(v2, in, 1);
                    result.m_val -= v2.m_val;
                    if (result.m_bits > 0)
                        result.m_bits = 0;
                    continue;
                case '^':
                    ++in;
                    parseVal(v2, in, 1);
                    result.m_val = ((int)result.m_val) ^ ((int)v2.m_val);
                    if (result.m_bits > 0 && (v2.m_bits <= 0 || v2.m_bits > result.m_bits))
                        result.m_bits = v2.m_bits;
                    continue;
            }
        }
        if (prec < 3) {
            if (*in == ':') {
                ++in;
                parseVal(v2, in, 3);
                result.m_bits = v2.m_val;
                if (*in == ':') {
                    ++in;
                    parseVal(v2, in, 3);
                    result.m_val = (double)(((int)result.m_val) >> ((int)v2.m_val));
                }
                if (result.m_bits < 0) {
                    result.m_bits = -result.m_bits;
                    result.m_val = (double)(reverse((int)result.m_val) >> (32 - result.m_bits));
                }
                result.m_val = (double)(((int)result.m_val) & m_mask[result.m_bits]);
                continue;
            }
        }
        break;
    }
}

void IRP::generate(int* s, int* r, float* raw) {
    m_hex.clear();
    m_cumulative = 0.0;
    m_pendingBits = (m_msb ? 1 : m_bitGroup);
    int Single = genHex(m_form);
    if (m_cumulative < m_messageTime)
        genHex(m_cumulative - m_messageTime);
    if (m_hex.size() & 1)
        genHex(-1.0);
    if (Single < 0)
        Single = m_hex.size();
    Single >>= 1;

    int unit;
    if (m_frequency) {
        unit = floor(4145146. / m_frequency + 0.5);
    } else {
        float mn = m_hex[0];
        for (int nIndex = 1; nIndex < (int)m_hex.size(); nIndex++)
            if (mn > m_hex[nIndex])
                mn = m_hex[nIndex];
        unit = 4.145146 * .125 * mn + 0.5;
        unit &= -2;
    }
    if (unit <= 0)
        unit = 1;
    (void)unit;

    for (int nIndex = 0; nIndex < (int)m_hex.size(); nIndex += 2) {
        raw[nIndex] = m_hex[nIndex];
        raw[nIndex + 1] = m_hex[nIndex + 1];
    }

    *s = Single;
    *r = m_hex.size() / 2 - Single;
    return;
}

int IRP::genHex(const char* Pattern) {
    int Result = -1;
    if (*Pattern == ';') {
        Result = 0; // Single section is empty
        Pattern++;
    }
    while (*Pattern) {
        if (*Pattern == '*') {
            genHex((Result >= 0 && m_rPrefix) ? m_rPrefix : m_prefix);
            Pattern++;
        } else if (*Pattern == '_') {
            genHex((Result >= 0 && m_rSuffix) ? m_rSuffix : m_suffix);
            Pattern++;
            if (m_cumulative < m_messageTime) {
                genHex(m_cumulative - m_messageTime);
            }
        } else if (*Pattern == '^') {
            Pattern++;

            Value val;
            parseVal(val, const_cast<char*&>(Pattern));
            if (val.m_bits == 0)
                val.m_val *= m_timeBase;

            if (m_cumulative < val.m_val) {
                genHex(m_cumulative - val.m_val);
            }
        } else {
            Value val;
            parseVal(val, const_cast<char*&>(Pattern));

            if (val.m_bits == 0)
                val.m_val *= m_timeBase;
            if (val.m_bits <= 0) {
                genHex(val.m_val);
            } else {
                int Number = (int)(val.m_val);
                if (m_msb)
                    Number = reverse(Number) >> (32 - val.m_bits);
                while (--val.m_bits >= 0) {
                    if (m_msb) {
                        m_pendingBits = (m_pendingBits << 1) + (Number & 1);
                        if (m_pendingBits & m_bitGroup) {
                            genHex(m_digits[m_pendingBits - m_bitGroup]);
                            m_pendingBits = 1;
                        }
                    } else {
                        m_pendingBits = (m_pendingBits >> 1) + (Number & 1) * m_bitGroup;
                        if (m_pendingBits & 1) {
                            genHex(m_digits[m_pendingBits >> 1]);
                            m_pendingBits = m_bitGroup;
                        }
                    }
                    Number >>= 1;
                }
            }
        }
        if (*Pattern == ';') {
            if (m_cumulative < m_messageTime) {
                genHex(m_cumulative - m_messageTime);
            }
            if (m_hex.size() & 1) {
                genHex(-1.0);
            }
            Result = m_hex.size();
            m_cumulative = 0.0;
        } else if (*Pattern != ',')
            break;
        Pattern++;
    }
    return Result;
}

void IRP::genHex(float number) {
    if (number == 0.0)
        return;
    int nHex = m_hex.size();
    if (number > 0) {
        m_cumulative += number;
        if (nHex & 1)
            m_hex[nHex - 1] += number;
        else
            m_hex.push_back(number);
    } else if (nHex) {
        m_cumulative -= number;
        if (nHex & 1)
            m_hex.push_back(-number);
        else
            m_hex[nHex - 1] -= number;
    }
}
