#!/bin/sh
# Host-side terminal TX policy test.
set -e
cd "$(dirname "$0")/.."
if grep -n "spsc_queue_add_blocking(&tx_fifo" src/usb_tx.c; then
    echo "FAIL: usb_tx.c must not use the blocking terminal producer"
    exit 1
fi

gcc -O2 -Wall -Wextra -Wpedantic -Itests/stubs -Isrc -o tests/test_terminal_tx tests/test_terminal_tx.c
./tests/test_terminal_tx
