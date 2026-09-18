# PN532 RFID / NFC Mode

The `RFID` mode connects an external NXP PN532 NFC/RFID transceiver module to the Bus Pirate's buffered I/O header via **High-Speed UART (HSU)**. It supports multi-protocol discovery, raw frame/APDU exchange, UID reading, binary memory backup/restore, sector/page writing, UID cloning to compatible "Magic" cards, block erasing, and NFC Forum Type 2/3 NDEF operations.

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
* **ISO/IEC 14443 Type B, FeliCa, and Jewel/Topaz:**
  * Passive target discovery and raw frame exchange.
  * NFC Forum Type 3 NDEF extraction from FeliCa tags.
* **ISO/IEC 14443-4 smart cards:**
  * Raw APDU exchange after Type A or Type B activation.

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

### 3. `scan`
Polls every passive protocol exposed by the PN532 `InListPassiveTarget` command:
```text
RFID> scan
RFID> scan -v
RFID> scan -t 1
```
The `-t` mask follows `nfc-list`: `1` Type A, `2` FeliCa 212, `4` FeliCa 424,
`8` Type B, `256` Jewel, and `512` NFC Barcode. NFC Barcode is reported as not
implemented because it needs a manual ISO14443A-2 bit/parity capture path; the
PN532 has no Barcode `BrTy` polling mode.

### 4. `dump` and `restore`
Reads and displays all readable blocks or pages from the tag:
```text
RFID> dump
RFID> dump -f mycard.txt
RFID> dump -b mycard.bin
RFID> restore -f mycard.bin
```
* Displays hex bytes and ASCII representation.
* `-f <filename>`: Saves full tag memory dump to a text file on the TF-card.
* `-b <filename>`: Saves a fixed-size binary image suitable for `restore`.
* `restore` preserves manufacturer/UID bytes and Ultralight lock/config pages by default.
* `restore -m` also writes manufacturer data. Use it only with a compatible
  direct-write magic tag; ordinary tags may be permanently damaged.

### 5. `write`
Writes data to a specific block or page, or updates the UID on Magic cards:
```text
# Write 16 bytes to MIFARE Classic block 4:
RFID> write -b 4 -d 00112233445566778899AABBCCDDEEFF

# Write 4 bytes to Ultralight / NTAG page 4:
RFID> write -b 4 -d 01020304

# Write 4-byte UID to Chinese Magic Card (Gen2):
RFID> write -u 12345678
```

### 6. `clone`
Interactive tool to clone a 4-byte MIFARE Classic tag onto a Magic Card:
```text
RFID> clone
```
1. Reads source tag UID, SAK, and ATQA.
2. Prompts to remove source tag and place target Magic card.
3. Automatically computes BCC checksum and writes Block 0.

### 7. `erase`
Overwrites tag user data blocks with `0x00`:
```text
RFID> erase
```
* Prompts for confirmation before proceeding.
* Automatically protects MIFARE Classic Sector Trailers (Access Bits / Keys) to avoid bricking cards.

### 8. `ndef`
Reads or writes smart tag NDEF records (URLs and Text):
```text
# Read existing NDEF record from tag:
RFID> ndef

# Write website URL:
RFID> ndef -u https://buspirate.com

# Write text message:
RFID> ndef -m "Bus Pirate 5"
```

### 9. `forum3`
Reads the NFC Forum Type 3 attribute block and extracts the raw NDEF message:
```text
RFID> forum3
RFID> forum3 -f message.ndef
```

### 10. `transceive`
Selects a tag and exchanges a raw frame or ISO14443-4 APDU:
```text
RFID> transceive -p a -d 00A4040000
RFID> transceive -p f212 -d 0600FFFFFFFFFFFF
RFID> transceive -p jewel -d 0000A1B2C3D4
```
Protocols are `a`, `b`, `f212`, `f424`, and `jewel`. Add `-c` to use the
PN532 transparent `InCommunicateThru` command instead of `InDataExchange`.

### 11. `jewel`
Reads and restores the 120-byte static memory image used by `nfc-jewel`:
```text
RFID> jewel -r card.jwd
RFID> jewel -w card.jwd
```
Block 0 and reserved block 13 are never written. Lock and OTP bytes are also
preserved unless explicitly enabled with `-l` and `-o`; both can be irreversible.

### 12. `forum4`
Emulates an NFC Forum Type 4 NDEF tag for a phone or external NFC reader:
```text
RFID> forum4
RFID> forum4 -f input.ndef -o updated.ndef
RFID> forum4 -1
```
The input and output files contain the raw NDEF message (without the two-byte
Type 4 NLEN field). The built-in message is used when no input file is given.
Use `-1` for the v1.0 application/CC mapping; v2.0 is the default.

### 13. `tools`
Prints a quick mapping from familiar libnfc utilities to Bus Pirate commands,
including operations that need additional hardware.

## libnfc Utility Coverage and Limits

| libnfc utility | Bus Pirate equivalent |
| :--- | :--- |
| `nfc-list` | `scan`, `read` |
| `nfc-scan-device` | `info` (the configured PN532 is the device) |
| `nfc-mfclassic` | `dump -b`, `restore`, `write`, `clone`, `erase` |
| `nfc-mfultralight` | `dump -b`, `restore`, `write`, `ndef` |
| `nfc-read-forum-tag3` | `forum3` |
| `nfc-emulate-forum-tag4` | `forum4` |
| `nfc-jewel` | `jewel` (120-byte JWD format) |
| ISO14443-4 APDU access | `scan`, `transceive` |

A true `nfc-relay-picc` setup requires two NFC radios: one initiator beside the
real tag and one target beside the reader.
One Bus Pirate plus one PN532 cannot be both endpoints at the same time. NFC
Barcode support needs the PN532 register-level bit/parity capture path and is
not implemented in this driver yet.
