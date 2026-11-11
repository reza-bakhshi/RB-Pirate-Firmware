<div align="center">

# RB-Pirate Firmware

**Production-grade RP2350B firmware for the RB-Pirate hardware exploration and debugging tool**

[![MCU: RP2350B](https://img.shields.io/badge/MCU-RP2350B-c51a4a?style=flat-square)](#hardware-architecture)
[![Architecture: Dual-Core Zero-Heap](https://img.shields.io/badge/Architecture-Dual--Core%20Zero--Heap-0ea5e9?style=flat-square)](#dual-core-architecture)
[![Storage: TF card](https://img.shields.io/badge/Storage-TF%20card-2f855a?style=flat-square)](#removable-tf-card-storage)
[![Tests: host regression suites](https://img.shields.io/badge/Tests-host%20regression%20suites-10b981?style=flat-square)](#automated-testing--verification)
[![Based on: Bus Pirate 6](https://img.shields.io/badge/Based%20on-Bus%20Pirate%206-314cb0?style=flat-square)](https://github.com/DangerousPrototypes/BusPirate5-firmware)
[![License: MIT](https://img.shields.io/badge/License-MIT-f5c518?style=flat-square)](LICENSE.TXT)

</div>

RB-Pirate Firmware empowers developers, security researchers, and hardware enthusiasts to inspect, reverse-engineer, and automate electronic devices directly from an interactive terminal. It bridges host computers to physical buses, RF transceivers, infrared decoders, logic analyzers, smart cards, and programmable power supplies — all accessible through an ergonomic color VT100 CLI and high-speed binary protocols.

RB-Pirate Firmware is an independent project re-architected on the foundation of the [Bus Pirate 5/6 firmware](https://github.com/DangerousPrototypes/BusPirate5-firmware) by [Dangerous Prototypes](https://dangerousprototypes.com/).

> [!IMPORTANT]
> ### RB-Pirate Hardware Only
> This firmware is tailored exclusively for the **[RB-Pirate board](https://github.com/reza-bakhshi/RB-Pirate)** (Raspberry Pi RP2350B). Do not flash this firmware onto mainline Bus Pirate 5, 5XL, 6, or 7 hardware. For upstream hardware, use the official releases from Dangerous Prototypes.

---

## Architecture Innovations

The RB-Pirate firmware incorporates a complete ground-up re-architecture designed for rock-solid real-time performance, zero memory fragmentation, and non-blocking operation.

The current firmware uses a hybrid dual-core layout:

| Core | Current responsibilities |
| --- | --- |
| Core 0 | CLI, command parsing and syntax execution, mode lifecycle, storage service, watchdog supervision |
| Core 1 | TinyUSB CDC/MSC, terminal transport, toolbars, AMUX/PSU monitoring, asynchronous protocol slices |

Protocol jobs share Core 1 with USB and monitoring. Slices must remain bounded;
strict control/data isolation is not complete. See the [architecture guide](docs/architecture/dual_core_guide.md).

### 2. Deterministic Static Memory Model
- **Zero Runtime Heap**: New runtime code uses fixed storage. The memory guard checks selected migrated modules, including MakeHex; legacy FlatCC builder allocation still requires migration. Global zero-heap operation is not yet established.
- **Bounded SRAM Footprint**: Total static allocation is strictly bounded to $\le 380\text{ KB}$ SRAM (out of 512 KB), guaranteeing $>130\text{ KB}$ of runtime safety headroom.
- **Scratch Stacks**: Fixed 4 KB stack allocations located in dedicated scratch SRAM (`SCRATCH_Y` for Core 0, `SCRATCH_X` for Core 1).
- **Poisoned Memory Pools**: Static pool allocators (`bp_mem_pool`) with buffer poison guards and alignment enforcement.

### 3. Cooperative Service Kernel & Asynchronous Jobs
- **Event-Driven Kernel (`bp_kernel`)**: Prioritized event queues and non-blocking software timers eliminate busy-wait loops in the control loop.
- **Protocol Jobs Runtime (`bp_job`)**: Frequency sweeps and selected sniffers execute as Core 1 jobs. Other command paths still execute synchronously on Core 0.
- **Universal Cancellation**: Cooperative `<Ctrl+C>` cancellation is checked between job slices; latency depends on the active callback.

### 4. Hardware Resource Lease Manager (`bp_res`)
- Hardware peripherals (PIO0/PIO1 state machines, DMA channels, hardware interrupts, pin directions, buffer configurations) are managed via explicit transactional leases.
- Guaranteed zero-leak reclamation: Any resource leased by a command or mode is automatically restored on error, unexpected abort, `<Ctrl+C>`, or mode transition.

### 5. Hardware Crash Diagnostics & Watchdog
- Hardware watchdog supervisor cooperatively fed by the kernel loop.
- Non-volatile crash recording (`bp_crash_record`) preserves registers, fault addresses, and stack traces across unexpected panics or hard faults.
- Diagnostic CLI command: `crash` displays the post-mortem analysis of the preceding crash.

### 6. Lock-Free Binary Transport (`bp_bin_transport`)
- High-speed binary communications (Sigrok logic analyzer, Flashrom programmer, automated scripts) run over dedicated USB CDC1 without interfering with the interactive VT100 terminal on CDC0.

---

## Modes and Protocols

### Standard Wired Interfaces
- **I²C**: Master, multi-byte transactions, scanner, 7-bit/10-bit addressing, hardware pull-up supervision, passive bus sniffer.
- **SPI**: High-speed master, modes 0–3, flashrom support, EEPROM/FRAM read/write, passive SPI sniffer with PIO decoding.
- **UART / Half-Duplex UART**: Configurable baud rates (up to 10+ Mbps), auto-baud detection, bridging, hardware flow control.
- **1-Wire / 2-Wire / 3-Wire**: Bit-level operations, DS18B20 thermometer discovery, custom synchronous protocols.
- **Digital I/O & PWM**: Pin state read/write, frequency measurement, clock generation, servo/PWM generator.
- **JTAG / SWD**: Pin discovery, chain scanning, target identification, PicoSWIO probe interface.
- **Smart Cards & Security**: ISO7816 / SLE4442 smart card dump and security code verification.

### Merged Advanced Features

#### 📡 SUBGHZ (TI CC1101 Transceiver)
- **Band Scan & Sweep**: Spectrum energy sweep across 300–348 MHz, 387–464 MHz, and 779–928 MHz with signal strength and activity confidence scoring.
- **Waterfall & Logic Trace**: Live terminal waterfall display and GDO0 signal logic analysis.
- **Demodulation & Sniffing**: Automatic bitrate, frequency deviation, and modulation guessing (ASK/OOK, 2-FSK, 4-FSK, GFSK, MSK).
- **Princeton & Fixed Codes**: Encode and decode PT2262/SC2260 remotes.
- **Bruteforce Generator**: Automated transmission of 12-bit key dictionaries (Nice, Came, Linear, Chamberlain, Holtek, Ansonic).
- **Flipper Integration**: Load and transmit Flipper Zero `.sub` radio captures directly from the TF card.
- **RSSI Ear (`ear`)**: Maps received signal strength directly to an audible PWM audio tone on a buffered pin for directional antenna hunting.
- **KeeLoq & Jamming**: Rolling-code decryption key utilities and continuous carrier/noise jamming.

#### 🕹️ INFRARED (IR Transmitter & Receiver)
- **Protocol Engine**: Complete MakeHex and IRP protocol engine supporting hundreds of consumer electronics protocols.
- **Bit-Pirate Integration**: Full protocol decoder set with pulse-distance, pulse-width, and bi-phase decoders.
- **Flipper `.ir` Support**: Load, parse, and transmit standard Flipper `.ir` remote control files.
- **TV-B-Gone**: Embedded database of universal power codes for display turn-off testing.
- **Raw Capture & Replay**: High-resolution pulse/space capture, visualization, file export, and repeat transmission.

#### 🖥️ LCDTAP (Passive External Display Sniffer)
- **Passive Sniffing**: Non-intrusive capture of display bus traffic across I²C, 4-line SPI, and 9-bit SPI3 without loading the bus.
- **Live Framebuffer Reconstruction**: Real-time rendering of controller commands for ST7032, ST7789, ILI9341, SSD1306, and SSD1331 controllers.
- **Direct Image Export**: Capture live displays and export full-color `.bmp` bitmap screenshots directly to the TF card.
- **Hardware Protection**: Zero PIO teardown leaks and safe high-impedance state when idle.

#### 📻 RF24 (Nordic nRF24L01+ 2.4 GHz)
- **2.4 GHz Spectrum Sweep**: Real-time channel activity sweep and waterfall across the 2.400–2.525 GHz ISM band.
- **Enhanced ShockBurst**: Packet transmission, reception, dynamic payload length, auto-acknowledgment decoding, and jamming.

#### 🏷️ RFID / NFC (NXP PN532 over HSU UART)
- **Contactless Interrogation**: ISO14443A/B, FeliCa, and NFC forum tag detection.
- **MIFARE Classic / Ultralight**: Key authentication (Key A / Key B), sector dump, block write, and UID emulation.
- **Direct LibNFC Bridge**: Transparent command tunneling for host NFC software suites.

#### 📱 CELL (SIMCom Cellular AT Engine)
- **Integrated AT Supervisor**: AT command engine integrated inside UART mode.
- **Cooperative Background Processing**: Unsolicited Result Code (URC) processing without locking terminal interaction.
- **Cellular Diagnostic Toolkit**: One-touch inspection of signal quality (CSQ), network registration (CREG/COPS), SIM status, SMS reading/sending, and voice call controls.

---

## Removable TF Card Storage

- **Storage Engine**: SPI mode SD/TF driver backed by FatFS 0.15.
- **Serialized Storage Access**: The storage service runs on Core 0; MSC callbacks run on Core 1 and require block-device serialization.
- **Hotplug Resilient**: Hardware insertion/removal detection with automatic unmount and mount recovery.
- **Mass Storage Class (MSC)**: Mounts the TF card as a standard USB thumb drive on your PC for dragging and dropping scripts, captures, firmware updates, and `.sub` / `.ir` files.

---

## Automated Testing & Verification

The codebase includes an extensive suite of automated host tests, sanitizers, and Hardware-in-the-Loop (HIL) harnesses:

```bash
# Run the entire test suite (host suites; physical HIL is skipped without HIL_PORT)
tests/run_all.sh

# Run the grammar and command-parser fuzz testing suite (10,000+ malformed vectors)
tests/test_parser_fuzz.sh

# Run memory model verification (verifies zero-heap and <=380 KB limit)
tests/test_memory_model.sh

# Select a device for physical HIL; use --mock only for harness testing
python3 tests/hil_test_suite.py --smoke --cycles 1000 --soak 60
```

### Diagnostic CLI Commands
- `features` — Dumps active compiled modules, kernel timers, and memory status.
- `crash` — Inspects the non-volatile post-mortem crash record from the previous boot.
- `memory` — Real-time static RAM utilization breakdown.

---

## Build and Installation

### Requirements
- **CMake** (3.21+)
- **Python 3**
- **Raspberry Pi Pico SDK** (v2.1.0+)
- **ARM GCC Toolchain** (`arm-none-eabi-gcc`)

### 1. Configure
```bash
cmake -S . -B build_rp2350 -DPICO_SDK_FETCH_FROM_GIT=TRUE -DBP_PICO_PLATFORM=rp2350
```
*(To use an existing local SDK installation, pass `-DPICO_SDK_PATH=/path/to/pico-sdk -DPICO_SDK_FETCH_FROM_GIT=FALSE`)*.

### 2. Compile
```bash
cmake --build build_rp2350 --parallel --target bus_pirate6
```
The compiled binaries will be output to:
- `build_rp2350/src/bus_pirate6.uf2`
- `build_rp2350/src/bus_pirate6.elf`
- `build_rp2350/src/bus_pirate6.bin`

### 3. Flash to Hardware
1. Connect the RB-Pirate to your computer while holding down the **BOOTSEL** button.
2. A mass storage volume named **RP2350** will mount on your host operating system.
3. Drag and drop `build_rp2350/src/bus_pirate6.uf2` onto the volume.
4. The board will reboot automatically into the new RB-Pirate firmware.

---

## Related Projects

- **[RB-Pirate](https://github.com/reza-bakhshi/RB-Pirate)** — RB-Pirate hardware design (RP2350B, 2-layer KiCad 10 PCB).
- **[RB-Pirate RGB & Button HAT](https://github.com/reza-bakhshi/RB-Pirate-RGB-HAT)** — Optional add-on hardware for status LEDs and tactile user controls.

---

## License

RB-Pirate Firmware is released under the [MIT License](LICENSE.TXT).

Third-party libraries and components are subject to their respective upstream licenses as detailed in [docs/licenses.md](docs/licenses.md).

## Trademarks

Bus Pirate™ is a trademark of Where Labs, LLC. This project is an independent fork and is not affiliated with or endorsed by Where Labs, LLC or Dangerous Prototypes.

---

<div align="center">
  <sub>Engineered for reliability, precision, and deep hardware exploration.</sub>
</div>
