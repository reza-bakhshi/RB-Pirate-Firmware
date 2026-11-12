# Tasks: Define and enforce the supported platform contract

Status: in progress. Task groups 1, 2, 3, 4, and 6 have host implementation/build and physical hardware validation evidence in [evidence.md](evidence.md). Task group 5 is explicitly NOT APPLICABLE under the current RP2350-only product decision. Every group inherits the build/memory gates in [design.md](design.md).

## 1. Record RP2350-only versus separately approved RP2040-port decision and rationale

- [x] 1.1 Record RP2350-only versus separately approved RP2040-port decision and rationale.
  Verification: Review config.yaml, board headers and CMake; explicit unresolved future-port scope must not count as implemented support.
- [x] 1.2 Build the supported firmware, run the relevant focused checks and record flash/RAM delta and warnings for this group.
  Verification: `cmake --build build_rp2350 --target bus_pirate6`; `bash tests/test_memory_model.sh`; attach compiler/linker and named test logs.

## 2. Implement non-silent platform/board validation and supported presets

- [x] 2.1 Implement non-silent platform/board validation and supported presets.
  Verification: Add configure tests for supported RP2350, explicit RP2040 rejection and incompatible board combinations.
- [x] 2.2 Build the supported firmware, run the relevant focused checks and record flash/RAM delta and warnings for this group.
  Verification: `cmake --build build_rp2350 --target bus_pirate6`; `bash tests/test_memory_model.sh`; attach compiler/linker and named test logs.

## 3. Emit and verify build identity from actual definitions, ELF and memory regions

- [x] 3.1 Emit and verify build identity from actual definitions, ELF and memory regions.
  Verification: Inspect CMakeCache.txt, compile_commands.json if enabled and arm-none-eabi-readelf -h/-A/-S output, never directory name alone.
- [x] 3.2 Build the supported firmware, run the relevant focused checks and record flash/RAM delta and warnings for this group.
  Verification: `cmake --build build_rp2350 --target bus_pirate6`; `bash tests/test_memory_model.sh`; attach compiler/linker and named test logs.

## 4. Align user docs, CI and artifacts with the chosen contract

- [x] 4.1 Align user docs, CI and artifacts with the chosen contract.
  Verification: Search for RP2040/BP5 support claims and require each to reference its supported or unsupported status.
- [x] 4.2 Build the supported firmware, run the relevant focused checks and record flash/RAM delta and warnings for this group.
  Verification: `cmake --build build_rp2350 --target bus_pirate6`; `bash tests/test_memory_model.sh`; attach compiler/linker and named test logs.

## 5. For an approved port only, create a separate change covering capabilities, memory and board boot

NOT APPLICABLE under the existing RP2350-only decision; this is not an implemented or approved RP2040 port.

- [ ] 5.1 For an approved port only, create a separate change covering capabilities, memory and board boot.
  Verification: Do not check this conditional task complete unless an explicit port decision and its separate acceptance record exist; otherwise record NOT APPLICABLE with the RP2350-only decision.
- [ ] 5.2 Build the supported firmware, run the relevant focused checks and record flash/RAM delta and warnings for this group.
  Verification: `cmake --build build_rp2350 --target bus_pirate6`; `bash tests/test_memory_model.sh`; attach compiler/linker and named test logs.

## 6. Validate representative supported hardware and update inventory with evidence

- [x] 6.1 Validate representative supported hardware and update inventory with evidence.
  Verification: Build supported target, run tests/run_all.sh and tests/test_memory_model.sh, and attach a physical boot/platform-info log.
- [x] 6.2 Build the supported firmware, run the relevant focused checks and record flash/RAM delta and warnings for this group.
  Verification: `cmake --build build_rp2350 --target bus_pirate6`; `bash tests/test_memory_model.sh`; attach compiler/linker and named test logs.

## Final acceptance

- [ ] Complete every row of [the acceptance checklist](../../checklists/define-supported-platform-contract.md), with actual evidence or an explicit blocked result.
  Verification: no missing fixtures, required command rows or hardware skips may be converted into pass.
- [ ] Run normal and optimized ASan/UBSan unified suites and all supported platform builds.
  Verification: `bash tests/run_all.sh`, recorded optimized-compiler invocation, zero-warning build logs and actual platform identity.
- [ ] Measure stack high-water on both cores, memory ceiling, resource restoration and stable loaded operation.
  Verification: attach `health`/`res`, size output, scope/serial logs and failure-injection results.
- [ ] Update feature/checklist status and archive only after implementation and physical evidence meet the gates.
  Verification: review [FEATURE_ACCEPTANCE_CHECKLIST.md](../../FEATURE_ACCEPTANCE_CHECKLIST.md); unresolved gates keep this change active.
