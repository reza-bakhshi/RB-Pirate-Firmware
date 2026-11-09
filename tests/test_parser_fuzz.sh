#!/bin/sh
set -e
cd "$(dirname "$0")/.."

echo "=== Building and running syntax parser fuzz test suite ==="

gcc -O2 -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable -std=c11 -DBP_HOST_TEST \
    -Isrc -Itests/stubs -include tests/stubs/pirate.h -include tests/stubs/pico/stdlib.h \
    -o /tmp/test_parser_fuzz \
    tests/test_parser_fuzz.c \
    src/syntax_compile.c \
    src/ui/ui_parse.c \
    src/lib/bp_linenoise/ln_cmdreader.c \
    src/lib/bp_number/bp_number.c

/tmp/test_parser_fuzz

echo "--- AddressSanitizer + UBSan ---"
gcc -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable -std=c11 -DBP_HOST_TEST \
    -Isrc -Itests/stubs -include tests/stubs/pirate.h -include tests/stubs/pico/stdlib.h \
    -o /tmp/test_parser_fuzz_asan \
    tests/test_parser_fuzz.c \
    src/syntax_compile.c \
    src/ui/ui_parse.c \
    src/lib/bp_linenoise/ln_cmdreader.c \
    src/lib/bp_number/bp_number.c

/tmp/test_parser_fuzz_asan
echo "SANITIZERS: ALL PASS"
