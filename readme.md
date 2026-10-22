# RB-Pirate Firmware

Custom firmware for the **RB-Pirate** board — a personal, trimmed-down fork of the
[Bus Pirate 5/6 firmware](https://github.com/DangerousPrototypes/BusPirate5-firmware) by DangerousPrototypes.

The Bus Pirate is a universal serial interface tool trusted by hackers since 2008: send commands over
common serial protocols (1-Wire, I2C, SPI, UART, MIDI, serial LEDs, …) to probe and get to know a chip
before prototyping — no code required.

## About this fork

This is a single-board build: every multi-version / multi-platform support that is not needed for the
RB-Pirate hardware has been removed, and the board identity lives in its own config file.

| | |
|---|---|
| Board | RB-Pirate (custom Bus Pirate 6 style hardware, rev 2) |
| Board config | `src/platform/rb-pirate.h` / `src/platform/rb-pirate.c` |
| Pico SDK board header | `src/boards/rb-pirate.h` (`PICO_BOARD=rb-pirate`) |
| MCU | Raspberry Pi **RP2350B** (dual Cortex-M33, 512KB SRAM) |
| Program flash | 128Mbit (16MB) external QSPI flash |
| Storage | **TF card (SPI)** — settings, files and USB mass storage |
| Display | none — full VT100 terminal interface (on-board LCD subsystem removed) |

### Customizations vs upstream

- **Single board target** — removed Bus Pirate 5, 5XL and 7 platform files, the RP2040 platform and the
  obsolete board-selection logic; the build produces the `bus_pirate6` (RB-Pirate) firmware only.
- **TF card instead of NAND flash** — removed the NAND/Dhara storage stack; SD SPI runs on
  `spi0` (CLK `GPIO34`, CDI `GPIO32`, CDO `GPIO35`, CS `GPIO31`), 512-byte sectors, USB MSC geometry fixed.
- **On-board LCD subsystem removed** — display code, images and LCD modes are gone; `LCDTap` below is the
  external-accessory replacement.
- **Pin configuration verified against the RB-Pirate schematic** — BUFDIR/BUFIO, current-sense
  control/detect pins, logic-analyzer sense pins, 74HCT4067 analog mux selects, RGB, buttons.
- **RP2350 E9 errata workaround** — the logic-analyzer sense pins (GPIO20-27) keep their input buffers
  disabled when idle so they don't leak into floating lines; enabled only while sampling.
- **All feature branches merged in:** `add-lcd-tap`, `add-nrf24`, `add-rfid`, `adding-cell`,
  `ir-inferared-improve`, `subGhz` (see below).

## Modes & features

1-Wire, UART (+HDUART), I2C, SPI, 2-Wire, 3-Wire, DIO, LED, JTAG and more from the upstream firmware,
plus these merged/custom modes:

- **INFRARED** — MakeHex/IRP protocol engine, full Bit-Pirate protocol set, Flipper `.ir` files,
  record/send/replay/jam, TV-B-Gone.
- **SUBGHZ** (CC1101) — `scan`, `sweep`, `trace`, `waterfall`, `send`, `receive`, `replay`, `record`,
  `bruteforce` (12-bit fixed-key protocols), `jam`, `load` (Flipper `.sub`), `ear` (RSSI-to-audio tone),
  `setfrequency`, KeeLoq keys, and more.
- **LCDTAP** — passively capture an external LCD bus (I2C / 4-line SPI / 9-bit SPI3), reconstruct the
  framebuffer and save it as a BMP (ST7032, ST7789, ILI9341, SSD1306, SSD1331, KS0108).
- **RF24** (nRF24L01) — channel scan/sweep/waterfall, send/receive packets, jamming.
- **RFID** (PN532 over HSU UART) — NFC/RFID operations, MIFARE authentication, libnfc-style commands.
- **CELL** — SIMCom-style AT modem commands inside UART mode (info, network, SMS, calls, USSD, …).

## Build

This project uses `cmake`; building takes 2 steps:

1. Configure (once):
    ```
    cmake -S . -B build_rp2350 -DPICO_SDK_FETCH_FROM_GIT=TRUE -DBP_PICO_PLATFORM=rp2350
    ```
    Add `-DPICO_SDK_PATH=/path/to/pico-sdk -DPICO_SDK_FETCH_FROM_GIT=FALSE` to use a local pico-sdk.

2. Build:
    ```
    cmake --build build_rp2350 --parallel --target bus_pirate6
    ```

The UF2 image ends up in `build_rp2350/src/bus_pirate6.uf2` — hold BOOTSEL, plug in USB, copy it over.

## Resources

- [Upstream Bus Pirate firmware](https://github.com/DangerousPrototypes/BusPirate5-firmware)
- [Bus Pirate forum](https://forum.buspirate.com/)
- [Firmware documentation](https://firmware.buspirate.com/)
- [Hardware documentation](https://hardware.buspirate.com/)

## Licenses

This project inherits the upstream licenses (see [docs/licenses.md](docs/licenses.md)) and adds third
party components (LcdTap, MakeHex, Bruce bruteforce tables, …) with their own notices.

## Trademarks

Bus Pirate™ is a trademark of Where Labs, LLC. All rights reserved. This fork is an independent
personal project and is not affiliated with or endorsed by Where Labs, LLC or DangerousPrototypes.
