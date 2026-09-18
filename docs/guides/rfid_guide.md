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
   * `230400`, `460800`, `921600` (high-speed via SetSerialBaudrate switch)
   * `9600`

### Direct CLI Flags:
```text
HiZ> m rfid -p 0 -b 115200
HiZ> m rfid -p 0 -b 460800
```
* `-p <0|1>`: Pin pair (`0` for BIO4/BIO5, `1` for BIO0/BIO1)
* `-b <baud>`: Baud rate (`9600`, `115200`, `230400`, `460800`, `921600`)
* High rates handshake at 115200 first, then switch with `SetSerialBaudrate 0x10`. Keep wires short (<10cm) at 460800+; a power-cycled PN532 always returns to 115200 and the driver re-switches automatically.

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
RFID> dump -k FFFFFFFFFFFF
RFID> dump -k A0A1A2A3A4A5 -B
RFID> restore -f mycard.bin
```
* Displays hex bytes and ASCII representation.
* `-f <filename>`: Saves full tag memory dump to a text file on the TF-card.
* `-b <filename>`: Saves a fixed-size binary image suitable for `restore`.
* `-k <12 hex>` + `-B`: try your 6-byte key first (Key A by default, Key B with `-B`), then the built-in dictionary (A then B, like `nfc-mfclassic`). The working `Key A/B` is printed per sector.
* `restore` preserves manufacturer/UID bytes and Ultralight lock/config pages by default.
* `restore -m` also writes manufacturer data. Use it only with a compatible
  direct-write magic tag; ordinary tags may be permanently damaged.
* `restore` overwrites Classic sector trailers (keys/access): only restore to clone/magic cards.

### 5. `write`
Writes data to a specific block or page, or updates the UID on Magic cards:
```text
# Write 16 bytes to MIFARE Classic block 4:
RFID> write -b 4 -d 00112233445566778899AABBCCDDEEFF

# Write with a custom Key B:
RFID> write -b 4 -d 00112233445566778899AABBCCDDEEFF -k A0A1A2A3A4A5 -B

# Write 4 bytes to Ultralight / NTAG page 4:
RFID> write -b 4 -d 01020304

# Write 4-byte UID to Chinese Magic Card (Gen2):
RFID> write -u 12345678
```
* Trailer blocks (e.g. 3, 7, 11...) print a brick warning and show the successful `Key A/B`.
* Gen1A magic cards need the `50 00 57 CD` backdoor unlock first (not implemented); Gen2/CUID cards work directly.

### 6. `clone`
Interactive tool to clone a 4-byte MIFARE Classic tag onto a Magic Card:
```text
RFID> clone
```
1. Reads source tag UID, SAK, and ATQA.
2. Prompts to remove source tag and place target Magic card.
3. Automatically computes BCC checksum and writes Block 0.
* Gen2/CUID targets work out of the box. Gen1A targets need the backdoor unlock (`nfc-mfsetuid` style), which must be sent as raw bytes via `transceive -p a -d 500057CD -c`.

### 7. `erase`
Overwrites tag user data blocks with `0x00`:
```text
RFID> erase
RFID> erase -k FFFFFFFFFFFF
```
* Prompts for confirmation before proceeding.
* Automatically protects MIFARE Classic Sector Trailers (Access Bits / Keys) and block 0 to avoid bricking cards.
* NTAG erase is limited to user pages (`4..4+data_pages-1`); lock/config/password pages are preserved.
* `-k/-B` selects the Classic key tried first.

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

### 13. `pwauth`
NTAG/Ultralight-EV1 password authentication (`nfc-mfultralight --pw` equivalent):
```text
RFID> pwauth -p FFFFFFFF
RFID> pwauth -p 12345678
```
Runs `GET_VERSION 0x60` (8 bytes: vendor/product/subtype/major/minor/size/protocol)
then `PWD_AUTH 0x1B`. On success prints the 2-byte PACK. Protection lasts until
the tag leaves the RF field.

### 14. `scanner`
Bulk Tag Scanner inventory (Killer-style tag collection on a real PN532):
```text
RFID> scanner
RFID> scanner -f tags.csv -n 20
```
* Polls ISO14443A continuously, prints `[ms] #N UID ATQA SAK (type)` for every **new** UID (up to 64 tracked).
* `-n <num>`: stop after N unique tags; otherwise any key stops.
* `-f <file>`: saves `index,uid,uid_len,atqa,sak,type` CSV to the TF-card for classroom inventories.

### 15. `nonce`
Mifare nested-auth nonce collector for offline key recovery (honest PN532 version of `mfkey32v2`/`mfkey64` sniffing):
```text
RFID> nonce -b 4
RFID> nonce -b 4 -B -n 20 -f nonces.csv
```
* Sends raw `AUTH (60/61 + block)` via transparent `InCommunicateThru`, captures the 4-byte tag nonce (NT), then sends a random reader nonce+response (NR+AR) and logs the tag reply (AT/NACK/timeout).
* Re-selects the tag every sample (failed AUTH drops the tag). ~2-4 samples/sec: **minutes, not Killer-seconds**. Keep the tag still.
* `-n <2..64>` (default 8), `-B` for Key B, `-f` saves `uid,block,key_type,nt,nr,ar,at` CSV.
* Crack on a PC (RP2040 is too slow), e.g.:
```text
mfkey32v2 <UID> <NT> <NR0> <AR0> <NR1> <AR1>   # two samples, same NT
mfkey64 <UID> <NT> <NR> <AR> <AT>               # one full session
```
* Teach: even *failed* auths leak `(NT, NR, AR)` Crypto1 equations; nested nonces come from the tag's weak PRNG, which is why collection needs many samples.

