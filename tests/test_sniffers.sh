#!/bin/sh
set -e
# Host-side unit test suite for PS/2 and USB sniffer subsystems (F9)
cd "$(dirname "$0")/.." || exit 1

echo "=== Building and running Sniffers test suite ==="
gcc -O2 -Wall -Wextra -std=c11 -DBP_HOST_TEST -Itests/stubs -Isrc \
    -o /tmp/test_sniffers \
    tests/test_sniffers.c src/kernel/bp_resource.c src/kernel/bp_job.c -lpthread || exit 1
/tmp/test_sniffers

echo "--- AddressSanitizer + UBSan ---"
gcc -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Wextra -std=c11 \
    -DBP_HOST_TEST -Itests/stubs -Isrc \
    -o /tmp/test_sniffers_asan \
    tests/test_sniffers.c src/kernel/bp_resource.c src/kernel/bp_job.c -lpthread || exit 1
ASAN_OPTIONS=detect_leaks=0 /tmp/test_sniffers_asan > /dev/null && echo "SANITIZERS: ALL PASS"
rm -f /tmp/test_sniffers /tmp/test_sniffers_asan
