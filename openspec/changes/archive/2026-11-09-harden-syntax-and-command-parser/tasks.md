# Tasks

## 1. Syntax Compiler & Backlog Hardening

- [x] 1.1 Decouple `current_sense` measurement from boolean enable in `src/system_monitor.c` (ARCH-700) and verify with tests
- [x] 1.2 Make `system_tick_enable()` and `system_tick_disable()` idempotent in `src/pirate.c` (ARCH-702)
- [x] 1.3 Add strict pre-condition buffer bounds checks and reject `:0` repeats in `src/syntax_compile.c` (ARCH-703, ARCH-315)
- [x] 1.4 Remove strict-aliasing violation in `src/pirate/mcu.c` (ARCH-704)
- [x] 1.5 Replace `hard_assert(success)` on PIO claim in `src/commands/spi/sniff.c` and `src/lib/picorvd/picoswio.c` with clean error returns (ARCH-705)
- [x] 1.6 Add cooperative wait and cancellation check in `src/ui/ui_term_linenoise.c` (ARCH-707)

## 2. Parser Fuzz Suite & Verification

- [x] 2.1 Implement `tests/test_parser_fuzz.c` and `tests/test_parser_fuzz.sh` with >10,000 malformed inputs and verify zero leaks under ASan/UBSan
- [x] 2.2 Add `tests/test_parser_fuzz.sh` to `tests/run_all.sh` and verify all test suites pass cleanly
- [x] 2.3 Verify RP2350 target firmware builds cleanly without compiler warnings

## Workflow follow-up

- Archive change with `openspec archive harden-syntax-and-command-parser --yes`.
