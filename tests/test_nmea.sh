#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

echo "=== Building test_nmea ==="
gcc -Wall -Wextra -Werror -fsanitize=address,undefined -g \
    -I"${REPO_ROOT}" \
    -I"${REPO_ROOT}/src" \
    -I"${REPO_ROOT}/src/lib/minmea" \
    -I"${REPO_ROOT}/tests/stubs" \
    "${REPO_ROOT}/tests/test_nmea.c" \
    "${REPO_ROOT}/src/lib/minmea/minmea.c" \
    "${REPO_ROOT}/src/lib/minmea/gps.c" \
    -o "${SCRIPT_DIR}/test_nmea"

echo "=== Running test_nmea ==="
"${SCRIPT_DIR}/test_nmea"
rm -f "${SCRIPT_DIR}/test_nmea"
echo "=== test_nmea completed successfully ==="
