# PN532 RFID / NFC Mode

The `RFID` mode connects an external NXP PN532 NFC/RFID transceiver module to the Bus Pirate's buffered I/O header via I2C. It supports tag detection, UID reading, memory dumping, sector/page writing, UID cloning to Chinese "Magic" cards, block erasing, and NDEF smart-tag reading/writing.

## Supported Tag Architectures

* **MIFARE Classic 1K / 4K / Mini (ISO/IEC 14443 Type A):**
  * UID reading (4-byte single size or 7-byte double size).
  * Sector authentication using built-in dictionary of standard keys (transport, NFC forum, NDEF, zero keys).
  * Block data reading and writing (16 bytes per block).
* **MIFARE Ultralight / NTAG213 / NTAG215 / NTAG216:**
  * Fast page reading and writing (4 bytes per page).
  * Automatic memory size and Capability Container (CC) detection.
  * NDEF URI and Text record parsing and encoding.
* **Chinese "Magic" Cards (Gen2 / Direct Block 0 Rewritable):**
  * Sector 0 / Block 0 UID and manufacturer block cloning.

---

## Wiring

Connect the PN532 module in **I2C mode** (on standard red PN532 boards, set the DIP switches to `SEL0 = 1`, `SEL1 = 0` for I2C).

| PN532 Pin | Bus Pirate Pin | Description |
| :--- | :--- | :--- |
| **SDA** | **BIO0** (default, selectable BIO0-BIO7) | I2C Data line |
| **SCL** | **BIO1** (default, selectable BIO0-BIO7) | I2C Clock line |
| **GND** | **GND** | Ground |
| **VCC** | **3.3V** | 3.3V Power (do NOT connect to 5V) |

> [!NOTE]
> Enable the Bus Pirate onboard pull-up resistors (`P` command) or ensure your PN532 breakout board has pull-up resistors installed on the I2C bus.

---

## Entering RFID Mode

### Interactive Mode:
```text
m rfid
```

### Direct CLI Flags:
```text
m rfid -s 0 -c 1 -k 100
```
* `-s <pin>`: SDA BIO pin (default: 0)
* `-c <pin>`: SCL BIO pin (default: 1)
* `-k <speed>`: I2C bus speed in kHz (`100` for Standard Mode, `400` for Fast Mode)

When entering the mode, the firmware queries the PN532 firmware registers (`CMD 0x02`) and displays:
```text
[+] PN532 detected (IC=0x32, Ver 1.6), I2C @ 100kHz.
```

---

## Available Commands

### 1. `info`
Queries chip model, firmware version, revision, and supported RF protocols:
```text
RFID> info
```

### 2. `read`
Detects tags placed in the RF field and displays UID, ATQA, SAK, and memory preview.
```text
RFID> read
RFID> read -s
```
* Continuous mode: Polls continuously until any key is pressed to stop.
* `-s`: Single tag read (exits after detecting first tag).

### 3. `dump`
Reads and displays all readable blocks or pages from the tag:
```text
RFID> dump
RFID> dump -f mycard.txt
```
* Displays hex bytes and ASCII representation.
* `-f <filename>`: Saves full tag memory dump to a text file on the TF-card.

### 4. `write`
Writes data to a specific block or page, or updates the UID on Magic cards:
```text
# Write 16 bytes to MIFARE Classic block 4:
RFID> write -b 4 -d 00112233445566778899AABBCCDDEEFF

# Write 4 bytes to Ultralight / NTAG page 4:
RFID> write -b 4 -d 01020304

# Write 4-byte UID to Chinese Magic Card (Gen2):
RFID> write -u 12345678
```

### 5. `clone`
Interactive tool to clone a 4-byte MIFARE Classic tag onto a Magic Card:
```text
RFID> clone
```
1. Reads source tag UID, SAK, and ATQA.
2. Prompts to remove source tag and place target Magic card.
3. Automatically computes BCC checksum and writes Block 0.

### 6. `erase`
Overwrites tag user data blocks with `0x00`:
```text
RFID> erase
```
* Prompts for confirmation before proceeding.
* Automatically protects MIFARE Classic Sector Trailers (Access Bits / Keys) to avoid bricking cards.

### 7. `ndef`
Reads or writes smart tag NDEF records (URLs and Text):
```text
# Read existing NDEF record from tag:
RFID> ndef

# Write website URL:
RFID> ndef -u https://buspirate.com

# Write text message:
RFID> ndef -m "Bus Pirate 5"
```
