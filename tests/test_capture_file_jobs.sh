#!/bin/sh
set -e
cd "$(dirname "$0")/.."

echo "=== Building and running capture & file jobs test suite ==="
gcc -O2 -Wall -Wextra -std=c11 -Isrc \
    -o /tmp/test_capture_file_jobs \
    tests/test_capture_file_jobs.c \
    src/kernel/bp_job.c \
    src/kernel/bp_resource.c

/tmp/test_capture_file_jobs

echo "--- AddressSanitizer + UBSan ---"
gcc -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -std=c11 -Isrc \
    -o /tmp/test_capture_file_jobs_asan \
    tests/test_capture_file_jobs.c \
    src/kernel/bp_job.c \
    src/kernel/bp_resource.c

/tmp/test_capture_file_jobs_asan
echo "SANITIZERS: ALL PASS"
