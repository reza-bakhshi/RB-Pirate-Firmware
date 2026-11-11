#!/bin/sh
set -e
cd "$(dirname "$0")/.."

echo "=== Building and running storage service test suite ==="
gcc -O2 -Wall -Wextra -std=c11 -DBP_HOST_TEST -Isrc -Itests/stubs \
    -o /tmp/test_storage_service \
    tests/test_storage_service.c

/tmp/test_storage_service

echo "--- AddressSanitizer + UBSan ---"
gcc -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -std=c11 -DBP_HOST_TEST -Isrc -Itests/stubs \
    -o /tmp/test_storage_service_asan \
    tests/test_storage_service.c

/tmp/test_storage_service_asan
echo "SANITIZERS: ALL PASS"
