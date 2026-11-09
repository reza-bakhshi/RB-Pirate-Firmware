# Design: add-feature-conformance-tests

## Context

See `proposal.md` — Why.

The codebase now contains 25 independent test suites covering kernel services, memory models, protocol jobs, binary framing, resource managers, and all ported hardware features (IR, SubGHz, RFID, RF24, LCDTap, Cellular, PS/2, and USB). Developers and CI pipelines need a single entry point to execute the complete verification matrix. Additionally, users on interactive hardware need an in-firmware command to inspect feature capabilities, port status, and checklist references directly from the Bus Pirate prompt.

## Goals / Non-Goals

**Goals:**
- Provide `tests/run_all.sh` that iterates through all host unit test suites (`tests/test_*.sh`), runs each under AddressSanitizer and UBSan, aggregates results, and returns non-zero exit code on any regression.
- Provide the `features` global command in firmware (`src/commands/global/cmd_features.c`), displaying a formatted status table of all subsystems with their status (`verified`, `host-tested`, `partial`, `ported`), origin, and checklist references matching `openspec/FEATURES.md`.
- Keep static memory usage zero-heap and within the 380 KB RAM ceiling.

**Non-Goals:**
- Dynamic network testing or running tests requiring live physical radio/modem hardware without stubs.

## Decisions

### Decision 1: Unified Test Runner Architecture
`tests/run_all.sh` discovers all executable `tests/test_*.sh` scripts (excluding itself). It runs each script, captures outputs, and tracks passes/failures. An overall summary table is output upon completion, ensuring strict CI gating.

### Decision 2: Firmware Feature Command Implementation
`src/commands/global/cmd_features.c` defines a `bp_feature_info_t` table containing:
- Feature name (e.g. `INFRARED`, `SUBGHZ`, `RFID`, `RF24`, `LCDTAP`, `CELL`, `PS2`, `USB`, `BASE`, `BINMODE`)
- Source origin (e.g. `ESP32-Bit-Pirate`, `Flipper Zero`, `Bus Pirate upstream`)
- Status (`host-tested`, `partial`, `ported`, `verified`)
- Capability identifier
- Verification test suite (`test_ir_protocols.sh`, `test_subghz.sh`, etc.)

The command supports:
- `features`: prints a clean table with columns for Feature, Status, Capability, and Test Reference.
- `features <name>`: displays detailed properties and hardware requirements for a specific subsystem.
- `features -v` / `--verbose`: displays full descriptions and known hardware limits.

## Risks / Trade-offs

- **[Risk] Firmware binary size growth** → Mitigation: Feature table is stored as `const` in Flash (`.rodata`), consuming < 1.5 KB of Flash and 0 bytes of RAM.
