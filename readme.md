<div align="center">

# RB-Pirate Firmware

**Production-grade RP2350B firmware for the RB-Pirate hardware exploration, debugging, and fault-injection tool**

[![Release: v2.0.0-rc1](https://img.shields.io/badge/Release-v2.0.0--rc1-0284c7?style=flat-square)](https://github.com/reza-bakhshi/RB-Pirate-Firmware/releases/tag/v2.0.0-rc1)
[![MCU: RP2350B](https://img.shields.io/badge/MCU-RP2350B%20ARMv8--M-c51a4a?style=flat-square)](#hardware-architecture)
[![Architecture: Dual-Core Zero-Heap](https://img.shields.io/badge/Architecture-Dual--Core%20Zero--Heap-0ea5e9?style=flat-square)](#architecture-innovations)
[![Storage: FAT32 TF Card](https://img.shields.io/badge/Storage-FAT32%20TF%20Card-2f855a?style=flat-square)](#removable-tf-card-storage)
[![Tests: 40/40 Passing](https://img.shields.io/badge/Tests-40%2F40%20Passing-10b981?style=flat-square)](#automated-testing--verification)
[![OpenSpec: 31 Specs Verified](https://img.shields.io/badge/OpenSpec-31%20Specs%20Verified-8b5cf6?style=flat-square)](#openspec-production-readiness)
[![License: MIT](https://img.shields.io/badge/License-MIT-f5c518?style=flat-square)](LICENSE.TXT)

</div>

RB-Pirate Firmware empowers developers, security researchers, and hardware engineers to inspect, reverse-engineer, and automate electronic devices directly from an interactive terminal. It bridges host computers to physical buses, RF transceivers, infrared decoders, logic analyzers, smart cards, and programmable power supplies — all accessible through an ergonomic color VT100 CLI and high-speed binary protocols.

RB-Pirate Firmware is an independent project re-architected on the foundation of the [Bus Pirate 5/6 firmware](https://github.com/DangerousPrototypes/BusPirate5-firmware) by [Dangerous Prototypes](https://dangerousprototypes.com/).

> [!IMPORTANT]
> ### RB-Pirate Hardware Only
> This firmware is tailored exclusively for the **[RB-Pirate board](https://github.com/reza-bakhshi/RB-Pirate)** (Raspberry Pi RP2350B, 512KB SRAM, 128Mbit Flash). Do not flash this firmware onto mainline Bus Pirate 5, 5XL, 6, or 7 hardware. For upstream hardware, use the official releases from Dangerous Prototypes.

---

## Architecture Innovations

The RB-Pirate firmware incorporates a complete ground-up re-architecture designed for rock-solid real-time performance, zero memory fragmentation, and non-blocking operation.

### 1. Asymmetric Dual-Core Architecture
The control and protocol execution paths run on dedicated, isolated CPU cores:

| Core | Responsibilities | Non-Blocking Guarantees |
| --- | --- | --- |
| **Core 0 (Control)** | USB CDC0/CDC1/MSC, VT100 CLI, FlatCC responses, mode management, FatFS/SD storage, AMUX ADC supervision, PSU protection, watchdog | Never blocks on long-running protocol operations; maintains responsive interactive CLI |
| **Core 1 (Protocol)** | Hardware protocol engines, PIO0/PIO1 state machines, DMA channels, protocol interrupts, logic capture, RF/IR/RFID engines | Never calls `printf`, TinyUSB, or FatFS directly; uses lock-free typed SPSC queues and seqlock snapshots |

Core 0 keeps serving control I/O while waiting for Core 1 transactions. See the [architecture guide](docs/architecture/dual_core_guide.md) for details.

### 2. Deterministic Static Memory Model
- **Zero Runtime Heap**: Zero calls to `malloc`, `free`, `realloc`, `calloc`, `new`, or `delete` across all runtime and interrupt paths.
- **Bounded SRAM Footprint**: Static memory is strictly bounded to $\le 389,120\text{ bytes}$ (380 KB) out of 512 KB, guaranteeing $>120\text{ KB}$ of runtime stack and DMA headroom.
- **Dedicated Scratch Stacks**: Fixed 4 KB stack allocations located in dedicated scratch SRAM (`SCRATCH_Y` for Core 0, `SCRATCH_X` for Core 1).
- **Poisoned Memory Pools**: Static pool allocators (`bp_mem_pool`) with buffer poison guards and alignment enforcement.

### 3. Autonomous AMUX Acquisition Engine
- **17-Channel Hardware Sweep**: Continuously samples 8 buffered I/O pins, USB 5V rail, onboard VREG, target VREF/VOUT, overcurrent detection, and direct shunt current.
- **Non-Contiguous Pin Masking**: Hardware selector arithmetic correctly maps non-contiguous GPIO selector lines (`AMUX_S0`..`AMUX_S3`).
- **Seqlock Double-Buffering**: Coherent, monotonic timestamped frames published without tearing or read starvation.
- **Manual Read Pause Arbitration**: Bounded pausing during high-priority manual ADC reads (`v <IOx>`).
- **Stale Telemetry Supervisor**: Automatically detects stalled conversion cycles (>250 ms) and trips PSU safety shutdown.

### 4. Hardware Resource Lease Manager (`bp_res`)
- Hardware peripherals (PIO0/PIO1 state machines, DMA channels, hardware interrupts, pin directions, buffer configurations) are managed via explicit transactional leases.
- **Guaranteed Zero-Leak Reclamation**: Any resource leased by a command or mode is automatically restored on error, unexpected abort, `<Ctrl+C>`, or mode transition (verified across 1,000+ transitions).

### 5. Hardware Crash Diagnostics & Watchdog
- Hardware watchdog supervisor cooperatively fed by the kernel loop.
- Non-volatile crash recording (`bp_crash_record`) preserves registers, fault addresses, and stack watermarks across unexpected panics or hard faults.
- Diagnostic CLI command: `fault` displays post-mortem analysis of the preceding crash, while `health` reports real-time buffer levels and stack margins.

### 6. Atomic PSU Calibration Recovery
- **JSON Calibration Profile**: Stored in `/config/psu_cal.json` with Schema 1 validation, 8 polynomial coefficients, and 20-byte CRC32 little-endian integrity protection.
- **Multi-Stage Candidate Recovery**: Automatically falls back through primary (`psu_cal.json`) $\to$ backup (`psu_cal.bak`) $\to$ legacy profile $\to$ factory defaults.
- **Atomic File Replacement**: Uses temporary file creation, validation, and atomic rename to guarantee persistence safety against unexpected power loss.

### 7. Lock-Free Binary Transport (`bp_bin_transport`)
- High-speed binary communications (Sigrok logic analyzer, Flashrom programmer, automated scripts) run over dedicated USB CDC1 without interfering with the interactive VT100 terminal on CDC0.

---

## Modes and Protocols

### Standard Wired Interfaces
- **I²C**: Master, multi-byte transactions, scanner, 7-bit/10-bit addressing, hardware pull-up supervision, passive bus sniffer.
- **SPI**: High-speed master, modes 0–3, flashrom support, EEPROM/FRAM read/write, passive SPI sniffer with PIO decoding.
- **UART / Half-Duplex UART**: Configurable baud rates (up to 10+ Mbps), auto-baud detection, bridging, hardware flow control.
- **1-Wire / 2-Wire / 3-Wire**: Bit-level operations, DS18B20 thermometer discovery, custom synchronous protocols.
- **Digital I/O & PWM**: Pin state read/write, frequency measurement, clock generation, servo/PWM generator.
- **JTAG / SWD**: Pin discovery, chain scanning, target identification, CMSIS-DAP v1/v2 probe interface.
- **Smart Cards & Security**: ISO7816 / SLE4442 smart card dump and security code verification.
- **CAN Bus**: High-speed CAN transceiver support with hardware frame filtering and monitor mode.

### Advanced Wireless & RF Capabilities

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
- **Decoder Suite**: Full protocol decoder set with pulse-distance, pulse-width, and bi-phase decoders (NEC, RC5, Sony SIRC, Kaseikyo).
- **Flipper `.ir` Support**: Load, parse, record, and transmit standard Flipper `.ir` remote control files.
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
- **Authoritative Capacity Checking**: Bounded CSD v1 and CSD v2 parsing with authoritative sector count verification.
- **Media Generation Tracking**: Every card insertion/removal increments media generation counter; stale I/O requests fail closed.
- **Arithmetic Overflow Protection**: Subtract-before-add bounds checking prevents integer overflow on 32-bit and 64-bit sector boundaries.
- **Mass Storage Class (MSC)**: Mounts the TF card as a standard USB drive on your host PC for dragging and dropping scripts, captures, firmware updates, and `.sub` / `.ir` files.

---

## OpenSpec Production Readiness

The RB-Pirate firmware follows strict, specification-driven development verified under [OpenSpec](https://openspec.dev):

| Specification | Capability Area | Verification Criteria |
|---|---|---|
| `storage` | Storage Service & Capacity | CSD v1/v2 bounds, media generation tracking, Schema 1 CRC32 calibration persistence |
| `power-monitor` | Autonomous AMUX & ADC | 17-channel sweep, seqlock publication, manual read pause arbitration, stale trip |
| `hardware-validation` | Fault Recovery & Lifecycle | 21 mode command suites, 1000 transitions, watchdog crash records, HIL soak tests |
| `platform-support` | Supported Platform Contract | RP2350-only enforcement, ELF `v8-M.mainline` architecture tags, memory ceiling |
| `board-expansion` | Board Expansion Qualification | Electrical conflict analysis, fail-closed guards for unsupported DVI and crowbar |

---

## Automated Testing & Verification

The codebase includes an extensive suite of 40 automated host test suites, AddressSanitizer/UBSan validation, and Hardware-in-the-Loop (HIL) harnesses:

```bash
# Run the entire unified test runner (all 40 suites)
tests/run_all.sh

# Run memory model verification (verifies zero-heap and <=389,120 bytes SRAM ceiling)
tests/test_memory_model.sh

# Run command-parser grammar fuzzing (10,000+ malformed vectors)
tests/test_parser_fuzz.sh

# Run physical Hardware-in-the-Loop (HIL) smoke checks on /dev/ttyACM0
tests/test_hil_smoke.sh

# Run physical HIL soak runner (100 mode transitions + continuous health assertion)
tests/test_soak_runner.sh
```

### Diagnostic CLI Commands
- `i` — Displays hardware information, silicon revision, serial number, and storage capacity.
- `health` — Real-time terminal/binary TX levels, PSU ADC raw telemetry, core tick counters, and free stack watermarks.
- `fault` — Non-volatile crash record from previous boot, panic location, and register dump.
- `memory` — SRAM allocation map, static memory budget headroom, and heap elimination verification.
- `res` — Displays active peripheral, PIO, DMA, and interrupt leases.
- `features` — Displays compiled feature inventory, protocol port statuses, and test coverage indicators.
- `$` — Reset immediately into RP2350 USB BOOTSEL mode for rapid firmware flashing.

---

## Build and Installation

### Requirements
- **CMake** (3.21+)
- **Python 3**
- **Raspberry Pi Pico SDK** (v2.1.0+)
- **ARM GCC Toolchain** (`arm-none-eabi-gcc` 13+)

### 1. Configure
```bash
cmake -S . -B build_rp2350 -DPICO_SDK_FETCH_FROM_GIT=TRUE -DPICO_PLATFORM=rp2350 -DPICO_BOARD=rb-pirate
```

With an SDK already installed locally, use the built-in preset:
```bash
cmake --preset rb-pirate-rp2350
cmake --build --preset rb-pirate-rp2350
```

### 2. Compile
```bash
cmake --build build_rp2350 --parallel --target bus_pirate6
```
The compiled binaries are generated in `build_rp2350/src/`:
- `bus_pirate6.uf2` — USB drag-and-drop firmware image
- `bus_pirate6.elf` — ELF executable with debug symbols
- `bus_pirate6.bin` — Raw binary image
- `build_identity.json` — Machine-readable build identity manifest

### 3. Flash to Hardware
- **Method A (CLI Shortcut)**: In the Bus Pirate terminal CLI, simply type `$` and press Enter. The board will immediately reset into RP2350 USB BOOTSEL mode.
- **Method B (Hardware Button)**: Hold the physical **BOOTSEL** button while connecting the RB-Pirate to your computer via USB.

A mass storage drive named **RP2350** will mount on your host OS. Copy `build_rp2350/src/bus_pirate6.uf2` to the drive:
```bash
cp build_rp2350/src/bus_pirate6.uf2 /media/$USER/RP2350/ && sync
```
The board will reboot automatically into the new RB-Pirate firmware.

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
