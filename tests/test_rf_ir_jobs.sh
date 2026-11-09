#!/bin/sh
set -e
cd "$(dirname "$0")/.."

echo "=== Building and running RF & IR protocol jobs test suite ==="
gcc -O2 -Wall -Wextra -std=c11 -Isrc \
    -o /tmp/test_rf_ir_jobs \
    tests/test_rf_ir_jobs.c \
    src/kernel/bp_job.c \
    src/kernel/bp_resource.c

/tmp/test_rf_ir_jobs

echo "--- AddressSanitizer + UBSan ---"
gcc -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -std=c11 -Isrc \
    -o /tmp/test_rf_ir_jobs_asan \
    tests/test_rf_ir_jobs.c \
    src/kernel/bp_job.c \
    src/kernel/bp_resource.c

/tmp/test_rf_ir_jobs_asan
echo "SANITIZERS: ALL PASS"
