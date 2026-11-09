#!/bin/sh
# Host-side SPSC queue stress test.
set -e
cd "$(dirname "$0")/.."
gcc -O2 -Wall -Wextra -Wpedantic -pthread -Itests/stubs -o tests/test_spsc_queue tests/test_spsc_queue.c
./tests/test_spsc_queue
