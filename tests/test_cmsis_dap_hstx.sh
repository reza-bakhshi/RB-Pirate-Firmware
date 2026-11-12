#!/bin/sh
set -e
cd "$(dirname "$0")/.."

echo "=== Building and running CMSIS-DAP v2 & HSTX DVI test suite ==="
gcc -O2 -Wall -Wextra -Wno-unused-variable -std=c11 -D_GNU_SOURCE -DBP_HOST_TEST -DBP_VER=6 -Itests/stubs -Isrc -lpthread -lm \
    -o /tmp/test_cmsis_dap_hstx \
    tests/test_cmsis_dap_hstx.c \
    src/binmode/cmsis_dap.c \
    src/pirate/hstx_dvi.c \
    src/pirate/font8x16.c

/tmp/test_cmsis_dap_hstx

echo "--- AddressSanitizer + UBSan ---"
gcc -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Wno-unused-variable -std=c11 -D_GNU_SOURCE -DBP_HOST_TEST -DBP_VER=6 -Itests/stubs -Isrc -lpthread -lm \
    -o /tmp/test_cmsis_dap_hstx_asan \
    tests/test_cmsis_dap_hstx.c \
    src/binmode/cmsis_dap.c \
    src/pirate/hstx_dvi.c \
    src/pirate/font8x16.c

/tmp/test_cmsis_dap_hstx_asan
echo "SANITIZERS: ALL PASS"
