/* SubGHz Flipper .sub file field parsers (portable C, host-testable).
 * Covers RAW and Key files: Filetype/Version/Frequency/Preset/Protocol/
 * Bit/Key/TE/Manufacture/Custom_preset_data fields.
 * Copyright (c) 2026 Bus Pirate contributors. SPDX-License-Identifier: MIT
 */
#ifndef BP_SUBGHZ_FILE_H
#define BP_SUBGHZ_FILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Exact match of the value part of a "Prefix: value" line (trims spaces). */
bool subghz_field_value_equals(const char *line, size_t prefix_length, const char *expected);

/* "Frequency: 433920000" (Hz, must be a whole kHz in a CC1101 band). */
bool subghz_parse_frequency_khz(const char *line, uint32_t *frequency_khz);

/* "Bit: 24" (1-128). */
bool subghz_parse_bit_field(const char *line, uint32_t *bits);

/* "Key: 00 A6 8D ..." hex bytes MSB-first (max 8 bytes). Sets *key_bits. */
bool subghz_parse_key_field(const char *line, uint64_t *key, uint32_t *key_bits);

/* "TE: 350" (50-100000). */
bool subghz_parse_te_field(const char *line, uint32_t *te_us);

/* "Custom_preset_data: 02 0D ..." addr/val pairs (max out_pairs). */
bool subghz_parse_custom_preset(const char *line, uint8_t *pairs, size_t *pair_count,
                                size_t max_pairs);

/* Flipper-exact custom preset: register pairs, then "00 00" terminator,
 * then exactly 8 PA-table bytes. Legacy pairs-only lines (no terminator)
 * are still accepted with *have_pa == false. */
#define SUBGHZ_CUSTOM_PA_SIZE 8u
bool subghz_parse_custom_preset_full(const char *line, uint8_t *pairs, size_t *pair_count,
                                     size_t max_pairs, uint8_t *pa_table, bool *have_pa);

/* "Manufacture: Foo" free text (truncated to out_size - 1). */
bool subghz_parse_manufacture(const char *line, char *out, size_t out_size);

/* BinRAW fields (Flipper .sub BinRAW files). */
#define SUBGHZ_BINRAW_MAX_BITS 4096u
#define SUBGHZ_BINRAW_MAX_BYTES (SUBGHZ_BINRAW_MAX_BITS / 8u)
/* "Bit_RAW: 260" (1..4096). Total-file "Bit:" uses subghz_parse_bit_field. */
bool subghz_parse_bit_raw_field(const char *line, uint32_t *bits);
/* "Data_RAW: 00 0F ..." hex bytes MSB-first (1..512 bytes). */
bool subghz_parse_data_raw_field(const char *line, uint8_t *bytes, size_t *byte_count,
                                 size_t max_bytes);
/* Expand one Bit_RAW/Data_RAW block: each bit is one TE interval,
 * 1 = carrier (high), 0 = no carrier (low), MSB-first. Run-length encodes
 * into Flipper pulse form (positive = high, negative = low). Appends to
 * pulses[*pulse_count] (capacity total). Returns false on bad input. */
bool subghz_binraw_expand(uint32_t te_us, const uint8_t *bytes, uint32_t bit_len,
                          int32_t *pulses, uint32_t *pulse_count, uint32_t capacity);

#ifdef __cplusplus
}
#endif

#endif
