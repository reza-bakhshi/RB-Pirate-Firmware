#!/bin/sh
set -e
cd "$(dirname "$0")/.."

SOAK_DURATION="${SOAK_SECONDS:-5}"

echo "=== Running Hardware-In-The-Loop (HIL) Soak Runner (${SOAK_DURATION}s) ==="

python3 tests/hil_test_suite.py --auto-detect --smoke --cycles 100 --soak "${SOAK_DURATION}"

echo "HIL SOAK RUNNER: ALL PASS"
