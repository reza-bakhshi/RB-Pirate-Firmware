# Tasks: Qualify DVI and dedicated crowbar board capabilities

Status: proposed; all tasks intentionally unchecked. Pure documentation creation does not satisfy any implementation or hardware task below. Every group inherits the build/memory gates in [design.md](design.md).

## 1. Obtain actual board revision, DVI/crowbar adapter schematics and wiring evidence

- [ ] 1.1 Obtain actual board revision, DVI/crowbar adapter schematics and wiring evidence; inventory missing fixtures.
  Verification: Review every net against src/platform/rb-pirate.h; record UNSUPPORTED/BLOCKED until qualified.
- [ ] 1.2 Build the supported firmware, run the relevant focused checks and record flash/RAM delta and warnings for this group.
  Verification: `cmake --build build_rp2350 --target bus_pirate6`; `bash tests/test_memory_model.sh`; attach compiler/linker and named test logs.

## 2. Create the GPIO/function/electrical conflict matrix and capability manifest

- [ ] 2.1 Create the GPIO/function/electrical conflict matrix and capability manifest.
  Verification: Host tests must refuse unknown boards and deliberate PSU/debug/storage conflicts without hardware writes.
- [ ] 2.2 Build the supported firmware, run the relevant focused checks and record flash/RAM delta and warnings for this group.
  Verification: `cmake --build build_rp2350 --target bus_pirate6`; `bash tests/test_memory_model.sh`; attach compiler/linker and named test logs.

## 3. Make a DVI feasibility decision with clock derivation, TMDS/blanking schedule and byte budget

- [ ] 3.1 Make a DVI feasibility decision with clock derivation, TMDS/blanking schedule and byte budget.
  Verification: Review 800x525 timing calculations, actual divider error, DMA rearm design and tests/test_memory_model.sh budget.
- [ ] 3.2 Build the supported firmware, run the relevant focused checks and record flash/RAM delta and warnings for this group.
  Verification: `cmake --build build_rp2350 --target bus_pirate6`; `bash tests/test_memory_model.sh`; attach compiler/linker and named test logs.

## 4. For qualified DVI wiring only, implement physical timing and transfer ownership in a separate tested backend

- [ ] 4.1 For qualified DVI wiring only, implement physical timing and transfer ownership in a separate tested backend.
  Verification: Run host row/count/underflow tests and capture analyzer/monitor evidence under heavy concurrent load; otherwise retain unsupported status.
- [ ] 4.2 Build the supported firmware, run the relevant focused checks and record flash/RAM delta and warnings for this group.
  Verification: `cmake --build build_rp2350 --target bus_pirate6`; `bash tests/test_memory_model.sh`; attach compiler/linker and named test logs.

## 5. Make a crowbar feasibility decision with independent pulse bound and dummy-load envelope

- [ ] 5.1 Make a crowbar feasibility decision with independent pulse bound and dummy-load envelope.
  Verification: Attach circuit review and stalled-control scope captures; PSU override must not be used as the output.
- [ ] 5.2 Build the supported firmware, run the relevant focused checks and record flash/RAM delta and warnings for this group.
  Verification: `cmake --build build_rp2350 --target bus_pirate6`; `bash tests/test_memory_model.sh`; attach compiler/linker and named test logs.

## 6. For qualified crowbar circuitry only, implement dedicated pulse generation and safe rollback

- [ ] 6.1 For qualified crowbar circuitry only, implement dedicated pulse generation and safe rollback.
  Verification: Measure width, polarity, edge latency and forced-stall release; invalid/cancel/setup-failure tests leave output inactive.
- [ ] 6.2 Build the supported firmware, run the relevant focused checks and record flash/RAM delta and warnings for this group.
  Verification: `cmake --build build_rp2350 --target bus_pirate6`; `bash tests/test_memory_model.sh`; attach compiler/linker and named test logs.

## 7. Run repeated enable/disable and fault cycles and publish only measured claims

- [ ] 7.1 Run repeated enable/disable and fault cycles and publish only measured claims.
  Verification: Compare health/res/pin/clock baselines, complete hardware checklist and keep unqualified features disabled.
- [ ] 7.2 Build the supported firmware, run the relevant focused checks and record flash/RAM delta and warnings for this group.
  Verification: `cmake --build build_rp2350 --target bus_pirate6`; `bash tests/test_memory_model.sh`; attach compiler/linker and named test logs.

## Final acceptance

- [ ] Complete every row of [the acceptance checklist](../../checklists/qualify-dvi-and-crowbar-board-capabilities.md), with actual evidence or an explicit blocked result.
  Verification: no missing fixtures, required command rows or hardware skips may be converted into pass.
- [ ] Run normal and optimized ASan/UBSan unified suites and all supported platform builds.
  Verification: `bash tests/run_all.sh`, recorded optimized-compiler invocation, zero-warning build logs and actual platform identity.
- [ ] Measure stack high-water on both cores, memory ceiling, resource restoration and stable loaded operation.
  Verification: attach `health`/`res`, size output, scope/serial logs and failure-injection results.
- [ ] Update feature/checklist status and archive only after implementation and physical evidence meet the gates.
  Verification: review [FEATURE_ACCEPTANCE_CHECKLIST.md](../../FEATURE_ACCEPTANCE_CHECKLIST.md); unresolved gates keep this change active.
