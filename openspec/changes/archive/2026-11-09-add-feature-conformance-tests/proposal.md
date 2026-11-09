# Proposal: add-feature-conformance-tests

## Why

Across Phase F (F1 through F9), all ported subsystems (INFRARED, SUBGHZ, RFID, RF24, LCDTAP, CELL, PS/2, and USB) were audited, hardened against resource leaks and hangs, and equipped with dedicated host test suites. Without a unified test runner and in-firmware diagnostic introspection, ported features risk regressing silently during future kernel or command changes.

This change establishes an automated regression guard by providing `tests/run_all.sh` (which executes all 25 host test suites under AddressSanitizer and UBSan) and implementing the `features` global shell command to report verified capabilities, status, and test coverage on live hardware.

## What Changes

- **Unified Conformance Test Runner**: Implement `tests/run_all.sh` to run every unit test suite in `tests/` under ASan and UBSan, reporting summary metrics and failing immediately on any failure.
- **Global `features` Shell Command**: Implement `src/commands/global/cmd_features.c` and `src/commands/global/cmd_features.h`, registered in `src/commands.c`, providing the `features` command. Supports listing all features with their origin, status, capability, and test suite references matching `openspec/FEATURES.md`, with optional `--verbose` or per-feature queries.
- **Translation / Help Registration**: Add help and usage definitions for `cmd_features`.
- **Feature Matrix and Roadmap Updates**: Update `openspec/FEATURES.md` and `openspec/ROADMAP.md` marking Phase F complete.

## Capabilities

### New Capabilities

None. `skip_specs: true` is set in `.openspec.yaml`.

### Modified Capabilities

None.

## Impact

- `tests/run_all.sh`: New unified host test runner script.
- `src/commands/global/cmd_features.c`, `src/commands/global/cmd_features.h`: Implementation of the `features` diagnostic command.
- `src/commands.c`: Registration of `cmd_features`.
- `openspec/FEATURES.md`, `openspec/ROADMAP.md`: Status updates for Phase F completion.
