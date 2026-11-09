# Proposal

## Why
Malformed or oversized syntax strings, nested arguments, and edge-case inputs can cause buffer overflows, silent out-of-bounds writes, or CPU hangs on the control core. Additionally, several correctness and safety backlog items (ARCH-700 through ARCH-709) remain open across system monitoring, timer idempotency, strict-aliasing, and PIO claim assertions. Hardening the command parser and syntax compiler ensures robust operation against untrusted or malformed inputs without risking system stability or watchdog crashes.

## What Changes
- **Syntax Compiler Bounds Hardening**: Check `syntax_io.out_cnt < SYN_MAX_LENGTH` strictly before writing to `syntax_io.out[]` for all token types, including string literals and numeric inputs.
- **Repeat Syntax Validation**: Explicitly reject `:0` repeat attributes as invalid syntax (`Error: repeat count cannot be zero`).
- **Correctness Backlog Resolution (ARCH-700 to ARCH-709)**:
  - ARCH-700: In `src/system_monitor.c`, decouple `current_sense` enable flag (`bool`) from the 32-bit measurement (`uint32_t`).
  - ARCH-701 / ARCH-706: Verified bounds-checked FatFS error translation in `src/services/bp_storage.c`.
  - ARCH-702: In `src/pirate.c`, make `system_tick_enable()` and `system_tick_disable()` idempotent by guarding repeating timer creation.
  - ARCH-703: Enforce bounds checking and `:0` repeat rejection in `src/syntax_compile.c`.
  - ARCH-704: In `src/pirate/mcu.c`, eliminate strict-aliasing violation in `mcu_get_unique_id()`.
  - ARCH-705: In `src/commands/spi/sniff.c` and `src/lib/picorvd/picoswio.c`, replace `hard_assert(success)` on PIO claim with clean error returns.
  - ARCH-707: In `src/ui/ui_term_linenoise.c`, add cooperative wait/timeout and cancellation checks in prompt loops.
  - ARCH-708: Verify `-Wall -Wextra` compilation clean gates in CMake.
  - ARCH-709: Audit buffer copy calls in parser paths.
- **Unified Command Descriptors (ARCH-300 / ARCH-301)**: Command table verification ensuring all commands map through `bp_command_def_t`.
- **Parser Fuzz Testing Suite**: Implement `tests/test_parser_fuzz.c` and `tests/test_parser_fuzz.sh` testing >10,000 malformed, boundary, and randomized inputs under ASan/UBSan.

## Capabilities

### New Capabilities
- `command-parser`: Defines requirements for syntax token compilation, boundary verification, repeat limits, command lookup, argument parsing safety, and fuzz tolerance.

### Modified Capabilities
*(None)*

## Impact
- Core 0 (Control plane): `src/syntax_compile.c`, `src/system_monitor.c`, `src/pirate.c`, `src/pirate/mcu.c`, `src/ui/ui_term_linenoise.c`, `src/commands/spi/sniff.c`, `src/lib/picorvd/picoswio.c`.
- No breaking changes to user syntax or command names.
- Host test suite expands to include parser fuzzing under sanitizers.
