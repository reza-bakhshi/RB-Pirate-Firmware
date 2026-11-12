# Hardware Validation Evidence: Board Fault Recovery & Resource Lifecycle

Date: 2026-11-13  
Platform: RB-Pirate (RP2350B, 512KB SRAM, 128Mbit Flash)  
Board S/N: `45003CF749C0B0D3`  
Connected Interfaces: USB CDC CLI (`/dev/ttyACM0`), USB CDC Binary (`/dev/ttyACM1`), 2.00 GB FAT32 MicroSD (on-board SPI).

---

## 1. Physical Fixture & Target Inventory

| Fixture / Target | Connected Status | Role / Scope | Acceptance Status |
|---|---|---|---|
| RB-Pirate RP2350B Board | **CONNECTED** (`/dev/ttyACM0`, `/dev/ttyACM1`) | Target Under Test (Core 0 + Core 1) | **QUALIFIED** |
| Onboard MicroSD Slot (2.00 GB FAT32) | **CONNECTED** (authoritative CSD v2.0) | Local filesystem and persistence recovery | **QUALIFIED** |
| CC1101 SubGHz Module | **ABSENT** | SubGHz RF carrier, frame decoders | **BLOCKED** (no external fixture) |
| PN532 NFC/RFID Module | **ABSENT** | 13.56 MHz NFC / RFID tag read/write | **BLOCKED** (no external fixture) |
| nRF24L01+ Transceiver Pair | **ABSENT** | 2.4 GHz GFSK carrier and packet checks | **BLOCKED** (no external fixture) |
| Infrared Demodulator & LED | **ABSENT** | IR toy / remote RX and TX | **BLOCKED** (no external fixture) |
| PS/2 Keyboard / Mouse | **ABSENT** | PS/2 bitstream sniffer | **BLOCKED** (no external fixture) |
| USB Low/Full-Speed Target | **ABSENT** | USB packet sniffer | **BLOCKED** (no external fixture) |
| ST7789 / SSD1306 External Display | **ABSENT** | Secondary external panel bus trace | **BLOCKED** (no external fixture) |
| SIMCom Cellular Modem | **ABSENT** | AT modem UART interface | **BLOCKED** (no external fixture) |
| External CMSIS-DAP / SWD Target | **ABSENT** | Target MCU SWD debug interface | **BLOCKED** (no external fixture) |
| Precision Programmable Electronic Load | **ABSENT** | Overcurrent trip calibration & scope trace | **BLOCKED** (no external fixture) |

Per design rules: **Missing fixtures produce BLOCKED and are never converted into PASS without physical evidence.**

---

## 2. Command Table Enumeration Across All Modes

### Global Commands (`src/commands.c`)
- System & Info: `i` (info), `health` (counters), `fault` (crash record), `memory` (budget/headroom), `kernel` (metrics), `job` (queue), `res` (leases), `features` (conformance), `reboot` (restart), `$` (bootloader), `~` (selftest), `bug` (errata), `ovrclk` (clock/voltage), `rand` (TRNG).
- Configuration: `c` (config menu), `o` (display format), `p`/`P` (pullups), `v`/`V` (voltages), `cls` (clear).
- PSU & Tools: `W`/`w` (PSU enable/disable), `w.cal` (two-point cal wizard / -s show / -d default), `w.profile` (power profiler / CSV streaming), `logic` (analyzer), `glitch` (fault injection), `toolbar` (UI bar).
- Storage & Files: `storage` (mount/status), `ls` (list), `cd` (directory), `mkdir` (create dir), `rm` (delete), `cat` (print), `hex` (hex dump), `format` (FAT format), `label` (disk label), `edit` (text editor), `hexedit` (hex editor), `sha256` (hash).
- Modes & Syntax: `m` (mode selection), `binmode` (binary mode), bus syntax execution (`[` `]` `r` `123` `0x12` `d`/`D` `v.x` `a`/`A`/`@.x`).

### Mode-Specific Command Tables (`src/modes.c`)
1. **HiZ**: Default safe floating mode; setup rollback target.
2. **1WIRE** (`hw1wire_commands`): `scan`, `eeprom`, `ds18b20`.
3. **UART** (`hwuart_commands`): `gps`, `bridge`, `glitch`, `cellmodem`, `cellnet`, `cellops`, `cellsim`, `cellpin`, `cellbook`, `cellsms`, `cellcall`, `cellussd`, `cellmode`, `cellat`.
4. **HDUART** (`hwhduart_commands`): `bridge`.
5. **I2C** (`hwi2c_commands`): `scan`, `sniff`, `eeprom`, `ddr5`, `ddr4`, `sht3x`, `sht4x`, `si7021`, `ms5611`, `tsl2561`, `tcs3472`, `fusb302`, `i2c`, `usbpd`, `mpu6050`.
6. **SPI** (`hwspi_commands`): `flash`, `eeprom`.
7. **2WIRE** (`hw2wire_commands`): `sle4442`, `sniff`.
8. **3WIRE** (`hw3wire_commands`): standard 3-wire bit-bang syntax.
9. **DIO** (`dio_commands`): direct buffered digital I/O.
10. **LED** (`hwled_commands`): WS2812 / APA102 pixel protocol.
11. **INFRARED** (`infrared_commands`): `test`, `tvbgone`, `irtx`, `irrx`, `send`, `receive`, `setprotocol`, `devicebgone`, `remote`, `record`, `load`, `replay`, `jam`, `config`.
12. **SUBGHZ** (`subghz_commands`): `setfrequency`, `rssi`, `preset`, `protocol`, `info`, `scan`, `sweep`, `trace`, `waterfall`, `receive`, `hop`, `replay`, `record`, `load`, `send`, `decode`, `clone`, `savekey`, `monitor`, `keyload`, `keys`, `keyadd`, `txallow`, `power`, `bitrate`.
13. **RF24** (`rf24_commands`): `scan`, `sniff`, `carrier`, `tx`, `rx`, `clone`, `status`, `config`.
14. **RFID** (`rfid_commands`): `mifare`, `em4100`, `protocols`, `tune`, `sniff`, `clone`, `dump`, `emulate`.
15. **JTAG** (`jtag_commands`): `scan`, `chain`.
16. **PS2** (`ps2_commands`): `sniff`, `keyboard`, `mouse`.
17. **LCDTAP** (`lcdtap_commands`): `spi4`, `spi3`, `i2c`.
18. **USB** (`usb_commands`): `sniff`, `passthrough`.
19. **I2S** (`i2s_commands`): `tone`, `play`.
20. **USBPD** (`usbpd_commands`): `source`, `sink`.
21. **CAN** (`can_commands`): `sniff`, `send`, `filter`.

---

## 3. Build & Memory Model Budget

- Target: `bus_pirate6` (RP2350B, `rb-pirate`)
- Binary Flash Usage: 1,054,872 bytes (6.29% of 16 MB)
- Static Main RAM Usage: 388,688 bytes (432 bytes headroom below 389,120 bytes budget ceiling)
- Dynamic Heap Allocations: 0 (ZERO_DYNAMIC_ALLOC policy strictly enforced)
