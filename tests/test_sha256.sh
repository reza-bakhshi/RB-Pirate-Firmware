#!/bin/sh
# Copyright (c) 2026 RB-Pirate contributors. SPDX-License-Identifier: MIT
set -eu
cd "$(dirname "$0")/.."
TEST_DIR=$(mktemp -d /tmp/bp-sha256.XXXXXX)
trap 'rm -rf "$TEST_DIR"' EXIT
# Exercise the exact host fallback implementation against Python's independent
# SHA-256 oracle, without replacing any hash functions with test doubles.
python3 - "$TEST_DIR" <<'PY'
import hashlib
import pathlib
import sys
source = pathlib.Path('src/commands/global/cmd_sha256.c').read_text()
start = source.index('typedef struct {')
end = source.index('\n#endif', start)
code = '#include <stdint.h>\n#include <stddef.h>\n#include <assert.h>\n#include <string.h>\n' + source[start:end]
code += '\nint main(void) {\nuint8_t input[4096], hash[32];\n'
code += 'for (unsigned i = 0; i < sizeof(input); ++i) input[i] = (uint8_t)i;\n'
for n in (0, 1, 55, 56, 63, 64, 65, 119, 127, 128, 511, 512, 513, 4096):
    digest = hashlib.sha256(bytes(i % 256 for i in range(n))).digest()
    expected = ','.join(str(x) for x in digest)
    code += '{\nconst uint8_t expected[32] = {' + expected + '};\n'
    code += 'for (unsigned chunk = 1; chunk <= 512; chunk *= 2) {\n'
    code += 'host_sha256_ctx_t ctx; host_sha256_init(&ctx);\n'
    if n:
        code += f'for (unsigned off = 0; off < {n};) {{ unsigned len = {n} - off; if (len > chunk) len = chunk; host_sha256_update(&ctx, input + off, len); off += len; }}\n'
    code += 'host_sha256_final(&ctx, hash); assert(memcmp(hash, expected, 32) == 0);\n}\n}\n'
code += 'return 0;\n}\n'
pathlib.Path(sys.argv[1], 'sha256.c').write_text(code)
PY
for flags in '-O2' '-O2 -fsanitize=address,undefined -fno-omit-frame-pointer'; do
    gcc -std=c11 -Wall -Wextra $flags "$TEST_DIR/sha256.c" -o "$TEST_DIR/test_sha256"
    "$TEST_DIR/test_sha256"
done
