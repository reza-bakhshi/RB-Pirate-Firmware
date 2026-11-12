#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

echo "=== Building and running CAN and Fault Injection test suite ==="

CC="${CC:-gcc}"
CFLAGS="-Wall -Wextra -Wno-unused-parameter -DBP_HOST_TEST -include ${ROOT_DIR}/tests/stubs/pico/stdlib.h -include ${ROOT_DIR}/src/translation/base.h -I${ROOT_DIR}/tests/stubs -I${ROOT_DIR}/src -fsanitize=address,undefined -g -O1"

${CC} ${CFLAGS} \
    ${ROOT_DIR}/tests/test_can_glitch.c \
    ${ROOT_DIR}/src/kernel/bp_resource.c \
    ${ROOT_DIR}/src/pirate/hwcan_pio.c \
    ${ROOT_DIR}/src/mode/can.c \
    ${ROOT_DIR}/src/commands/global/cmd_glitch.c \
    ${ROOT_DIR}/src/lib/bp_args/bp_cmd.c \
    ${ROOT_DIR}/src/lib/bp_number/bp_number.c \
    -o "${SCRIPT_DIR}/test_can_glitch"

"${SCRIPT_DIR}/test_can_glitch"
echo "SANITIZERS: ALL PASS"
rm -f "${SCRIPT_DIR}/test_can_glitch"
