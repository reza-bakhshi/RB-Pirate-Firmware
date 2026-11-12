#!/bin/sh
set -e
cd "$(dirname "$0")/.."

echo "=== Building and running DMA pipeline & multi-block storage test suite ==="

gcc -O2 -Wall -Wextra -Wno-unused-variable -std=c11 -DBP_HOST_TEST -DBP_VER=6 -Isrc -Itests/stubs -lpthread \
    -o /tmp/test_dma_pipeline \
    tests/test_dma_pipeline.c \
    src/kernel/bp_resource.c \
    src/pirate/amux.c \
    src/pirate/lcd.c \
    src/fatfs/tf_card.c

/tmp/test_dma_pipeline

echo "--- AddressSanitizer + UBSan ---"
gcc -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Wno-unused-variable -std=c11 -DBP_HOST_TEST -DBP_VER=6 -Isrc -Itests/stubs -lpthread \
    -o /tmp/test_dma_pipeline_asan \
    tests/test_dma_pipeline.c \
    src/kernel/bp_resource.c \
    src/pirate/amux.c \
    src/pirate/lcd.c \
    src/fatfs/tf_card.c

/tmp/test_dma_pipeline_asan
echo "SANITIZERS: ALL PASS"
