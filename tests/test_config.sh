#!/bin/sh
set -e
cd "$(dirname "$0")/.."

echo "=== Building and running configuration & snapshot test suite ==="
gcc -O2 -Wall -Wextra -Wno-unused-variable -std=c11 -DBP_HOST_TEST -Isrc \
    -o /tmp/test_config \
    tests/test_config.c \
    src/kernel/bp_config.c

/tmp/test_config

echo "--- AddressSanitizer + UBSan ---"
gcc -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Wno-unused-variable -std=c11 -DBP_HOST_TEST -Isrc \
    -o /tmp/test_config_asan \
    tests/test_config.c \
    src/kernel/bp_config.c

/tmp/test_config_asan
echo "SANITIZERS: ALL PASS"
