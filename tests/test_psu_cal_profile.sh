#!/bin/sh
set -e
cd "$(dirname "$0")/.."

echo "=== Building and running PSU profiler & calibration test suite ==="
gcc -O2 -Wall -Wextra -Wno-unused-variable -std=c11 -D_GNU_SOURCE -DBP_HOST_TEST -DBP_VER=6 -Isrc -Itests/stubs -lpthread -lm \
    -o /tmp/test_psu_cal_profile \
    tests/test_psu_cal_profile.c \
    src/kernel/bp_config.c \
    src/kernel/bp_job.c \
    src/kernel/bp_resource.c \
    src/kernel/bp_event.c \
    src/pirate/amux.c \
    src/pirate/psu.c \
    src/mjson/mjson.c

/tmp/test_psu_cal_profile

echo "--- AddressSanitizer + UBSan ---"
gcc -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Wno-unused-variable -std=c11 -D_GNU_SOURCE -DBP_HOST_TEST -DBP_VER=6 -Isrc -Itests/stubs -lpthread -lm \
    -o /tmp/test_psu_cal_profile_asan \
    tests/test_psu_cal_profile.c \
    src/kernel/bp_config.c \
    src/kernel/bp_job.c \
    src/kernel/bp_resource.c \
    src/kernel/bp_event.c \
    src/pirate/amux.c \
    src/pirate/psu.c \
    src/mjson/mjson.c

/tmp/test_psu_cal_profile_asan
echo "SANITIZERS: ALL PASS"
