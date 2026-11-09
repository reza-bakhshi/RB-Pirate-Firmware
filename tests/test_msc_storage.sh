#!/bin/sh
set -e
cd "$(dirname "$0")/.."

echo "=== Building and running MSC storage test suite ==="
gcc -O2 -Wall -Wextra -std=c11 -DBP_HOST_TEST -Isrc -Itests/stubs -DCFG_TUD_MSC=1 \
    -o /tmp/test_msc_storage \
    tests/test_msc_storage.c \
    src/msc_disk.c \
    src/services/bp_storage.c

/tmp/test_msc_storage

echo "--- AddressSanitizer + UBSan ---"
gcc -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -std=c11 -DBP_HOST_TEST -Isrc -Itests/stubs -DCFG_TUD_MSC=1 \
    -o /tmp/test_msc_storage_asan \
    tests/test_msc_storage.c \
    src/msc_disk.c \
    src/services/bp_storage.c

/tmp/test_msc_storage_asan
echo "SANITIZERS: ALL PASS"
