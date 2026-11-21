# RB-Pirate Firmware

Custom **RP2350B firmware for the [RB-Pirate hardware](https://github.com/reza-bakhshi/RB-Pirate)**, derived from [Bus Pirate 5/6 firmware](https://github.com/DangerousPrototypes/BusPirate5-firmware) by Dangerous Prototypes. It provides an interactive serial terminal for digital protocols, hardware measurements, logic capture, storage, and host automation.

[Build and flash](#build-and-flash) · [Documentation](docs/README.md) · [Hardware](https://github.com/reza-bakhshi/RB-Pirate) · [Host toolkit](https://github.com/reza-bakhshi/RB-Pirate-Skill)

## Hardware compatibility

This repository's build configuration supports **RP2350 Arm Secure on `rb-pirate` only**. Mainline Bus Pirate 5, 5XL, 6, and 7 boards require their corresponding upstream firmware. The output target retains the name `bus_pirate6` for this custom board.

The base board supplies buffered I/O, configurable target power, voltage/current monitoring, USB connectivity, and microSD storage. RF, NFC, and infrared functions require the appropriate external modules and wiring.

## Capabilities

| Area | Workflows | Reference |
|---|---|---|
| Wired protocols | I²C, SPI, UART, 1-Wire, 2-Wire, 3-Wire, digital I/O, and serial LEDs | [Documentation index](docs/README.md) |
| Host interfaces | Interactive terminal and binary communication | [USB guide](docs/architecture/usb_communication_guide.md), [binary modes](docs/guides/binary_mode_guide.md) |
| Measurements and capture | I/O voltage, target power, logic capture, and diagnostics | [System monitor](docs/guides/system_monitor_guide.md) |
| Removable storage | Configuration, captures, and files on microSD | [Storage guide](docs/architecture/storage-guide.md) |
| External RF modules | CC1101 Sub-GHz and nRF24L01+ workflows | [Sub-GHz](docs/guides/subghz_guide.md), [RF24](docs/guides/rf24_guide.md) |
| External NFC reader | PN532 integration | [RFID guide](docs/guides/rfid_guide.md) |
| Display traffic | Passive display-bus capture and reconstruction | [LCDTAP guide](docs/guides/lcdtap_guide.md) |

Available commands depend on the build, selected mode, and attached hardware. Check the device's help and `features` output rather than assuming every upstream command is supported.

## Build and flash

### Requirements

- CMake 3.21 or newer.
- Python 3 and Git.
- Arm GNU toolchain (`arm-none-eabi-gcc`); the existing build documentation specifies GCC 13 or newer.
- Raspberry Pi Pico SDK; the existing build documentation specifies SDK 2.1.0 or newer.

```bash
git clone https://github.com/reza-bakhshi/RB-Pirate-Firmware.git
cd RB-Pirate-Firmware
cmake -S . -B build_rp2350 \
  -DPICO_SDK_FETCH_FROM_GIT=TRUE \
  -DPICO_PLATFORM=rp2350 \
  -DPICO_BOARD=rb-pirate
cmake --build build_rp2350 --parallel --target bus_pirate6
```

The generated firmware is `build_rp2350/src/bus_pirate6.uf2`. To use a locally installed SDK, configure `PICO_SDK_PATH` instead of fetching it.

1. Hold **BOOTSEL** while connecting the RB-Pirate over USB, or use the terminal's `$` bootloader command.
2. Copy the UF2 built for this board to the RP2350 boot drive.
3. Reconnect with a serial terminal, select the interactive CDC port, and follow the startup prompt.

### First terminal commands

| Command | Purpose |
|---|---|
| `i` | Board and firmware information |
| `features` | Compiled feature inventory |
| `health` | Runtime diagnostic information |
| `fault` | Recorded fault diagnostics |

Check the target's voltage and connections before enabling power or pull-ups. Module-specific wiring and commands belong in the linked protocol guides.

## Architecture

The custom implementation separates USB, terminal, storage, and monitoring work on Core 0 from protocol execution on Core 1. The repository also contains static memory management, peripheral resource leases, watchdog/crash diagnostics, and bounded inter-core communication.

- [Dual-core guide](docs/architecture/dual_core_guide.md)
- [Memory model](docs/architecture/memory-model.md)
- [Cooperative kernel](docs/architecture/kernel-guide.md)
- [Differences from upstream](docs/architecture/upstream-divergence.md)

## Validation

Host tests and hardware tests are separate. A host test result does not establish electrical behavior or successful operation with an attached target.

```bash
tests/run_all.sh
```

See the [testing guide](docs/testing/testing_guide.md) and [HIL rig guide](docs/testing/hil_test_rig.md) for dependencies, test scope, and on-device checks. This README does not claim a current passing count or production qualification.

## Related projects and credits

- [RB-Pirate](https://github.com/reza-bakhshi/RB-Pirate): base-board KiCad files.
- [RGB & Button HAT](https://github.com/reza-bakhshi/RB-Pirate-RGB-HAT): optional visual feedback and physical input.
- [RB-Pirate Skill](https://github.com/reza-bakhshi/RB-Pirate-Skill): host serial automation and agent references.
- [Dangerous Prototypes Bus Pirate](https://github.com/DangerousPrototypes/BusPirate5-firmware): upstream firmware foundation.

## License

See [LICENSE.TXT](LICENSE.TXT) and the [third-party license guide](docs/licenses.md). Bundled libraries retain their individual terms and notices.
