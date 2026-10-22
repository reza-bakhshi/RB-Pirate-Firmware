<div align="center">

# RB-Pirate Firmware

**Custom RP2350B firmware for the RB-Pirate hardware exploration and debugging tool**

[![MCU: RP2350B](https://img.shields.io/badge/MCU-RP2350B-c51a4a?style=flat-square)](#firmware-features)
[![Storage: TF card](https://img.shields.io/badge/Storage-TF%20card-2f855a?style=flat-square)](#removable-tf-card-storage)
[![Based on: Bus Pirate 6](https://img.shields.io/badge/Based%20on-Bus%20Pirate%206-314cb0?style=flat-square)](https://github.com/DangerousPrototypes/BusPirate5-firmware)
[![License: MIT](https://img.shields.io/badge/License-MIT-f5c518?style=flat-square)](LICENSE.TXT)

</div>

RB-Pirate Firmware lets developers, makers, and hardware enthusiasts communicate
with, analyze, and debug electronic devices directly from a terminal. It can
explore serial buses, inspect unknown interfaces, capture logic activity, measure
signal voltages, safely power target circuits, work with memory devices and
sensors, and automate common hardware tasks — all with simple commands typed
into a color VT100 terminal, no code required.

RB-Pirate Firmware is an independent project based on the
[Bus Pirate 5/6 firmware](https://github.com/DangerousPrototypes/BusPirate5-firmware)
created by [Dangerous Prototypes](https://dangerousprototypes.com/).

> [!IMPORTANT]
> ### RB-Pirate hardware only
>
> This is a single-board build made exclusively for the
> **[RB-Pirate board](https://github.com/reza-bakhshi/RB-Pirate)**. Do not flash
> it on mainline Bus Pirate 5, 5XL, 6, or 7 hardware. For those devices use the
> official firmware from Dangerous Prototypes.

## Firmware features

### Single-board build

- Board identity lives in its own config file: `src/platform/rb-pirate.h` (+ `src/platform/rb-pirate.c`)
- Pico SDK board header: `src/boards/rb-pirate.h` (`PICO_BOARD=rb-pirate`)
- Bus Pirate 5, 5XL, and 7 platform files, RP2040 platform, and board-selection logic removed
- Pin configuration verified against the RB-Pirate schematic (buffer direction, current sense,
  logic-analyzer sense pins, analog mux selects, RGB, buttons)

### Terminal and logic analysis

- Color VT100 terminal interface
- Live status toolbar with voltage and current monitoring
- Follow-Along Logic Analyzer capture while commands are executed
- Live bus-activity inspection, decoding, and analysis workflows
- RP2350 E9 errata workaround keeps analyzer sense pins from leaking into floating lines when idle

### Removable TF card storage

- Stores global and protocol-mode settings
- Provides space for logs, captures, dumps, scripts, and `.sub` signal files
- Appears as a USB mass-storage device through the firmware's USB storage interface
- Removable and easy to replace or upgrade

## Modes and protocols

- I²C, SPI, UART, half-duplex UART, 1-Wire, 2-Wire, and 3-Wire communication
- Digital I/O and serial LED control
- JTAG and SWD discovery
- EEPROM, flash, FRAM, sensor, and smart-card interaction

### Additional merged modes

- **INFRARED** — MakeHex/IRP protocol engine, full Bit-Pirate protocol set, Flipper `.ir` files,
  record/send/replay/jam, TV-B-Gone
- **SUBGHZ (CC1101)** — band `scan` and `sweep` with activity/confidence, GDO0 logic `trace`,
  `waterfall`, Princeton `send`, `receive` with encoding/bitrate guessing, `replay`, `record`,
  12-bit key `bruteforce` (Nice/Came/Ansonic/Holtek/Linear/Chamberlain), `jam`, Flipper `.sub` `load`,
  RSSI-to-audio `ear`, `setfrequency`, KeeLoq keys, and more
- **LCDTAP** — passive capture of external LCD buses (I²C, 4-line SPI, 9-bit SPI3) with framebuffer
  reconstruction and BMP export (ST7032, ST7789, ILI9341, SSD1306, SSD1331, KS0108)
- **RF24 (nRF24L01)** — channel scan/sweep/waterfall, packet send/receive, jamming
- **RFID (PN532 over HSU UART)** — NFC/RFID operations, MIFARE authentication, libnfc-style commands
- **CELL** — SIMCom-style AT modem commands inside UART mode (info, network, SIM, SMS, calls, USSD, …)

## Build

This project uses `cmake` as the build system.

### Requirements

- CMake
- Raspberry Pi [pico-sdk](https://github.com/raspberrypi/pico-sdk)
- ARM GCC toolchain (installed automatically with the SDK environment)

### Steps

1. Configure the project (run once, or when changing configuration):

    ```
    cmake -S . -B build_rp2350 -DPICO_SDK_FETCH_FROM_GIT=TRUE -DBP_PICO_PLATFORM=rp2350
    ```

    To use a local pico-sdk instead, add `-DPICO_SDK_PATH=/path/to/pico-sdk -DPICO_SDK_FETCH_FROM_GIT=FALSE`.

2. Build the firmware:

    ```
    cmake --build build_rp2350 --parallel --target bus_pirate6
    ```

The build outputs `build_rp2350/src/bus_pirate6.uf2`.

## Installation

1. Hold the **BOOTSEL** button while connecting the board to USB
2. Copy `bus_pirate6.uf2` onto the mounted `RP2350` drive
3. The board reboots into the new firmware automatically

For updates over USB serial, the firmware also exposes a bootloader identification
through the terminal interface.

## Related projects

- **[RB-Pirate](https://github.com/reza-bakhshi/RB-Pirate)** — the RB-Pirate hardware (RP2350B, 2-layer KiCad 10 PCB)
- **[RB-Pirate RGB & Button HAT](https://github.com/reza-bakhshi/RB-Pirate-RGB-HAT)** — optional RGB feedback
  and user button; the firmware provides RGB & Button HAT integration, and RB-Pirate operates normally without it

## Resources

- [Bus Pirate forum](https://forum.buspirate.com/)
- [Firmware documentation](https://firmware.buspirate.com/)
- [Hardware documentation](https://hardware.buspirate.com/)

## License

RB-Pirate Firmware is released under the [MIT License](LICENSE.TXT).

Third-party components and libraries remain subject to their respective licenses
(see [docs/licenses.md](docs/licenses.md)).

## Trademarks

Bus Pirate™ is a trademark of Where Labs, LLC. All rights reserved. This fork is
an independent personal project and is not affiliated with or endorsed by
Where Labs, LLC or Dangerous Prototypes.

---

<div align="center">
  <sub>Built for hardware exploration, debugging, and experimentation.</sub>
</div>
