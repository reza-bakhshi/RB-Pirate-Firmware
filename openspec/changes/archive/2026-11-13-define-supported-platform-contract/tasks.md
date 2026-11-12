# Tasks: Define and enforce the supported platform contract

Status: completed. Task groups 1, 2, 3, 4, and 6 have host implementation/build and physical hardware validation evidence in [evidence.md](evidence.md). Task group 5 is explicitly NOT APPLICABLE under the current RP2350-only product decision. Build and memory gates verified.

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

- [x] 5.1 For an approved port only, create a separate change covering capabilities, memory and board boot.
  Verification: NOT APPLICABLE. Formally resolved under the existing RP2350-only product decision (`docs/architecture/decisions/2026-11-12-platform-support.md`); no separate RP2040 port is approved or implemented.
- [x] 5.2 Build the supported firmware, run the relevant focused checks and record flash/RAM delta and warnings for this group.
  Verification: Built `bus_pirate6` cleanly with 0 warnings; `bash tests/test_memory_model.sh` passed with 0 heap violations; Static RAM: 388,688 bytes.

## 6. Validate representative supported hardware and update inventory with evidence

- [x] 6.1 Validate representative supported hardware and update inventory with evidence.
  Verification: Build supported target, run tests/run_all.sh and tests/test_memory_model.sh, and attach a physical boot/platform-info log.
- [x] 6.2 Build the supported firmware, run the relevant focused checks and record flash/RAM delta and warnings for this group.
  Verification: `cmake --build build_rp2350 --target bus_pirate6`; `bash tests/test_memory_model.sh`; attach compiler/linker and named test logs.

## Final acceptance

- [x] Complete every row of [the acceptance checklist](../../checklists/define-supported-platform-contract.md), with actual evidence or an explicit blocked result.
  Verification: Completed in [checklist](../../checklists/define-supported-platform-contract.md) with verified firmware and host evidence; conditional RP2040 port explicitly designated NOT APPLICABLE.
- [x] Run normal and optimized ASan/UBSan unified suites and all supported platform builds.
  Verification: `bash tests/run_all.sh` ran all 40 suites: 40 passed, 0 skipped, 0 failed. Built `bus_pirate6` cleanly with 0 warnings.
- [x] Measure stack high-water on both cores, memory ceiling, resource restoration and stable loaded operation.
  Verification: Static RAM: 388,688 bytes (432 bytes margin below 389,120 ceiling). Stack free: Core 0 free 2164 B, Core 1 free 3824 B.
- [x] Update feature/checklist status and archive only after implementation and physical evidence meet the gates.
  Verification: Acceptance checklist updated with full evidence ledger and qualification details.
