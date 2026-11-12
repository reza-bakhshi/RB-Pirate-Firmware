#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
TEST_BIN=$(mktemp /tmp/bp-profile-clock.XXXXXX)
trap 'rm -f "$TEST_BIN"' EXIT
for flags in '-O2' '-O2 -fsanitize=address,undefined -fno-omit-frame-pointer'; do
    gcc -std=c11 -Wall -Wextra -Werror $flags -Isrc tests/test_profile_clock.c -o "$TEST_BIN"
    "$TEST_BIN"
done
