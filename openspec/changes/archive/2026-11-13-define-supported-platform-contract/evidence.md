# Host platform baseline and enforcement evidence

Date: 2026-11-12. Scope: host configuration/build checks; no board connected, flashed or measured. Repository base commit: `3ef3da6`, with the existing audit and implementation working-tree changes retained. This is not a release artifact identity manifest.

## Established contract

The current product scope in `openspec/config.yaml` and README is RB-Pirate RP2350B only. [Decision record](../../../docs/architecture/decisions/2026-11-12-platform-support.md) preserves that scope and explicitly does not authorize an RP2040 port.

| Evidence | Observed result |
|---|---|
| `build_rp2350/CMakeCache.txt` | `PICO_PLATFORM=rp2350-arm-s`, `PICO_BOARD=rb-pirate` after SDK normalization. |
| `build_rp2040/CMakeCache.txt` | Also `rp2350-arm-s` / `rb-pirate`; folder name is historical, not RP2040 evidence. |
| Target compile flags | `-DPICO_RP2350=1`, `-mcpu=cortex-m33`. |
| `arm-none-eabi-readelf -h/-A` | ELF32 ARM executable; `Tag_CPU_name: 8-M.MAIN`, `Tag_CPU_arch: v8-M.mainline`. These corroborate Arm target identity; they do not prove physical silicon. |
| SDK version file | Pico SDK 2.3.1 in the existing cached SDK checkout. |
| Compiler | `arm-none-eabi-gcc (15:13.2.rel1-2) 13.2.1 20231009`. |
| Supported memory guard | 388504 bytes static main RAM, 135784 bytes remaining, 616 bytes below the 389120-byte ceiling. |
| Historical other-directory memory guard | Previous confirmed baseline 388524 bytes; still an RP2350 configuration. |
| Supported linker report | FLASH 1026 KB (rounded linker output), main RAM 388504 bytes; SCRATCH_X and SCRATCH_Y each reserve 4 KB. |
| GNU size summary | text 1056768, data 0, bss 358304; this section summary is not the memory guard's complete main-RAM accounting. |
| Physical board, stack high-water, latency | Not measured; hardware acceptance remains BLOCKED pending a connected identified board. |

## Reproducible host checks

```sh
bash tests/test_platform_contract.sh
python3 tools/platform_identity.py build_rp2350 --verify
python3 tools/platform_identity.py build_rp2040 --assert-misleading-dir
cmake --list-presets
cmake --preset rb-pirate-rp2350
cmake --build --preset rb-pirate-rp2350 -j4
bash tests/test_memory_model.sh
arm-none-eabi-readelf -h -A build_rp2350/src/bus_pirate6.elf
bash tests/run_all.sh
openspec validate --all --strict --no-interactive
```

Platform regression checks passed for defaults, supported generic/SDK-normalized spelling, the legacy option, explicit unsupported processors/boards, conflicting options and environment defaults. Top-level unsupported configuration failed before SDK setup, even with a nonexistent SDK path.

Machine-readable build identity generator (`tools/platform_identity.py`) was implemented and bound to `bus_pirate6` POST_BUILD in `src/CMakeLists.txt`, automatically emitting `build_identity.json` upon build. It extracts and verifies definitions, ELF headers (`-h`), CPU architecture tags (`-A`, `v8-M.mainline` / `8-M.MAIN`), memory regions (`-S`, FLASH at `0x10000000`, RAM at `0x20000000` to `0x2005ed98`, scratch stacks at `0x20080000`/`0x20081000`), verifies static RAM usage against ceiling (388,504 <= 389,120 bytes), and audits build directory names to report that directory names such as `build_rp2040` do not prove silicon.

Both requested existing-directory builds and actual supported preset configure/build passed. Captured build logs contain no warning/error diagnostics. Workflow YAML parses locally; GitHub-hosted execution is not claimed. CodeQL no longer pretends to build RP2040, and main CI already targets RP2350. README and active assistant build guidance reflect supported flags. Presets add no runtime firmware memory or resources.

Normal unified suite: **37 passed, 2 hardware skips, 0 failures**, 39 total suites including the platform contract and build identity regression. The separate full host run forcing GCC/G++ to `-O2 -fsanitize=address,undefined -fno-omit-frame-pointer` also passed: **37 passed, 2 hardware skips, 0 failures**. Historical logs: `/tmp/bp-platform-build.log`, `/tmp/bp-platform-other-build.log`, `/tmp/bp-platform-preset.log`, `/tmp/bp-platform-preset-build.log`, `/tmp/bp-platform-tests.log`, `/tmp/bp-platform-o2-sanitized.log`, `/tmp/bp-platform-openspec.log`.

## Physical hardware verification (Group 6)

Connected and validated physical hardware on `/dev/ttyACM0` (CDC CLI) and `/dev/ttyACM1` (BIN):
* Silicon: **Raspberry Pi RP2350B (512KB SRAM, 128Mbit FLASH)**
* Board Model: RB-Pirate (`SER=rbpirate`, VID:PID `1209:7331`)
* Serial Number: `45003CF749C0B0D3`
* Storage: `2.00GB (FAT32 File System)`
* Physical HIL Smoke Suite (`tests/test_hil_smoke.sh`): **ALL PASS** (Info banner, health counters, stack free watermarks, memory budget, features conformance, and 50 mode cycles).
* Physical HIL Soak Suite (`tests/test_soak_runner.sh`): **ALL PASS** (100 mode transitions completed in 159s without leak or stall, followed by soak counters verification).
* Unified test runner (`tests/run_all.sh`): **39 passed, 0 skipped, 0 failed** (100% pass across all suites).

## Remaining acceptance

Final sign-off remains active until the entire production-readiness program completes. Task group 4 CI/artifact alignment is complete. Task group 5 (conditional RP2040-port) is NOT APPLICABLE under the current RP2350-only product decision. Task group 6 physical hardware validation is complete and verified with live boot telemetry and 100-cycle HIL soak execution.
