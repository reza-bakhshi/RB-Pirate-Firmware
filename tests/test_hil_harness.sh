#!/bin/sh
set -e
cd "$(dirname "$0")/.."
PYTHONDONTWRITEBYTECODE=1 python3 tests/test_hil_harness.py
