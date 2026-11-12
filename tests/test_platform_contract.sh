#!/bin/sh
# Copyright (c) 2026 RB-Pirate contributors. SPDX-License-Identifier: MIT
set -eu
REPO_ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
TEST_DIR=$(mktemp -d /tmp/bp-platform.XXXXXX)
trap 'rm -rf "$TEST_DIR"' EXIT
cat > "$TEST_DIR/driver.cmake" <<'CMAKE'
include("${REPO_ROOT}/cmake/bp_platform.cmake")
if(NOT PICO_PLATFORM MATCHES "^(rp2350|rp2350-arm-s)$" OR NOT PICO_BOARD STREQUAL "rb-pirate")
    message(FATAL_ERROR "Unexpected effective platform/board")
endif()
CMAKE
pass() {
    env -u PICO_PLATFORM -u PICO_BOARD cmake -DREPO_ROOT="$REPO_ROOT" "$@" -P "$TEST_DIR/driver.cmake"
}
reject() {
    if env -u PICO_PLATFORM -u PICO_BOARD cmake -DREPO_ROOT="$REPO_ROOT" "$@" -P "$TEST_DIR/driver.cmake" > "$TEST_DIR/reject.log" 2>&1; then
        echo "FAIL: unsupported request accepted: $*"; exit 1
    fi
    grep -q 'Unsupported' "$TEST_DIR/reject.log"
}
pass
pass -DPICO_PLATFORM=rp2350 -DPICO_BOARD=rb-pirate
pass -DPICO_PLATFORM=rp2350-arm-s -DBP_PICO_PLATFORM=rp2350
reject -DPICO_PLATFORM=rp2040
reject -DBP_PICO_PLATFORM=rp2040
reject -DPICO_PLATFORM=rp2350 -DBP_PICO_PLATFORM=rp2040
reject -DPICO_PLATFORM=rp2350-riscv
reject -DPICO_BOARD=pico2
reject -DPICO_PLATFORM=
if env -u PICO_BOARD PICO_PLATFORM=rp2040 cmake -DREPO_ROOT="$REPO_ROOT" -P "$TEST_DIR/driver.cmake" > "$TEST_DIR/reject.log" 2>&1; then
    echo 'FAIL: unsupported environment accepted'; exit 1
fi
grep -q 'Unsupported' "$TEST_DIR/reject.log"
# Integration: top-level rejection must happen before any SDK download or compiler setup.
if env -u PICO_PLATFORM -u PICO_BOARD cmake -S "$REPO_ROOT" -B "$TEST_DIR/top-level" -DPICO_PLATFORM=rp2040 -DPICO_SDK_PATH=/nonexistent-sdk > "$TEST_DIR/reject.log" 2>&1; then
    echo 'FAIL: top-level configure accepted RP2040'; exit 1
fi
grep -q 'Unsupported PICO_PLATFORM' "$TEST_DIR/reject.log"

# Identity and ELF verification checks
if [ -f "$REPO_ROOT/build_rp2350/src/bus_pirate6.elf" ]; then
    python3 "$REPO_ROOT/tools/platform_identity.py" "$REPO_ROOT/build_rp2350" --verify -o "$TEST_DIR/identity.json" > /dev/null
    grep -q '"detected_silicon": "RP2350"' "$TEST_DIR/identity.json"
    grep -q '"supported_contract_passed": true' "$TEST_DIR/identity.json"
fi

if [ -f "$REPO_ROOT/build_rp2040/src/bus_pirate6.elf" ]; then
    python3 "$REPO_ROOT/tools/platform_identity.py" "$REPO_ROOT/build_rp2040" --assert-misleading-dir -o "$TEST_DIR/identity_rp2040.json" > /dev/null
    grep -q '"is_misleading": true' "$TEST_DIR/identity_rp2040.json"
    grep -q '"detected_silicon": "RP2350"' "$TEST_DIR/identity_rp2040.json"
fi

echo 'Platform contract: defaults, supported aliases, conflicts, environment, early rejection and build identity PASS'
