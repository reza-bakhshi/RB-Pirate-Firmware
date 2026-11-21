# Build System and Supported Target

This fork supports **RP2350 Arm Secure on the `rb-pirate` board**. The platform check in [cmake/bp_platform.cmake](../../cmake/bp_platform.cmake) rejects RP2040 and other board selections before Pico SDK initialization.

## Requirements

- CMake 3.21 or newer, Python 3, and Git.
- An Arm GNU embedded toolchain (`arm-none-eabi-gcc`); GCC 13 or newer is specified by the project README.
- Raspberry Pi Pico SDK; version 2.1.0 or newer is specified by the project README.

## Configure and build

From the repository root:

```bash
cmake -S . -B build_rp2350 \
  -DPICO_SDK_FETCH_FROM_GIT=TRUE \
  -DPICO_PLATFORM=rp2350 \
  -DPICO_BOARD=rb-pirate
cmake --build build_rp2350 --parallel --target bus_pirate6
```

For an installed SDK, replace the fetch option with `-DPICO_SDK_PATH=/path/to/pico-sdk`.

The repository also contains [CMakePresets.json](../../CMakePresets.json). Inspect its environment assumptions before using the `rb-pirate-rp2350` preset.

## Target and outputs

| Setting | Value |
|---|---|
| Board | `rb-pirate` |
| Platform | `rp2350` / `rp2350-arm-s` |
| CMake executable target | `bus_pirate6` |
| Compile-time board identifiers | `BP_VER=6`, `BP_REV=10` |
| Platform implementation | [rb-pirate.c](../../src/platform/rb-pirate.c), [rb-pirate.h](../../src/platform/rb-pirate.h) |
| UF2 output | `build_rp2350/src/bus_pirate6.uf2` |
| Debug output | `build_rp2350/src/bus_pirate6.elf` |

The executable name is inherited from the upstream build system. It does not imply compatibility with a stock Bus Pirate 6 board.

## Flashing

Hold BOOTSEL while connecting the custom board over USB, then copy the board-specific UF2 to the RP2350 boot drive. The firmware terminal's `$` command can also enter the bootloader.

## Source entry points

| File | Role |
|---|---|
| [CMakeLists.txt](../../CMakeLists.txt) | Platform validation, SDK import, and project setup |
| [cmake/bp_platform.cmake](../../cmake/bp_platform.cmake) | Supported platform and board contract |
| [src/CMakeLists.txt](../../src/CMakeLists.txt) | Source selection, target definitions, and linked dependencies |
| [src/boards](../../src/boards) | Pico SDK board definitions |
| [src/platform](../../src/platform) | Hardware platform code |

Use a new build directory when changing toolchain or SDK settings. Old RP2040 and upstream multi-board build instructions do not apply to this fork.

## Validation

See the [testing guide](../testing/testing_guide.md) and [HIL rig guide](../testing/hil_test_rig.md). A successful build alone does not validate electrical operation or attached modules.
