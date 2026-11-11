#!/bin/sh
set -e
cd "$(dirname "$0")/.."
for flags in '-O2' '-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer'; do
    gcc $flags -std=c11 -DBP_FLATCC_STATIC_TEST -DBP_EMBEDDED -DBP_HOST_TEST -Itests/stubs -Isrc -Isrc/flatcc \
        tests/test_flatcc_memory.c src/services/bp_flatcc_memory.c \
        src/builder.c src/emitter.c src/refmap.c src/verifier.c -o /tmp/test_flatcc_memory
    /tmp/test_flatcc_memory
done
