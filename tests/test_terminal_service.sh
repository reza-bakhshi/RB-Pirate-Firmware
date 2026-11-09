#!/bin/sh
set -e
cd "$(dirname "$0")/.."

echo "=== Building and running terminal service test suite ==="
gcc -O2 -Wall -Wextra -std=c11 -DBP_HOST_TEST -Isrc \
    -o /tmp/test_terminal_service \
    tests/test_terminal_service.c \
    src/services/bp_terminal.c \
    src/kernel/bp_job.c \
    src/kernel/bp_resource.c

/tmp/test_terminal_service

echo "--- AddressSanitizer + UBSan ---"
gcc -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -std=c11 -DBP_HOST_TEST -Isrc \
    -o /tmp/test_terminal_service_asan \
    tests/test_terminal_service.c \
    src/services/bp_terminal.c \
    src/kernel/bp_job.c \
    src/kernel/bp_resource.c

/tmp/test_terminal_service_asan
echo "SANITIZERS: ALL PASS"
