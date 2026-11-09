#!/bin/sh
set -e
# Host-side unit test suite for LCDTap subsystem (F7)
cd "$(dirname "$0")/.." || exit 1

echo "=== Building and running LCDTap test suite ==="
gcc -O2 -Wall -Wextra -std=c11 -DBP_HOST_TEST -Itests/stubs -Isrc \
    -o /tmp/test_lcdtap \
    tests/test_lcdtap.c src/kernel/bp_resource.c src/kernel/bp_job.c -lpthread || exit 1
/tmp/test_lcdtap

echo "--- AddressSanitizer + UBSan ---"
gcc -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Wextra -std=c11 \
    -DBP_HOST_TEST -Itests/stubs -Isrc \
    -o /tmp/test_lcdtap_asan \
    tests/test_lcdtap.c src/kernel/bp_resource.c src/kernel/bp_job.c -lpthread || exit 1
ASAN_OPTIONS=detect_leaks=0 /tmp/test_lcdtap_asan > /dev/null && echo "SANITIZERS: ALL PASS"
rm -f /tmp/test_lcdtap /tmp/test_lcdtap_asan
