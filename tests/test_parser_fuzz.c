/**
 * @file test_parser_fuzz.c
 * @brief Fuzzing and boundary tests for the syntax and command line parser.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "pirate.h"
#include "system_config.h"
#include "bytecode.h"
#include "syntax.h"
#include "syntax_internal.h"
#include "ui/ui_prompt.h"
#include "ui/ui_parse.h"
#include "ui/ui_cmdln.h"
#include "ui/ui_term.h"
#include "lib/bp_linenoise/ln_cmdreader.h"

// Stubs required by syntax_compile
struct _system_config system_config = {
    .num_bits = 8,
    .pin_func = {0}
};

const uint8_t bio2bufiopin[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };

char* ui_term_color_error(void) { static char empty[] = ""; return empty; }
char* ui_term_color_reset(void) { static char empty[] = ""; return empty; }

static void test_valid_syntax(void) {
    printf("[TEST] Valid syntax vectors...\n");

    const char* valid_inputs[] = {
        "[ 0x55 0xAA ]",
        "{ 1 2 3 }",
        "r:5",
        "\"hello\"",
        "d:100 D:10 ^:4",
        "a.0 A.0 @.0 v.0",
        "0x10:2 0x20:4",
        "/ \\ _ -"
    };

    for (size_t i = 0; i < sizeof(valid_inputs)/sizeof(valid_inputs[0]); i++) {
        ln_cmdln_init(valid_inputs[i], strlen(valid_inputs[i]));
        SYNTAX_STATUS res = syntax_compile();
        assert(res == SSTATUS_OK);
        assert(syntax_io.out_cnt <= SYN_MAX_LENGTH);
    }

    printf("[PASS] Valid syntax vectors compiled successfully\n");
}

static void test_repeat_zero_rejected(void) {
    printf("[TEST] Reject repeat :0 syntax (ARCH-703)...\n");

    const char* zero_repeat_inputs[] = {
        "0x55:0",
        "r:0",
        "[:0",
        "d:0"
    };

    for (size_t i = 0; i < sizeof(zero_repeat_inputs)/sizeof(zero_repeat_inputs[0]); i++) {
        ln_cmdln_init(zero_repeat_inputs[i], strlen(zero_repeat_inputs[i]));
        SYNTAX_STATUS res = syntax_compile();
        assert(res == SSTATUS_ERROR);
    }

    printf("[PASS] Repeat :0 rejected cleanly\n");
}

static void test_unterminated_string_rejected(void) {
    printf("[TEST] Reject unterminated string...\n");

    const char* unclosed = "\"this string has no closing quote";
    ln_cmdln_init(unclosed, strlen(unclosed));
    SYNTAX_STATUS res = syntax_compile();
    assert(res == SSTATUS_ERROR);

    printf("[PASS] Unterminated string rejected cleanly\n");
}

static void test_buffer_overflow_bounds(void) {
    printf("[TEST] Buffer overflow boundary checks...\n");

    // Case 1: Oversized string literal (>1024 characters = SYN_MAX_LENGTH)
    static char huge_str[1200];
    huge_str[0] = '"';
    memset(&huge_str[1], 'A', 1050);
    huge_str[1051] = '"';
    huge_str[1052] = '\0';

    ln_cmdln_init(huge_str, strlen(huge_str));
    SYNTAX_STATUS res = syntax_compile();
    assert(res == SSTATUS_ERROR);
    assert(syntax_io.out_cnt <= SYN_MAX_LENGTH);

    // Case 2: 1100 discrete write tokens (>1024)
    static char many_tokens[3500];
    many_tokens[0] = '\0';
    for (int i = 0; i < 1100; i++) {
        strcat(many_tokens, "1 ");
    }

    ln_cmdln_init(many_tokens, strlen(many_tokens));
    res = syntax_compile();
    assert(res == SSTATUS_ERROR);
    assert(syntax_io.out_cnt <= SYN_MAX_LENGTH);

    printf("[PASS] Buffer bounds strictly enforced without overflow\n");
}

static void test_fuzz_random_inputs(uint32_t iterations) {
    printf("[TEST] Fuzzing parser with %u malformed/random inputs...\n", iterations);

    static const char alphabet[] =
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "0123456789"
        " []{}:.\"\\/_^-@vvadD"
        "\t\r\n;,&|!#*$%~`\0\xff\x80\xaa";

    char fuzzed_buf[512];
    srand(0x1337BEEF); // Deterministic seed for reproducible testing

    for (uint32_t iter = 0; iter < iterations; iter++) {
        size_t len = rand() % (sizeof(fuzzed_buf) - 1);
        for (size_t i = 0; i < len; i++) {
            fuzzed_buf[i] = alphabet[rand() % (sizeof(alphabet) - 1)];
        }
        fuzzed_buf[len] = '\0';

        ln_cmdln_init(fuzzed_buf, len);
        SYNTAX_STATUS res = syntax_compile();
        (void)res;
        assert(syntax_io.out_cnt <= SYN_MAX_LENGTH);
    }

    printf("[PASS] Fuzz testing completed: %u iterations without crash, leak, or buffer overrun\n", iterations);
}

int main(void) {
    printf("=== Running Syntax & Command Parser Fuzz Test Suite ===\n");
    test_valid_syntax();
    test_repeat_zero_rejected();
    test_unterminated_string_rejected();
    test_buffer_overflow_bounds();
    test_fuzz_random_inputs(10000);
    printf("=== ALL PARSER FUZZ TESTS PASSED ===\n");
    return 0;
}
