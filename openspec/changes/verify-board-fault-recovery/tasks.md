# Tasks: Verify board fault recovery and resource lifecycle

Status: proposed; all tasks intentionally unchecked. Pure documentation creation does not satisfy any implementation or hardware task below. Every group inherits the build/memory gates in [design.md](design.md).

## 1. Record actual board/fixture inventory and command enumeration for every listed mode and command variant

- [ ] 1.1 Record actual board/fixture inventory and command enumeration for every listed mode and command variant.
  Verification: Compare mode command tables to the acceptance matrix; mark each absent fixture BLOCKED.
- [ ] 1.2 Build the supported firmware, run the relevant focused checks and record flash/RAM delta and warnings for this group.
  Verification: `cmake --build build_rp2350 --target bus_pirate6`; `bash tests/test_memory_model.sh`; attach compiler/linker and named test logs.

## 2. Add structured HIL case results and immutable artifact manifests

- [ ] 2.1 Add structured HIL case results and immutable artifact manifests.
  Verification: Run tests/test_hil_harness.sh and tests/test_hil_smoke.sh; missing hardware must return skip rather than success.
- [ ] 2.2 Build the supported firmware, run the relevant focused checks and record flash/RAM delta and warnings for this group.
  Verification: `cmake --build build_rp2350 --target bus_pirate6`; `bash tests/test_memory_model.sh`; attach compiler/linker and named test logs.

## 3. Implement dummy-load PSU overcurrent, undervoltage and stale-telemetry procedures

- [ ] 3.1 Implement dummy-load PSU overcurrent, undervoltage and stale-telemetry procedures.
  Verification: Observe rail/current and cancellation with an independent scope and attach measured thresholds.
- [ ] 3.2 Build the supported firmware, run the relevant focused checks and record flash/RAM delta and warnings for this group.
  Verification: `cmake --build build_rp2350 --target bus_pirate6`; `bash tests/test_memory_model.sh`; attach compiler/linker and named test logs.

## 4. Implement SD hotplug/error and USB disconnect/load cases

- [ ] 4.1 Implement SD hotplug/error and USB disconnect/load cases.
  Verification: Run tests/test_hil_smoke.sh with identified media and record bounded recovery and media-generation changes.
- [ ] 4.2 Build the supported firmware, run the relevant focused checks and record flash/RAM delta and warnings for this group.
  Verification: `cmake --build build_rp2350 --target bus_pirate6`; `bash tests/test_memory_model.sh`; attach compiler/linker and named test logs.

## 5. Implement LCD completion and SWD parity/turnaround/WAIT/FAULT measurements

- [ ] 5.1 Implement LCD completion and SWD parity/turnaround/WAIT/FAULT measurements.
  Verification: Capture last SPI bit versus buffer-release notification and ACK/data-phase traces with the analyzer.
- [ ] 5.2 Build the supported firmware, run the relevant focused checks and record flash/RAM delta and warnings for this group.
  Verification: `cmake --build build_rp2350 --target bus_pirate6`; `bash tests/test_memory_model.sh`; attach compiler/linker and named test logs.

## 6. Automate 1000 mode/job transitions, partial setup rollback and repeated cancellation

- [ ] 6.1 Automate 1000 mode/job transitions, partial setup rollback and repeated cancellation.
  Verification: Compare health/res and complete GPIO/clock snapshots against the pre-test baseline after every cycle.
- [ ] 6.2 Build the supported firmware, run the relevant focused checks and record flash/RAM delta and warnings for this group.
  Verification: `cmake --build build_rp2350 --target bus_pirate6`; `bash tests/test_memory_model.sh`; attach compiler/linker and named test logs.

## 7. Exercise both stuck-core watchdog paths and warm-reset crash persistence

- [ ] 7.1 Exercise both stuck-core watchdog paths and warm-reset crash persistence.
  Verification: Run designated fault-injection build; record configured reset window, crash reason and post-boot state.
- [ ] 7.2 Build the supported firmware, run the relevant focused checks and record flash/RAM delta and warnings for this group.
  Verification: `cmake --build build_rp2350 --target bus_pirate6`; `bash tests/test_memory_model.sh`; attach compiler/linker and named test logs.

## 8. Run a minimum four-hour concurrent workload soak and publish acceptance records

- [ ] 8.1 Run a minimum four-hour concurrent workload soak and publish acceptance records.
  Verification: Extend the existing tests/test_soak_runner.sh entry point and its current backing harness; no unexplained resets/leaks or missed fault deadlines.
- [ ] 8.2 Build the supported firmware, run the relevant focused checks and record flash/RAM delta and warnings for this group.
  Verification: `cmake --build build_rp2350 --target bus_pirate6`; `bash tests/test_memory_model.sh`; attach compiler/linker and named test logs.

## Final acceptance

- [ ] Complete every row of [the acceptance checklist](../../checklists/verify-board-fault-recovery.md), with actual evidence or an explicit blocked result.
  Verification: no missing fixtures, required command rows or hardware skips may be converted into pass.
- [ ] Run normal and optimized ASan/UBSan unified suites and all supported platform builds.
  Verification: `bash tests/run_all.sh`, recorded optimized-compiler invocation, zero-warning build logs and actual platform identity.
- [ ] Measure stack high-water on both cores, memory ceiling, resource restoration and stable loaded operation.
  Verification: attach `health`/`res`, size output, scope/serial logs and failure-injection results.
- [ ] Update feature/checklist status and archive only after implementation and physical evidence meet the gates.
  Verification: review [FEATURE_ACCEPTANCE_CHECKLIST.md](../../FEATURE_ACCEPTANCE_CHECKLIST.md); unresolved gates keep this change active.
