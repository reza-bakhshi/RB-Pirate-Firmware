#!/bin/sh
# Host-side tests for the IR protocol engine (src/pirate/ir_protocols.c):
# 1. round-trip encode/decode of every hand-written protocol
# 2. cross-validation of the MakeHex IRP encoder (src/lib/makehex) against
#    the hand-written encoders - two independent implementations of the
#    same wire formats must agree
cd "$(dirname "$0")/.." || exit 1
gcc -c -O2 -Wall -Wextra -Itests/stubs -Isrc -o tests/test_ir_protocols_c.o tests/test_ir_protocols.c || exit 1
gcc -c -O2 -Wall -Itests/stubs -Isrc -o tests/test_ir_protocols_e.o src/pirate/ir_protocols.c || exit 1
gcc -c -O2 -Wall -Itests/stubs -Isrc -o tests/test_ir_protocols_i.o src/lib/makehex/irp_definitions.c || exit 1
g++ -c -O2 -Wall -Itests/stubs -Isrc -o tests/test_ir_protocols_m.o src/lib/makehex/MakeHex.cpp || exit 1
g++ -o tests/test_ir_protocols tests/test_ir_protocols_c.o tests/test_ir_protocols_e.o tests/test_ir_protocols_i.o tests/test_ir_protocols_m.o || exit 1
rm -f tests/test_ir_protocols_c.o tests/test_ir_protocols_e.o tests/test_ir_protocols_i.o tests/test_ir_protocols_m.o
./tests/test_ir_protocols
