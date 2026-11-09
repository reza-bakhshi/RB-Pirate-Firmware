# RB-Pirate Test Bench Inventory

> All hardware required by the feature acceptance checklists is available.  
> This file records what to connect so the checklist results are repeatable.

---

## Bench status

| Item | Status | Notes |
|---|---|---|
| RB-Pirate board (RP2350B) | available | target under test |
| CC1101 SubGHz module | available | 433/868/915 MHz antenna set |
| PN532 NFC/RFID module | available | HSU mode, DIP SEL0=0 SEL1=0 |
| nRF24L01+ modules (2) | available | one TX, one RX/RPD monitor |
| IR receiver | available | 38 kHz demodulator and barrier type |
| IR LED / transmitter | available | driven from IRTX pin |
| IR remote controls | available | NEC/RC5 test remotes |
| PS/2 keyboard | available | for PS2 sniffer |
| PS/2 mouse | available | for PS2 sniffer |
| USB low/full-speed target | available | for USB sniffer |
| LCD target: SPI ST7789/ILI9341 | available | 4-wire SPI, 3.3 V |
| LCD target: I2C SSD1306 | available | 128x64, address 0x3C |
| LCD target: ST7032 text LCD | available | I2C text controller |
| SIMCom-compatible modem | available | with powered SIM |
| Second nRF24/SDR receiver | available | verifies carrier sweeps and RF activity |
| Logic analyzer / scope | available | verifies pins, IR carrier, SPI/UART traffic |

---

## Wiring reference

### CC1101 (SUBGHZ)

| Signal | RB-Pirate |
|---|---|
| SCK | BIO6 |
| MOSI | BIO7 |
| MISO | BIO4 |
| CS | BIO5 |
| GDO0 | BIO0 (default; configurable BIO0-BIO3) |
| VCC | VOUT 3.3 V |
| GND | GND |

### PN532 (RFID, HSU)

| Signal | RB-Pirate |
|---|---|
| PN532 TX | BIO5 (default) or BIO1 |
| PN532 RX | BIO4 (default) or BIO0 |
| VCC | VOUT 3.3 V |
| GND | GND |
| DIP SEL0 | 0 (OFF) |
| DIP SEL1 | 0 (OFF) |

Do not enable the terminal/debug UART on the same BIO pins.

### nRF24L01+ (RF24)

| Signal | RB-Pirate |
|---|---|
| CE | BIO0 (default) |
| CSN | BIO5 |
| SCK | BIO6 |
| MOSI | BIO7 |
| MISO | BIO4 |
| VCC | VOUT 3.3 V only |
| GND | GND |

Use a 10 uF capacitor close to the module. A second module or SDR is used to confirm TX and carrier sweeps.

### INFRARED

| Signal | RB-Pirate |
|---|---|
| IR TX | BIO4 (default) |
| IR RX demod | BIO5 (default) |
| IR RX barrier | BIO3/BIO5/BIO7 selectable |
| VCC | VOUT according to the receiver/LED board |
| GND | GND |

### LCDTAP

| Bus | Wiring |
|---|---|
| SPI4 | IO0=CS, IO1=CLK, IO2=MOSI, IO3=D/C, IO4=RST |
| SPI3 | IO0=CS, IO1=CLK, IO2=9-bit MOSI, IO4=RST |
| I2C | IO0=SDA, IO1=SCL |

The tap joins the target bus; it does not drive the panel. Keep wires short and use mode 0, MSB-first SPI.

### CELL

| Signal | RB-Pirate |
|---|---|
| Modem TX | UART RX BIO |
| Modem RX | UART TX BIO |
| VCC | VOUT according to modem (usually 3.3-4.2 V) |
| GND | GND |

Use the UART mode baud required by the modem (commonly 115200).

### PS/2 and USB sniffers

| Feature | Wiring |
|---|---|
| PS2 | CLK/DAT on the documented BIO pins, device powered appropriately |
| USB | D+/D- on BIO1/BIO2, trigger on BIO0, start on BIO3, target VBUS/GND according to the sniffer guide |

---

## Test evidence rules

- Record the module model and wiring in each checklist.
- Attach the serial log and the second-device/SDR result.
- A module being present is not evidence; the checklist command must produce the expected observable result.
- If a module fails, record `FAIL` and open/assign the defect to the owning F change.
- Re-run the full checklist after the fix.
