#!/bin/sh
set -e
cd "$(dirname "$0")/.."

echo "=== Running Hardware-In-The-Loop (HIL) Smoke Test Suite ==="

python3 tests/hil_test_suite.py --auto-detect --smoke --cycles 50 --soak 2

echo "HIL SMOKE SUITE: ALL PASS"
