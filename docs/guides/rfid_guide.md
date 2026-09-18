# PN532 RFID / NFC Mode

The `RFID` mode connects an external NXP PN532 NFC/RFID transceiver module to the Bus Pirate's buffered I/O header via **High-Speed UART (HSU)**. It supports tag detection, UID reading, memory dumping, sector/page writing, UID cloning to Chinese "Magic" cards, block erasing, and NDEF smart-tag reading/writing.

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

## Hardware Setup & Wiring

### 1. PN532 DIP Switches
Set the module DIP switches for **High-Speed UART (HSU)** mode:
* **SEL0 = 0 (OFF / LOW)**
* **SEL1 = 0 (OFF / LOW)**

### 2. Pin Connections

| PN532 Pin | Bus Pirate Pin | Description |
| :--- | :--- | :--- |
| **RX** | **BIO4** (default) or **BIO0** | Bus Pirate TX &rarr; PN532 RX |
| **TX** | **BIO5** (default) or **BIO1** | Bus Pirate RX &larr; PN532 TX |
| **GND** | **GND** | Ground |
| **VCC** | **VOUT** | 3.3V Power from Bus Pirate |

> [!IMPORTANT]
> External power is **OFF by default** when entering a mode on the Bus Pirate.
> After selecting RFID mode, enable 3.3V power to the module by typing:
> ```text
> RFID> W 3.3
> ```

---

## Entering RFID Mode

### Interactive Mode:
```text
HiZ> m rfid
```
Prompts will ask for:
1. **UART Pin Pair:**
   * `0`: TX: BIO4, RX: BIO5 (uart0 - Default)
   * `1`: TX: BIO0, RX: BIO1 (uart1)
2. **Baud Rate:**
   * `115200` (Default for PN532 HSU)
   * `9600`

### Direct CLI Flags:
```text
HiZ> m rfid -p 0 -b 115200
```
* `-p <0|1>`: Pin pair (`0` for BIO4/BIO5, `1` for BIO0/BIO1)
* `-b <baud>`: Baud rate (`115200` or `9600`)

---

## Available Commands

Every command provides interactive help via `-h` / `--help`.

### 1. `info`
Queries chip model, firmware version, revision, and supported RF protocols:
```text
RFID> info
```
Output preview:
```text
--- PN532 NFC/RFID Controller ---
Chip IC:           0x32 (PN532)
Firmware Version:  1.6
Supported Modes:   0x07
  - ISO/IEC 14443 Type A (MIFARE)
  - ISO/IEC 14443 Type B
  - ISO/IEC 18092 (FeliCa / NFCIP-1)
Interface:         HSU (UART) @ 115200 baud (TX: BIO4, RX: BIO5)
```

### 2. `read`
Detects tags placed in the RF field and displays UID, ATQA, SAK, tag type, and memory preview.
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
