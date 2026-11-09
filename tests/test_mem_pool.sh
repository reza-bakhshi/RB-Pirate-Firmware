#!/bin/sh
set -e
cd "$(dirname "$0")/.."

gcc -O2 -Wall -Wextra -std=c11 -Isrc -o /tmp/test_mem_pool tests/test_mem_pool.c src/pirate/mem_pool.c
/tmp/test_mem_pool

echo "--- AddressSanitizer + UBSan ---"
gcc -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -std=c11 -Isrc \
    -o /tmp/test_mem_pool_asan tests/test_mem_pool.c src/pirate/mem_pool.c
/tmp/test_mem_pool_asan
echo "SANITIZERS: ALL PASS"
