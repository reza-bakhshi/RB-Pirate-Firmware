# Tasks: add-feature-conformance-tests

## 1. Unified Test Runner

- [x] 1.1 Create `tests/run_all.sh` covering all test suites in `tests/test_*.sh`, executing under ASan/UBSan, reporting progress, and enforcing CI exit code propagation.
- [x] 1.2 Verify `tests/run_all.sh` passes all 25 test suites cleanly.

## 2. Global Features Command

- [x] 2.1 Implement `src/commands/global/cmd_features.c` and `src/commands/global/cmd_features.h` with `features` command definition and table reporting.
- [x] 2.2 Register `cmd_features` in `src/commands.c` under `CMD_CAT_SYSTEM`.
- [x] 2.3 Add `T_CMDLN_FEATURES` in `src/translation/en-us.h` or appropriate translation definition.

## 3. Documentation & Roadmap Finalization

- [x] 3.1 Update `openspec/FEATURES.md` and `openspec/ROADMAP.md` reflecting completion of Phase F (F1 through F10).
- [x] 3.2 Build `bus_pirate6` firmware for RP2350, ensuring static RAM remains within the 380 KB limit.
