#!/bin/sh
set -e
cd "$(dirname "$0")/.."

echo "=== Building and running Storage Capacity & CSD Parsing test suite ==="

gcc -O2 -Wall -Wextra -Wno-unused-variable -std=c11 -DBP_HOST_TEST -DBP_VER=6 -Isrc -Itests/stubs -lpthread \
    -o /tmp/test_storage_capacity \
    tests/test_storage_capacity.c \
    src/kernel/bp_resource.c \
    src/fatfs/tf_card.c

/tmp/test_storage_capacity

echo "--- AddressSanitizer + UBSan ---"
gcc -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Wno-unused-variable -std=c11 -DBP_HOST_TEST -DBP_VER=6 -Isrc -Itests/stubs -lpthread \
    -o /tmp/test_storage_capacity_asan \
    tests/test_storage_capacity.c \
    src/kernel/bp_resource.c \
    src/fatfs/tf_card.c

/tmp/test_storage_capacity_asan
echo "SANITIZERS: ALL PASS"
