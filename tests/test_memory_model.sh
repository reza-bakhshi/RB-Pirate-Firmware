#!/bin/sh
set -e
cd "$(dirname "$0")/.."

BUILD_DIR="${BP_TEST_BUILD_DIR:-build_rp2350}"
ELF="$BUILD_DIR/src/bus_pirate6.elf"
OBJDIR="$BUILD_DIR/src/CMakeFiles/bus_pirate6.dir"

if [ ! -f "$ELF" ]; then
    echo "SKIP: $ELF not found. Build target bus_pirate6 first."
    exit 77
fi

command -v arm-none-eabi-nm >/dev/null
command -v arm-none-eabi-size >/dev/null

echo "=== Static Memory Model Guard Test ==="

# 1. Check FatFS heap elimination
echo "[CHECK] FatFS ffsystem.c.o heap elimination..."
symbols=$(arm-none-eabi-nm "$OBJDIR/fatfs/ffsystem.c.o")
if printf "%s\n" "$symbols" | grep -E "\b(malloc|free|ff_memalloc)\b"; then
    echo "FAIL: ffsystem.c.o references heap allocators"
    exit 1
fi
echo "[PASS] FatFS is clean (zero heap)"

# 2. Check SubGHz protocol pool
echo "[CHECK] SubGHz protocols.c.o heap elimination..."
symbols=$(arm-none-eabi-nm "$OBJDIR/mode/subghz_protocols.c.o")
if printf "%s\n" "$symbols" | grep -E "\b(malloc|free|calloc|realloc)\b"; then
    echo "FAIL: subghz_protocols.c.o references heap allocators"
    exit 1
fi
echo "[PASS] SubGHz protocols is clean (zero heap)"

# 3. Check FlatCC BPIO packet emission
echo "[CHECK] BPIO bpio.c.o heap elimination..."
symbols=$(arm-none-eabi-nm "$OBJDIR/binmode/bpio.c.o")
if printf "%s\n" "$symbols" | grep -E "\b(malloc|free|calloc|realloc)\b"; then
    echo "FAIL: bpio.c.o references heap allocators"
    exit 1
fi
echo "[PASS] BPIO is clean (zero heap)"

# 4. Check LCDTap backend
echo "[CHECK] LCDTap lcdtap_backend.cpp.o heap elimination..."
symbols=$(arm-none-eabi-nm "$OBJDIR/mode/lcdtap_backend.cpp.o")
if printf "%s\n" "$symbols" | grep -E "\b(malloc|free|calloc|realloc|_Znwj|_ZdlPv)\b"; then
    echo "FAIL: lcdtap_backend.cpp.o references dynamic heap"
    exit 1
fi
echo "[PASS] LCDTap backend is clean (zero heap)"

# 5. Check pirate/ core runtime
echo "[CHECK] pirate/ core runtime heap elimination..."
symbols=$(arm-none-eabi-nm "$OBJDIR"/pirate/*.c.o)
if printf "%s\n" "$symbols" | grep -E "\b(malloc|free|calloc|realloc)\b"; then
    echo "FAIL: pirate/ modules reference dynamic heap"
    exit 1
fi
echo "[PASS] pirate/ core runtime is clean (zero heap)"

# 6. Check kernel/ service runtime
echo "[CHECK] kernel/ service runtime heap elimination..."
symbols=$(arm-none-eabi-nm "$OBJDIR"/kernel/*.c.o)
if printf "%s\n" "$symbols" | grep -E "\b(malloc|free|calloc|realloc)\b"; then
    echo "FAIL: kernel/ modules reference dynamic heap"
    exit 1
fi
echo "[PASS] kernel/ service runtime is clean (zero heap)"

# MakeHex must not reintroduce C++ vector/string heap allocation.
echo "[CHECK] MakeHex encoder heap elimination..."
symbols=$(arm-none-eabi-nm -C "$OBJDIR/lib/makehex/MakeHex.cpp.o")
if printf "%s\n" "$symbols" | grep -E ' U (malloc|calloc|realloc|free|strdup|operator new\(unsigned|operator delete\()'; then
    echo "FAIL: MakeHex references heap allocators"
    exit 1
fi

# All FlatCC runtime backends, including aligned finalization, must use fixed pools.
echo "[CHECK] FlatCC runtime heap elimination..."
symbols=$(arm-none-eabi-nm -u "$OBJDIR/builder.c.o" "$OBJDIR/emitter.c.o" "$OBJDIR/refmap.c.o" "$OBJDIR/json_parser.c.o" "$OBJDIR/json_printer.c.o")
if printf "%s\n" "$symbols" | grep -E ' U (malloc|calloc|realloc|free|aligned_alloc|aligned_free|posix_memalign)$'; then
    echo "FAIL: FlatCC runtime references libc heap"
    exit 1
fi

# 7. Verify static RAM consumption <= 380 KB budget (389,120 bytes)
sections=$(arm-none-eabi-size -A "$ELF")
# Include vector table, alignment gaps and reserved heap; exclude scratch stacks.
RAM_SIZE=$(printf "%s\n" "$sections" | awk '$3 >= 536870912 && $3 < 537395200 {end = $3 + $2; if (end > high) high = end} END {if (high) print high - 536870912; else exit 1}')
echo "[CHECK] Total static RAM usage: $RAM_SIZE bytes (Budget: 389,120 bytes / 380 KB)..."
if [ "$RAM_SIZE" -gt 389120 ]; then
    echo "FAIL: Static RAM $RAM_SIZE bytes exceeds budget of 389,120 bytes!"
    exit 1
fi
echo "[PASS] Static RAM $RAM_SIZE bytes is within budget (headroom: $((524288 - RAM_SIZE)) bytes)"

echo "=== ALL STATIC MEMORY MODEL CHECKS PASSED ==="
