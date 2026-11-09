#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

echo "=== Building and running command completion test suite ==="

CC="${CC:-gcc}"
CFLAGS="-Wall -Wextra -Wno-unused-parameter -DBP_HOST_TEST -include ${ROOT_DIR}/tests/stubs/pico/stdlib.h -include ${ROOT_DIR}/src/translation/base.h -I${ROOT_DIR}/tests/stubs -I${ROOT_DIR}/src -fsanitize=address,undefined -g -O1"

${CC} ${CFLAGS} \
    ${ROOT_DIR}/tests/test_cmd_completion.c \
    ${ROOT_DIR}/src/lib/bp_args/bp_cmd.c \
    ${ROOT_DIR}/src/lib/bp_number/bp_number.c \
    -o "${SCRIPT_DIR}/test_cmd_completion"

"${SCRIPT_DIR}/test_cmd_completion"
echo "SANITIZERS: ALL PASS"
rm -f "${SCRIPT_DIR}/test_cmd_completion"
