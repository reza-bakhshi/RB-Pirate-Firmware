#!/bin/sh
# Host-side round-trip test for the IR protocol engine (src/pirate/ir_protocols.c)
# Encodes a command for every supported protocol, normalizes the output into
# what the RX engine would capture, and verifies the decoder recovers it.
cd "$(dirname "$0")/.." || exit 1
gcc -O2 -Wall -Wextra -Itests/stubs -Isrc -o tests/test_ir_protocols tests/test_ir_protocols.c src/pirate/ir_protocols.c || exit 1
./tests/test_ir_protocols
