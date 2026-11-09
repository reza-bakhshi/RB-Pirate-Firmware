#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

OUT_BIN="$SCRIPT_DIR/test_crash_record"

gcc -Wall -Wextra -Werror -fsanitize=address,undefined -g \
    -I"$REPO_ROOT/src" \
    "$REPO_ROOT/src/pirate/crash_record.c" \
    "$SCRIPT_DIR/test_crash_record.c" \
    -o "$OUT_BIN"

"$OUT_BIN"
rm -f "$OUT_BIN"