### 16. `tools`
Prints a quick mapping from familiar libnfc utilities to Bus Pirate commands,
including operations that need additional hardware.

## libnfc Utility Coverage and Limits

| libnfc utility | Bus Pirate equivalent |
| :--- | :--- |
| `nfc-list` | `scan`, `read` (`scan -v` shows ATQA/SAK/NFCID2/PUPI/ATQB) |
| `nfc-scan-device` | `info` (the configured PN532 is the device) |
| `nfc-mfclassic` | `dump -b`, `restore`, `write [-k key] [-B]`, `clone`, `erase` (Keys A+B) |
| `nfc-mfultralight` | `dump -b`, `restore`, `write`, `ndef`, `pwauth` (`--pw` equivalent) |
| `nfc-read-forum-tag3` | `forum3` |
| `nfc-emulate-forum-tag4` | `forum4` |
| `nfc-jewel` | `jewel` (120-byte JWD format) |
| `nfc-mfsetuid` (Gen1A) | Not implemented: Gen1A needs backdoor `50 00 57 CD`; use Gen2 cards with `clone` / `write -u` |
| ISO14443-4 APDU access | `scan`, `transceive` (e.g. DESFire `9060000000` GetVersion) |
| Bulk inventory (Killer Tag Scanner) | `scanner [-f inventory.csv] [-n num]` |
| Nested-auth research (mfkey32v2/mfkey64) | `nonce -b block [-B] [-n num] [-f nonces.csv]` + PC cracking (RP2040 too slow; PN532 ~2-4 samples/sec) |

A true `nfc-relay-picc` setup requires two NFC radios: one initiator beside the
real tag and one target beside the reader.
One Bus Pirate plus one PN532 cannot be both endpoints at the same time. NFC
Barcode support needs the PN532 register-level bit/parity capture path and is
not implemented in this driver yet.

## Card Memory Maps (classroom reference)

All addresses below are what `dump`/`restore`/`write -b` use.

### MIFARE Classic 1K (SAK 0x08): 16 sectors x 4 blocks, 64 blocks x 16B = 1024B
* Sector `s` (0-15): blocks `s*4 .. s*4+3`. Block `s*4+3` is the **trailer**: `KeyA(6) Access(4) KeyB(6)`.
* Block 0: `UID(4) BCC(1) SAK(1) ATQA(2) Manufacturer(8)`. BCC = UID0^UID1^UID2^UID3.
* Auth is per-sector: authenticate `first_block` with Key A (`0x60`) or Key B (`0x61`), then read/write any block in the sector. Dictionary tries A then B.
* `dump -k <12 hex> [-B]` tries your key first (e.g. `-k FFFFFFFFFFFF`, `-k A0A1A2A3A4A5 -B`).
* `restore` overwrites trailers: only restore to clone/magic cards. `erase` skips trailers + block 0.
* Never `write` a trailer unless you understand access bits: wrong bits = bricked sector.

### MIFARE Classic 4K (SAK 0x18): 40 sectors = 32x4 + 8x16, 256 blocks = 4096B
* Sectors 0-31: 4 blocks each (blocks 0-127). Sectors 32-39: 16 blocks each (blocks 128-255).
* Trailer of big sectors: last block of each 16 (`143, 159, ... 255`).

### MIFARE Mini (SAK 0x09): 5 sectors x 4 blocks, 20 blocks = 320B
* Same layout as 1K, truncated.

### Ultralight (SAK 0x00): 16 pages x 4B = 64B
* Pages 0-1: UID, page 2: lock bits, page 3: CC (`E1 10 ...`), pages 4-15: user (48B).
* One `READ 0x30` returns 4 pages (16B). `dump` uses that; `ndef` scans pages 4+ for TLV `03 ... FE`.

### NTAG213/215/216 (SAK 0x00, CC byte 2 = 0x12/0x3E/0x6D)
* NTAG213: 45 pages (180B), user 36 pages from 4. NTAG215: 135 pages (540B), user 126. NTAG216: 231 pages (924B), user 222.
* Tail pages are lock/dynamic-lock/config/password/PACK: `dump -b` saves them, `restore -m` writes them, `erase` preserves them (only `4..4+data_pages-1` zeroed).
* Password-protected tags: `pwauth -p <8 hex>` sends `PWD_AUTH 1B + PWD(4)`, tag returns PACK(2).

### Jewel/Topaz (Type 1, 120B JWD)
* Bytes 0-119; block 0 = UID/header, block 13 reserved. `jewel -r/-w` preserves both; `-l/-o` unlock lock/OTP (irreversible).
* Commands: `READ 01+addr`, `WRITE 53+addr+data`.

### FeliCa / Type 3, Type B, DESFire / Type 4
* FeliCa: NFCID2 (8B) + System Code (e.g. `12FC` NDEF). `CHECK 06` service `000B` reads 16B blocks. `forum3` does this.
* Type B: `scan` decodes PUPI(4) + ApplicationData(4) + ProtocolInfo(3). Use `transceive -p b` for ATTRIB/APDUs.
* DESFire/Plus/Type-4: ISO14443-4 APDUs via `transceive -p a -d <hex>`. Example GetVersion: `9060000000`. SELECT: `00A40400...`.
* Magic Gen1A vs Gen2: Gen1A needs `50 00 57 CD` backdoor unlock (like `nfc-mfsetuid`, not implemented); Gen2 allows direct Block 0 write (`clone`, `write -u`).
