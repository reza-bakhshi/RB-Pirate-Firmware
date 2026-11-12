# Validation results

| Check | Result |
|---|---|
| Baseline unified suite | 34 passed, 2 physical HIL skips, 0 failed (36 total) |
| Final unified suite | 35 passed, 2 physical HIL skips, 0 failed (37 total) |
| Unified suite with GCC/G++ forced to `-O2 -fsanitize=address,undefined` | 35 passed, 2 physical HIL skips, 0 failed |
| Focused PSU tests after persistence-error additions | Passed normal and ASan/UBSan executions |
| New profile clock regression | Passed optimized and optimized ASan/UBSan; twelve simulated hours, 1,800 mAh at 150 mA |
| `build_rp2350` incremental firmware build | Passed; no warning/error diagnostics in captured log |
| `build_rp2040` incremental firmware build | Passed; no warning/error diagnostics; cache actually targets RP2350 |
| RP2350 memory guard | Passed; 388,504 bytes main RAM, 135,784 bytes headroom, 616 bytes below ceiling |
| Other build directory memory guard | Passed; 388,524 bytes main RAM, 135,764 bytes headroom, 596 bytes below ceiling |
| Whitespace with existing CRLF convention | `git -c core.whitespace=cr-at-eol diff --check` passed |
| Hardware, actual RP2040, clean full rebuild | Not verified |

No device flashing was performed. Neither sanitizer run validates ARM interrupt ordering, electrical safety, DMA bus behavior or real USB enumeration. The memory guard checks its existing object scope and does not prove complete absence of heap calls throughout all library paths.

Captured local logs: `/tmp/bp-audit-baseline.log`, `/tmp/bp-audit-final-tests.log`, `/tmp/bp-audit-o2-sanitized.log`, `/tmp/bp-audit-final-2350.log`, `/tmp/bp-audit-final-2040.log`, `/tmp/bp-audit-memory.log`.
