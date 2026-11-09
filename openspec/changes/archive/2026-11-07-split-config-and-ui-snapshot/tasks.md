# Tasks: Split Config, Runtime, and UI Snapshot

## 1. Type Definitions & Seqlock Implementation
- [x] 1.1 Create `src/kernel/bp_config.h` defining `bp_config_t`, `bp_runtime_t`, `bp_ui_snapshot_t`, and seqlock prototypes. <!-- id: 1.1 -->
- [x] 1.2 Implement `src/kernel/bp_config.c` with default initialization, validation, version migration, and seqlock publish/read. <!-- id: 1.2 -->

## 2. Kernel & System Integration
- [x] 2.1 Integrate `bp_config` with `src/system_config.h` and storage config persistence. <!-- id: 2.1 -->
- [x] 2.2 Hook `bp_ui_snapshot_publish()` into Core 0 service loop in `src/kernel/bp_kernel.c`. <!-- id: 2.2 -->
- [x] 2.3 Update `src/CMakeLists.txt` to include `bp_config.c`. <!-- id: 2.3 -->

## 3. Verification & Testing
- [x] 3.1 Create host unit test suite `tests/test_config.c` and runner `tests/test_config.sh` with AddressSanitizer and UBSan. <!-- id: 3.1 -->
- [x] 3.2 Run all 16 test suites in `tests/*.sh` and confirm zero regressions. <!-- id: 3.2 -->
- [x] 3.3 Rebuild RP2350 target firmware `bus_pirate6.uf2` and verify RAM ($\le 380\text{ KB}$) and flash budgets. <!-- id: 3.3 -->
