#!/bin/sh
set -e
cd "$(dirname "$0")/.."

echo "=== Building and running binary transport test suite ==="
gcc -O2 -Wall -Wextra -std=c11 -DBP_HOST_TEST -Isrc \
    -o /tmp/test_bin_transport \
    tests/test_bin_transport.c \
    src/services/bp_bin_transport.c

/tmp/test_bin_transport

echo "--- AddressSanitizer + UBSan ---"
gcc -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -std=c11 -DBP_HOST_TEST -Isrc \
    -o /tmp/test_bin_transport_asan \
    tests/test_bin_transport.c \
    src/services/bp_bin_transport.c

/tmp/test_bin_transport_asan
echo "SANITIZERS: ALL PASS"
