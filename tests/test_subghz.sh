#!/bin/sh
# Host unit test for the SubGHz protocol library (no Pico SDK needed).
set -e
cd "$(dirname "$0")/.."
SRCS="tests/test_subghz.c src/mode/subghz_blocks.c src/mode/subghz_protocols.c src/mode/subghz_rx.c src/mode/subghz_keystore.c src/mode/subghz_file.c"
gcc -O2 -Wall -Wextra -std=c11 -Isrc -o /tmp/test_subghz $SRCS
/tmp/test_subghz
echo "--- AddressSanitizer + UBSan ---"
gcc -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -std=c11 -Isrc \
    -o /tmp/test_subghz_asan $SRCS
# LeakSanitizer is unavailable in some ptrace-based CI sandboxes; ASan and
# UBSan remain enabled for the actual memory/undefined-behavior checks.
ASAN_OPTIONS=detect_leaks=0 /tmp/test_subghz_asan > /dev/null && echo "SANITIZERS: ALL PASS"
