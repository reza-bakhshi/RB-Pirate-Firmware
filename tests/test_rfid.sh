#!/bin/sh
set -e
# Host-side unit test suite for PN532 RFID subsystem (F5)
cd "$(dirname "$0")/.." || exit 1

echo "=== Building and running PN532 RFID test suite ==="
gcc -O2 -Wall -Wextra -std=c11 -DBP_HOST_TEST -Itests/stubs -Isrc \
    -o /tmp/test_rfid \
    tests/test_rfid.c src/pirate/pn532.c src/kernel/bp_resource.c || exit 1
/tmp/test_rfid

echo "--- AddressSanitizer + UBSan ---"
gcc -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Wextra -std=c11 \
    -DBP_HOST_TEST -Itests/stubs -Isrc \
    -o /tmp/test_rfid_asan \
    tests/test_rfid.c src/pirate/pn532.c src/kernel/bp_resource.c || exit 1
ASAN_OPTIONS=detect_leaks=0 /tmp/test_rfid_asan > /dev/null && echo "SANITIZERS: ALL PASS"
rm -f /tmp/test_rfid /tmp/test_rfid_asan
