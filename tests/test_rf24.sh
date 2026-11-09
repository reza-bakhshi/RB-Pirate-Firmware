#!/bin/sh
set -e
# Host-side unit test suite for nRF24L01 RF24 subsystem (F6)
cd "$(dirname "$0")/.." || exit 1

echo "=== Building and running nRF24L01 RF24 test suite ==="
gcc -O2 -Wall -Wextra -std=c11 -DBP_HOST_TEST -Itests/stubs -Isrc \
    -o /tmp/test_rf24 \
    tests/test_rf24.c src/pirate/nrf24l01.c src/kernel/bp_resource.c || exit 1
/tmp/test_rf24

echo "--- AddressSanitizer + UBSan ---"
gcc -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Wextra -std=c11 \
    -DBP_HOST_TEST -Itests/stubs -Isrc \
    -o /tmp/test_rf24_asan \
    tests/test_rf24.c src/pirate/nrf24l01.c src/kernel/bp_resource.c || exit 1
ASAN_OPTIONS=detect_leaks=0 /tmp/test_rf24_asan > /dev/null && echo "SANITIZERS: ALL PASS"
rm -f /tmp/test_rf24 /tmp/test_rf24_asan
