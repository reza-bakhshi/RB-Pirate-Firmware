#!/bin/sh
set -e
# Host-side unit test suite for Cellular AT subsystem (F8)
cd "$(dirname "$0")/.." || exit 1

echo "=== Building and running Cellular AT test suite ==="
gcc -O2 -Wall -Wextra -std=c11 -DBP_HOST_TEST -Itests/stubs -Isrc \
    -o /tmp/test_cell \
    tests/test_cell.c src/kernel/bp_resource.c || exit 1
/tmp/test_cell

echo "--- AddressSanitizer + UBSan ---"
gcc -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Wextra -std=c11 \
    -DBP_HOST_TEST -Itests/stubs -Isrc \
    -o /tmp/test_cell_asan \
    tests/test_cell.c src/kernel/bp_resource.c || exit 1
ASAN_OPTIONS=detect_leaks=0 /tmp/test_cell_asan > /dev/null && echo "SANITIZERS: ALL PASS"
rm -f /tmp/test_cell /tmp/test_cell_asan
