# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

RB-Pirate firmware — embedded C firmware exclusively for the Raspberry Pi **RP2350** microcontroller (Bus Pirate 6 / RB-Pirate). RP2040 and legacy Bus Pirate 5 targets are completely out of scope. Universal serial interface supporting 1-Wire, I2C, SPI, UART, JTAG, infrared, Sub-GHz, RFID, and other protocols.

### Dual-Core Architecture
- **Core 0 (Control Plane)**: TinyUSB CDC0 (terminal), CDC1 (binary I/O), MSC (storage); Linenoise CLI with syntax highlighting and non-blocking sub-prompts; command parser & syntax compiler; transactional mode lifecycle manager; cooperative job dispatcher with resource leases (`bp_job`, `bp_res`); FatFS flash and SD storage; AMUX sweep & PSU supervision; seqlock snapshot publisher (`bp_ui_snapshot`); hardware watchdog supervisor.
- **Core 1 (Protocol Engine)**: Real-time protocol execution; PIO state machines (PIO0/PIO1); protocol DMA channels and hardware IRQs; captures and decoders; SUMP and BPIO binary engines; FALA follow-along logic analyzer; LCDTAP, IR, Sub-GHz, RFID, and RF24 transceiver engines; fixed-size inter-core log ring.

### Memory Model
Strict static memory model: zero runtime `malloc`, `calloc`, `realloc`, or `free`. Static allocation bounded to $\le 380\text{ KB}$ SRAM with dedicated 4 KB stacks in scratch SRAM (`SCRATCH_Y` for Core 0, `SCRATCH_X` for Core 1).

## Build Commands

```bash
# Configure for RP2350 (RB-Pirate / Bus Pirate 6)
cmake -S . -B build_rp2350 -DPICO_SDK_FETCH_FROM_GIT=TRUE -DBP_PICO_PLATFORM=rp2350

# Build target
cmake --build ./build_rp2350 --parallel --target bus_pirate6
```

Output: `.elf`, `.uf2`, `.bin` files in `build_rp2350/src/`.

## Tests

```bash
# Run all unified test suites under AddressSanitizer and UBSan
tests/run_all.sh

# Run individual suites
tests/test_parser_fuzz.sh
tests/test_hil_smoke.sh
tests/test_soak_runner.sh
tests/test_memory_model.sh
```

Test stubs for hardware headers are in `tests/stubs/`.

## Code Formatting

Clang-format configured (Mozilla style): 120-column limit, 4-space indent, attached braces. Apply with `clang-format -i <file>`.

## Architecture

### Mode System (`src/mode/`)
Protocol modes (HiZ, 1-Wire, UART, I2C, SPI, 2-Wire, 3-Wire, LED, DIO, IR, JTAG, I2S) implement a function-pointer dispatch table defined in `src/modes.h`. Each mode provides: `protocol_start`, `protocol_stop`, `protocol_read`, `protocol_write`, `protocol_setup`, plus mode-specific commands.

### Command System (`src/commands.c`, `src/commands.h`)
140+ global commands organized by category (IO, Configure, System, Files, Script, Tools, Mode). Two command structures coexist — legacy `_global_command_struct` and modern `bp_command_def` (migration in progress). Commands reference translation IDs for help text.

### Syntax Engine (`src/syntax_compile.c`, `src/syntax_run.c`, `src/syntax_post.c`)
Three-phase pipeline: user input → bytecode compilation → execution → output formatting. Bytecode defined in `src/bytecode.h`.

### Platform Abstraction
- **Board headers** (`src/boards/`): Pin assignments, hardware capabilities per board variant
- **Platform files** (`src/platform/`): Rev-specific initialization (e.g., `bpi5-rev10.c/h`, `bpi6-rev2.c`)
- **Hardware drivers** (`src/pirate/`): `bio.c` (bus IO), `psu.c` (power supply), `lcd.c`, `rgb.c`, `storage.c`

### Binary Mode (`src/binmode/`)
FlatBuffers-based binary protocol (schema: `bpio.fbs`) for programmatic USB access. Also hosts SUMP logic analyzer and legacy protocol support.

### Translation System (`src/translation/`)
All user-facing strings use translation ID macros (`T_*`). Languages: `en-us.h`, `zh-cn.h`, `pl-pl.h`. JSON templates in `src/translation/templates/` compiled to headers via `json2h.py`.

### Key Global State
`src/system_config.h` — monolithic struct (130+ fields) holding terminal settings, mode config, PSU state, pin tracking, etc.

## Feature Flags

Modes enabled via `#define BP_USE_*` in `src/pirate.h` (e.g., `BP_USE_HW1WIRE`, `BP_USE_HWI2C`, `BP_USE_HWSPI`). Per-target defines: `BP_VER`, `BP_REV`, `BP_FIRMWARE_HASH`.

## Conventions

- Namespace prefixes: `bio_`, `psu_`, `ui_`, `hw_` for subsystem separation
- New protocol modes: implement the `_mode` struct interface, register in `modes.c`
- New commands: add to the command table in `commands.c` with a `bp_command_def`
- Copyright notices required on new files; modifications need "Modified by" annotation
- 3rd-party code: MIT preferred, document in `docs/licenses.md`, save license to `docs/third_party_licenses/`

## Dependencies

Pico SDK 2.1.0 (fetched or via `PICO_SDK_PATH`), ARM GCC (`arm-none-eabi`), CMake 3.21+, Python 3. Bundled libraries: FlatCC, nanocobs, linenoise (modified), printf-4.0.0, mjson.
