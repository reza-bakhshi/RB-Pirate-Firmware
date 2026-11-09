#!/bin/sh
set -e
cd "$(dirname "$0")/.."

echo "=== Building and running kernel test suite ==="
gcc -O2 -Wall -Wextra -Wno-unused-variable -std=c11 -DBP_HOST_TEST -Isrc \
    -o /tmp/test_kernel \
    tests/test_kernel.c \
    src/kernel/bp_event.c \
    src/kernel/bp_timer.c \
    src/kernel/bp_work.c \
    src/kernel/bp_op.c \
    src/kernel/bp_log.c \
    src/kernel/bp_health.c \
    src/kernel/bp_job.c \
    src/kernel/bp_resource.c \
    src/mode/bp_mode.c \
    src/kernel/bp_protocol.c \
    src/kernel/bp_config.c \
    src/services/bp_terminal.c \
    src/services/bp_storage.c \
    src/services/bp_bin_transport.c \
    src/kernel/bp_kernel.c

/tmp/test_kernel

echo "--- AddressSanitizer + UBSan ---"
gcc -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Wno-unused-variable -std=c11 -DBP_HOST_TEST -Isrc \
    -o /tmp/test_kernel_asan \
    tests/test_kernel.c \
    src/kernel/bp_event.c \
    src/kernel/bp_timer.c \
    src/kernel/bp_work.c \
    src/kernel/bp_op.c \
    src/kernel/bp_log.c \
    src/kernel/bp_health.c \
    src/kernel/bp_job.c \
    src/kernel/bp_resource.c \
    src/mode/bp_mode.c \
    src/kernel/bp_protocol.c \
    src/kernel/bp_config.c \
    src/services/bp_terminal.c \
    src/services/bp_storage.c \
    src/services/bp_bin_transport.c \
    src/kernel/bp_kernel.c

/tmp/test_kernel_asan
echo "SANITIZERS: ALL PASS"
