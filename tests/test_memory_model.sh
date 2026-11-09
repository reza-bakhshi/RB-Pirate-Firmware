#!/bin/sh
set -e
cd "$(dirname "$0")/.."

ELF="build_rp2350/src/bus_pirate6.elf"
OBJDIR="build_rp2350/src/CMakeFiles/bus_pirate6.dir"

if [ ! -f "$ELF" ]; then
    echo "ERROR: $ELF not found. Build target bus_pirate6 first."
    exit 1
fi

echo "=== Static Memory Model Guard Test ==="

# 1. Check FatFS heap elimination
echo "[CHECK] FatFS ffsystem.c.o heap elimination..."
if arm-none-eabi-nm "$OBJDIR/fatfs/ffsystem.c.o" | grep -E "\b(malloc|free|ff_memalloc)\b"; then
    echo "FAIL: ffsystem.c.o references heap allocators"
    exit 1
fi
echo "[PASS] FatFS is clean (zero heap)"

# 2. Check SubGHz protocol pool
echo "[CHECK] SubGHz protocols.c.o heap elimination..."
if arm-none-eabi-nm "$OBJDIR/mode/subghz_protocols.c.o" | grep -E "\b(malloc|free|calloc|realloc)\b"; then
    echo "FAIL: subghz_protocols.c.o references heap allocators"
    exit 1
fi
echo "[PASS] SubGHz protocols is clean (zero heap)"

# 3. Check FlatCC BPIO packet emission
echo "[CHECK] BPIO bpio.c.o heap elimination..."
if arm-none-eabi-nm "$OBJDIR/binmode/bpio.c.o" | grep -E "\b(malloc|free|calloc|realloc)\b"; then
    echo "FAIL: bpio.c.o references heap allocators"
    exit 1
fi
echo "[PASS] BPIO is clean (zero heap)"

# 4. Check LCDTap backend
echo "[CHECK] LCDTap lcdtap_backend.cpp.o heap elimination..."
if arm-none-eabi-nm "$OBJDIR/mode/lcdtap_backend.cpp.o" | grep -E "\b(malloc|free|calloc|realloc|_Znwj|_ZdlPv)\b"; then
    echo "FAIL: lcdtap_backend.cpp.o references dynamic heap"
    exit 1
fi
echo "[PASS] LCDTap backend is clean (zero heap)"

# 5. Check pirate/ core runtime
echo "[CHECK] pirate/ core runtime heap elimination..."
if arm-none-eabi-nm "$OBJDIR"/pirate/*.c.o | grep -E "\b(malloc|free|calloc|realloc)\b"; then
    echo "FAIL: pirate/ modules reference dynamic heap"
    exit 1
fi
echo "[PASS] pirate/ core runtime is clean (zero heap)"

# 6. Check kernel/ service runtime
echo "[CHECK] kernel/ service runtime heap elimination..."
if arm-none-eabi-nm "$OBJDIR"/kernel/*.c.o | grep -E "\b(malloc|free|calloc|realloc)\b"; then
    echo "FAIL: kernel/ modules reference dynamic heap"
    exit 1
fi
echo "[PASS] kernel/ service runtime is clean (zero heap)"

# 7. Verify static RAM consumption <= 380 KB budget (389,120 bytes)
RAM_SIZE=$(arm-none-eabi-size -A "$ELF" | awk '$1 ~ /^\.(data|bss|uninitialized_data)$/ {sum += $2} END {print sum}')
echo "[CHECK] Total static RAM usage: $RAM_SIZE bytes (Budget: 389,120 bytes / 380 KB)..."
if [ "$RAM_SIZE" -gt 389120 ]; then
    echo "FAIL: Static RAM $RAM_SIZE bytes exceeds budget of 389,120 bytes!"
    exit 1
fi
echo "[PASS] Static RAM $RAM_SIZE bytes is within budget (headroom: $((524288 - RAM_SIZE)) bytes)"

echo "=== ALL STATIC MEMORY MODEL CHECKS PASSED ==="
