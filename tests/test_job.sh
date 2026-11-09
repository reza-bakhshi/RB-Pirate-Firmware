#!/bin/sh
set -e
cd "$(dirname "$0")/.."

echo "=== Building and running protocol job test suite ==="
gcc -O2 -Wall -Wextra -std=c11 -Isrc \
    -o /tmp/test_job \
    tests/test_job.c \
    src/kernel/bp_job.c \
    src/kernel/bp_resource.c

/tmp/test_job

echo "--- AddressSanitizer + UBSan ---"
gcc -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -std=c11 -Isrc \
    -o /tmp/test_job_asan \
    tests/test_job.c \
    src/kernel/bp_job.c \
    src/kernel/bp_resource.c

/tmp/test_job_asan
echo "SANITIZERS: ALL PASS"
