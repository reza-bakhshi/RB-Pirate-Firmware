#!/bin/sh
set -e
cd "$(dirname "$0")/.."

echo "=== Building and running resource lease manager test suite ==="
gcc -O2 -Wall -Wextra -std=c11 -pthread -Isrc \
    -o /tmp/test_resource \
    tests/test_resource.c \
    src/kernel/bp_resource.c

/tmp/test_resource

echo "--- AddressSanitizer + UBSan ---"
gcc -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -std=c11 -pthread -Isrc \
    -o /tmp/test_resource_asan \
    tests/test_resource.c \
    src/kernel/bp_resource.c

/tmp/test_resource_asan
echo "SANITIZERS: ALL PASS"
