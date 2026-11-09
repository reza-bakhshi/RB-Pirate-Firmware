# Tasks: Split Control and Protocol Cores

## 1. Mode Lifecycle & Compatibility Adapter
- [x] 1.1 Create `src/mode/bp_mode.h` with `bp_mode_t` definition, lifecycle methods, and status codes. <!-- id: 1.1 -->
- [x] 1.2 Implement `src/mode/bp_mode.c` with mode registry, compatibility adapter for `modes[MAXPROTO]`, and `bp_mode_switch()`. <!-- id: 1.2 -->
- [x] 1.3 Implement transactional rollback to HiZ on setup failure with resource lease reclamation. <!-- id: 1.3 -->

## 2. Global Cleanup-Before-Setup Ordering
- [x] 2.1 Update `src/ui/ui_mode.c` to use `bp_mode_switch()`. <!-- id: 2.1 -->
- [x] 2.2 Fix mode transition sequence in `src/binmode/binmodes.c` to enforce cleanup before setup. <!-- id: 2.2 -->
- [x] 2.3 Fix mode transition sequence in `src/binmode/bpio.c` and `src/binmode/binio_helpers.c`. <!-- id: 2.3 -->

## 3. Protocol Data Plane & Heartbeat
- [x] 3.1 Create `src/kernel/bp_protocol.h` and `src/kernel/bp_protocol.c` tracking protocol engine state and heartbeats. <!-- id: 3.1 -->
- [x] 3.2 Add protocol stall detection and recovery handler. <!-- id: 3.2 -->
- [x] 3.3 Hook protocol service and heartbeat into Core 1 kernel tick (`bp_service(1)`). <!-- id: 3.3 -->

## 4. Verification & Testing
- [x] 4.1 Create host unit test suite `tests/test_mode.c` testing mode registration, switching, rollback, and 1000-iteration soak test. <!-- id: 4.1 -->
- [x] 4.2 Create test runner `tests/test_mode.sh` with AddressSanitizer and UndefinedBehaviorSanitizer. <!-- id: 4.2 -->
- [x] 4.3 Run all test suites in `tests/*.sh` and confirm zero regressions. <!-- id: 4.3 -->
- [x] 4.4 Build RP2350 target `bus_pirate6.uf2` and verify flash/RAM budgets. <!-- id: 4.4 -->
