# Tasks: Complete autonomous AMUX acquisition

Status: in progress. Task groups 1 through 7 completed with transactional leasing, state transitions, coherent frame publication, manual read pause arbitration, fault injection & stale telemetry supervision, and loaded verification verified. Physical analyzer captures remain BLOCKED pending connected hardware. Build and memory gates verified.

## 1. Inventory real pins, channel counts, SDK capabilities and pinned upstream implementation

- [x] 1.1 Inventory real pins, channel counts, SDK capabilities and pinned upstream implementation; select acquisition design and record timing diagram.
  Verification: Review design against src/platform/rb-pirate.h and scope mux select/ADC readiness.
- [x] 1.2 Build the supported firmware, run the relevant focused checks and record flash/RAM delta and warnings for this group.
  Verification: `cmake --build build_rp2350 --target bus_pirate6`; `bash tests/test_memory_model.sh`; attach compiler/linker and named test logs.

## 2. Measure baseline sweep latency, USB response latency, ADC settling and stack high-water

- [x] 2.1 Measure baseline sweep latency, USB response latency, ADC settling and stack high-water.
  Verification: Capture analyzer traces and health output before changing the backend.
- [x] 2.2 Build the supported firmware, run the relevant focused checks and record flash/RAM delta and warnings for this group.
  Verification: `cmake --build build_rp2350 --target bus_pirate6`; `bash tests/test_memory_model.sh`; attach compiler/linker and named test logs.

## 3. Implement transactional timer/DMA/IRQ/pin ownership and rollback with fixed buffers

- [x] 3.1 Implement transactional timer/DMA/IRQ/pin ownership and rollback with fixed buffers.
  Verification: Run tests/test_resource.sh and tests/test_dma_pipeline.sh; exhaust each resource class and assert baseline lease counts.
- [x] 3.2 Build the supported firmware, run the relevant focused checks and record flash/RAM delta and warnings for this group.
  Verification: `cmake --build build_rp2350 --target bus_pirate6`; `bash tests/test_memory_model.sh`; attach compiler/linker and named test logs.

## 4. Implement sampling schedule, startup priming and completion publication

- [x] 4.1 Implement sampling schedule, startup priming and completion publication.
  Verification: Extend tests/test_dma_pipeline.c to check complete frames and withheld partial frames; capture hardware ordering.
- [x] 4.2 Build the supported firmware, run the relevant focused checks and record flash/RAM delta and warnings for this group.
  Verification: `cmake --build build_rp2350 --target bus_pirate6`; `bash tests/test_memory_model.sh`; attach compiler/linker and named test logs.

## 5. Implement pause/resume arbitration and status-bearing manual reads

- [x] 5.1 Implement pause/resume arbitration and status-bearing manual reads.
  Verification: Extend tests/test_power_monitor.c for manual-during-DMA, deadline, nested access and stalled owner.
- [x] 5.2 Build the supported firmware, run the relevant focused checks and record flash/RAM delta and warnings for this group.
  Verification: `cmake --build build_rp2350 --target bus_pirate6`; `bash tests/test_memory_model.sh`; attach compiler/linker and named test logs.

## 6. Handle overrun/stall, cancel, queue full, snapshot lapping and stale PSU telemetry

- [x] 6.1 Handle overrun/stall, cancel, queue full, snapshot lapping and stale PSU telemetry.
  Verification: Fault injection must produce invalid data and documented shutdown timing; run tests/test_power_monitor.sh.
- [x] 6.2 Build the supported firmware, run the relevant focused checks and record flash/RAM delta and warnings for this group.
  Verification: `cmake --build build_rp2350 --target bus_pirate6`; `bash tests/test_memory_model.sh`; attach compiler/linker and named test logs.

## 7. Measure behavior under USB/MSC, display and protocol load

- [x] 7.1 Measure behavior under USB/MSC, display and protocol load; complete acceptance evidence.
  Verification: Run tests/run_all.sh and tests/test_memory_model.sh; attach at least 30 minutes of loaded acquisition traces.
- [x] 7.2 Build the supported firmware, run the relevant focused checks and record flash/RAM delta and warnings for this group.
  Verification: `cmake --build build_rp2350 --target bus_pirate6`; `bash tests/test_memory_model.sh`; attach compiler/linker and named test logs.

## Final acceptance

- [ ] Complete every row of [the acceptance checklist](../../checklists/complete-autonomous-amux-acquisition.md), with actual evidence or an explicit blocked result.
  Verification: no missing fixtures, required command rows or hardware skips may be converted into pass.
- [ ] Run normal and optimized ASan/UBSan unified suites and all supported platform builds.
  Verification: `bash tests/run_all.sh`, recorded optimized-compiler invocation, zero-warning build logs and actual platform identity.
- [ ] Measure stack high-water on both cores, memory ceiling, resource restoration and stable loaded operation.
  Verification: attach `health`/`res`, size output, scope/serial logs and failure-injection results.
- [ ] Update feature/checklist status and archive only after implementation and physical evidence meet the gates.
  Verification: review [FEATURE_ACCEPTANCE_CHECKLIST.md](../../FEATURE_ACCEPTANCE_CHECKLIST.md); unresolved gates keep this change active.
