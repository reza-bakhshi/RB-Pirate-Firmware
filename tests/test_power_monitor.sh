#!/bin/sh
set -e
cd "$(dirname "$0")/.."

echo "=== Building and running power monitor & ADC test suite ==="
gcc -O2 -Wall -Wextra -Wno-unused-variable -std=c11 -DBP_HOST_TEST -DBP_VER=6 -Isrc -Itests/stubs -lpthread \
    -o /tmp/test_power_monitor \
    tests/test_power_monitor.c \
    src/kernel/bp_config.c \
    src/kernel/bp_job.c \
    src/kernel/bp_resource.c \
    src/kernel/bp_event.c \
    src/pirate/amux.c \
    src/pirate/psu.c

/tmp/test_power_monitor

echo "--- AddressSanitizer + UBSan ---"
gcc -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Wno-unused-variable -std=c11 -DBP_HOST_TEST -DBP_VER=6 -Isrc -Itests/stubs -lpthread \
    -o /tmp/test_power_monitor_asan \
    tests/test_power_monitor.c \
    src/kernel/bp_config.c \
    src/kernel/bp_job.c \
    src/kernel/bp_resource.c \
    src/kernel/bp_event.c \
    src/pirate/amux.c \
    src/pirate/psu.c

/tmp/test_power_monitor_asan
echo "SANITIZERS: ALL PASS"
